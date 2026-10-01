#include "PluginProcessor.h"
#include "BqtParameterIds.h"

namespace
{
constexpr auto numOversamplingFactors = 3;
constexpr auto vuRiseTo99Seconds = 0.3f;
constexpr auto vuTimeConstantSeconds = vuRiseTo99Seconds / 4.605170186f;
constexpr auto vuSineAverageToRms = 1.110720735f;
constexpr auto parameterSmoothingSeconds = 0.02;
constexpr auto baxShelfQ = 0.38f;
// Grit's pre-drive gain (dB of gain per dB of Drive). Cream maps Drive through its own taper.
constexpr auto saturationDriveScale = 0.40f;
// Grit's linear coloration (and the shared Vintage shelf) fades in over the first third of the
// drive range, reaching full strength at 6 dB of the 18 dB range. bqt::CreamModel uses the same
// ramp internally for its net tilt and DC blocker.
constexpr auto colorationRampScale = 3.0f;
constexpr auto dcBlockerHz = 5.0f;
// Control-rate subdivision for the expensive per-sample recomputations (shelf coefficient
// redesign and the autogain pow). Smoothers still advance every sample, so ramp timing is
// unchanged; only the derived values are refreshed every N samples. At 48 kHz this is ~3 kHz,
// which is ~60 updates across a 20 ms ramp -- far finer than anything audible.
constexpr auto controlRateInterval = 16;
// Length of the fade applied when a discrete switch (routing, stage bypass, saturation type,
// shelf frequency, vintage) changes. Short enough to read as an instant switch, long enough that
// the coefficient/routing change underneath it is inaudible.
constexpr auto structuralFadeSeconds = 0.004;

// Stack-based coefficient factory: returns a std::array by value, so assigning it into
// an already-sized Filter coefficients object updates it in place with no heap allocation
// (unlike Coefficients::makeXxx, which news a ref-counted object every call).
using ArrayCoeffs = juce::dsp::IIR::ArrayCoefficients<float>;

float dbToGain(float db)
{
    return juce::Decibels::decibelsToGain(db);
}

float loadValue(const std::atomic<float>* parameter)
{
    return parameter->load();
}

int loadChoice(const std::atomic<float>* parameter)
{
    return static_cast<int>(parameter->load());
}

bool loadFlag(const std::atomic<float>* parameter)
{
    return parameter->load() > 0.5f;
}

float clampShelfFrequency(double sampleRate, float frequency)
{
    return juce::jlimit(10.0f, static_cast<float>(sampleRate * 0.42), frequency);
}
} // namespace

void BqtAudioProcessor::cacheParameterPointers()
{
    const auto get = [this](const juce::String& id) { return parameters.getRawParameterValue(id); };

    for (int side = 0; side < 2; ++side)
    {
        const auto prefix = bqt::sidePrefix(side);
        const auto index = static_cast<size_t>(side);
        paramPtrs.lowGain[index]    = get(prefix + "LowGain");
        paramPtrs.highGain[index]   = get(prefix + "HighGain");
        paramPtrs.lowFreq[index]    = get(prefix + "LowFreq");
        paramPtrs.highFreq[index]   = get(prefix + "HighFreq");
        paramPtrs.drive[index]      = get(prefix + "Drive");
        paramPtrs.satType[index]    = get(prefix + "SatType");
        paramPtrs.mix[index]        = get(prefix + "Mix");
        paramPtrs.outputTrim[index] = get(prefix + "OutputTrim");
    }

    paramPtrs.inputTrim  = get("inputTrim");
    paramPtrs.eqMode     = get("eqMode");
    paramPtrs.satMode    = get("satMode");
    paramPtrs.eqBypass   = get("eqBypass");
    paramPtrs.satBypass  = get("satBypass");
    paramPtrs.autoGain   = get("autoGain");
    paramPtrs.vintage    = get("vintage");
    paramPtrs.bypass     = get("bypass");
    paramPtrs.osRealtime = get("osRealtime");
    paramPtrs.osRender   = get("osRender");
}

void BqtAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    // Some hosts probe with a zero block size; clamp so the chunk loop in processBlock can
    // never fail to advance and every buffer below is sized to at least one sample.
    preparedBlockSize = juce::jmax(1, samplesPerBlock);
    samplesPerBlock = preparedBlockSize;

    const juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1 };
    for (auto& side : filters)
        side.forEachFilter([&spec](Filter& filter) { filter.prepare(spec); filter.reset(); });

    for (auto& dryBuffer : dryBuffers)
        dryBuffer.setSize(1, samplesPerBlock * 8, false, false, true);
    bypassDryBuffer.setSize(2, samplesPerBlock, false, false, true);

    for (auto& delay : dryMixDelays)
    {
        delay.prepare(spec);
        delay.setMaximumDelayInSamples(4096);
        delay.reset();
    }

    inputTrimGain.reset(sampleRate, parameterSmoothingSeconds);
    inputTrimGain.setCurrentAndTargetValue(1.0f);
    globalBypassMix.reset(sampleRate, parameterSmoothingSeconds);
    globalBypassMix.setCurrentAndTargetValue(0.0f);

    for (int side = 0; side < 2; ++side)
    {
        const auto index = static_cast<size_t>(side);
        eqLowGainDb[index].reset(sampleRate, parameterSmoothingSeconds);
        eqHighGainDb[index].reset(sampleRate, parameterSmoothingSeconds);
        driveAmount[index].reset(sampleRate, parameterSmoothingSeconds);
        driveGain[index].reset(sampleRate, parameterSmoothingSeconds);
        saturationMix[index].reset(sampleRate, parameterSmoothingSeconds);
        outputTrimGain[index].reset(sampleRate, parameterSmoothingSeconds);

        eqLowGainDb[index].setCurrentAndTargetValue(0.0f);
        eqHighGainDb[index].setCurrentAndTargetValue(0.0f);
        driveAmount[index].setCurrentAndTargetValue(0.0f);
        driveGain[index].setCurrentAndTargetValue(1.0f);
        saturationMix[index].setCurrentAndTargetValue(1.0f);
        outputTrimGain[index].setCurrentAndTargetValue(1.0f);
    }

    for (int factorIndex = 0; factorIndex < numOversamplingFactors; ++factorIndex)
    {
        oversamplers[static_cast<size_t>(factorIndex)] = std::make_unique<juce::dsp::Oversampling<float>>(
            2,
            static_cast<size_t>(factorIndex + 1),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
            true,
            true);
        oversamplers[static_cast<size_t>(factorIndex)]->initProcessing(static_cast<size_t>(samplesPerBlock));
        oversamplers[static_cast<size_t>(factorIndex)]->reset();
    }

    // Latch the discrete switches so the first block does not look like a change.
    activeConfig = readStructuralConfig();
    structuralTransition = StructuralTransition::idle;
    structuralGain = 1.0f;
    structuralStep = static_cast<float>(1.0 / juce::jmax(1.0, structuralFadeSeconds * sampleRate));

    updateFilters();
    updateSaturationToneFilters();

    // Set the saturation-side smoothers to the oversampled rate they will actually be consumed
    // at. State is already reset above, so no second reset is needed here.
    lastActiveOversamplingIndex = getActiveOversamplingIndex();
    applyOversamplingFactorChange(lastActiveOversamplingIndex, sampleRate, false);

    // prepareToPlay runs on the message thread, so set the host latency directly here.
    currentLatencySamples.store(computeLatencySamples());
    setLatencySamples(currentLatencySamples.load());
    meterRms = {};
    resetSaturationState();
}

void BqtAudioProcessor::updateFilters()
{
    for (int side = 0; side < 2; ++side)
    {
        const auto sideIndex = static_cast<size_t>(side);
        const auto lowGainDb = loadValue(paramPtrs.lowGain[sideIndex]);
        const auto highGainDb = loadValue(paramPtrs.highGain[sideIndex]);
        const auto lowFreq = clampShelfFrequency(currentSampleRate, bqt::lowShelfFrequenciesHz[static_cast<size_t>(activeConfig.lowFreq[sideIndex])]);
        const auto highFreq = clampShelfFrequency(currentSampleRate, bqt::highShelfFrequenciesHz[static_cast<size_t>(activeConfig.highFreq[sideIndex])]);

        eqLowGainDb[sideIndex].setTargetValue(lowGainDb);
        eqHighGainDb[sideIndex].setTargetValue(highGainDb);

        if (! eqLowGainDb[sideIndex].isSmoothing())
            *filters[sideIndex].lowShelf.coefficients = bqt::makeMatchedShelf(currentSampleRate, lowFreq, baxShelfQ, dbToGain(lowGainDb), false);

        if (! eqHighGainDb[sideIndex].isSmoothing())
            *filters[sideIndex].highShelf.coefficients = bqt::makeMatchedShelf(currentSampleRate, highFreq, baxShelfQ, dbToGain(highGainDb), true);
    }
}

void BqtAudioProcessor::updateSaturationToneFilters()
{
    const auto vintageEnabled = activeConfig.vintage ? 1 : 0;

    // These coefficients only change with the sample rate or the vintage flag, so skip the
    // rebuild (and its trig) on every block where neither moved. currentSampleRate already
    // reflects the active oversampling factor when this runs inside the oversampled chain.
    if (juce::exactlyEqual(satToneSampleRate, currentSampleRate) && satToneVintage == vintageEnabled)
        return;

    satToneSampleRate = currentSampleRate;
    satToneVintage = vintageEnabled;

    for (int side = 0; side < 2; ++side)
    {
        const auto sideIndex = static_cast<size_t>(side);
        // Fitted broad top shelf (tools/cream_model), shared by both saturation types.
        *filters[sideIndex].vintage.coefficients = ArrayCoeffs::makeHighShelf(
            currentSampleRate, static_cast<float>(bqt::cream::vintageHz), static_cast<float>(bqt::cream::vintageQ),
            dbToGain(vintageEnabled != 0 ? static_cast<float>(bqt::cream::vintageDb) : 0.0f));
        creamModels[sideIndex].prepare(currentSampleRate);
        *filters[sideIndex].saturationLowGuardPre.coefficients = ArrayCoeffs::makeLowShelf(currentSampleRate, 95.0f, 0.55f, dbToGain(-2.2f));
        *filters[sideIndex].saturationLowGuardPost.coefficients = ArrayCoeffs::makeLowShelf(currentSampleRate, 95.0f, 0.55f, dbToGain(2.2f));
        *filters[sideIndex].transformerLowDrive.coefficients = ArrayCoeffs::makeLowShelf(currentSampleRate, 165.0f, 0.62f, dbToGain(1.10f));
        *filters[sideIndex].transformerLowRestore.coefficients = ArrayCoeffs::makeLowShelf(currentSampleRate, 165.0f, 0.62f, dbToGain(-0.70f));
        *filters[sideIndex].transformerWeight.coefficients = ArrayCoeffs::makePeakFilter(currentSampleRate, 245.0f, 0.72f, dbToGain(0.55f));
        *filters[sideIndex].transformerTop.coefficients = ArrayCoeffs::makeHighShelf(currentSampleRate, 7800.0f, 0.50f, dbToGain(-0.75f));
    }
}

void BqtAudioProcessor::resetSaturationState()
{
    for (auto& side : filters)
        side.forEachSaturationFilter([](Filter& filter) { filter.reset(); });

    for (auto& model : creamModels)
        model.reset();

    dcBlockPreviousInput = {};
    dcBlockPreviousOutput = {};
}

BqtAudioProcessor::StructuralConfig BqtAudioProcessor::readStructuralConfig() const
{
    StructuralConfig config;
    config.eqMode = loadChoice(paramPtrs.eqMode);
    config.satMode = loadChoice(paramPtrs.satMode);
    config.eqBypassed = loadFlag(paramPtrs.eqBypass);
    config.satBypassed = loadFlag(paramPtrs.satBypass);
    config.vintage = loadFlag(paramPtrs.vintage);

    for (size_t side = 0; side < 2; ++side)
    {
        config.satType[side] = loadChoice(paramPtrs.satType[side]);
        config.lowFreq[side] = juce::jlimit(0, static_cast<int>(bqt::lowShelfFrequenciesHz.size()) - 1,
                                            loadChoice(paramPtrs.lowFreq[side]));
        config.highFreq[side] = juce::jlimit(0, static_cast<int>(bqt::highShelfFrequenciesHz.size()) - 1,
                                             loadChoice(paramPtrs.highFreq[side]));
    }

    return config;
}

void BqtAudioProcessor::adoptPendingStructuralConfig(const StructuralConfig& pending)
{
    activeConfig = pending;

    // Adopt at the bottom of the fade, and clear the filter state that the old configuration
    // accumulated. Without this the new routing/coefficients would ring out the old state as a
    // transient, which is the second half of the click.
    for (auto& side : filters)
    {
        side.lowShelf.reset();
        side.highShelf.reset();
    }
    resetSaturationState();

    // Force a coefficient rebuild for the new frequency/vintage selection.
    satToneSampleRate = 0.0;
    satToneVintage = -1;
    updateFilters();
}

void BqtAudioProcessor::advanceStructuralTransition(double hostSampleRate)
{
    const auto pending = readStructuralConfig();
    const auto fadeSamples = juce::jmax(1.0, structuralFadeSeconds * hostSampleRate);
    structuralStep = static_cast<float>(1.0 / fadeSamples);

    switch (structuralTransition)
    {
        case StructuralTransition::idle:
            if (pending != activeConfig)
                structuralTransition = StructuralTransition::fadingOut;
            break;

        case StructuralTransition::fadingOut:
            if (structuralGain <= 0.0f)
            {
                adoptPendingStructuralConfig(pending);
                structuralTransition = StructuralTransition::fadingIn;
            }
            break;

        case StructuralTransition::fadingIn:
            if (structuralGain >= 1.0f)
                structuralTransition = pending != activeConfig ? StructuralTransition::fadingOut
                                                               : StructuralTransition::idle;
            break;
    }
}

void BqtAudioProcessor::applyStructuralTransitionGain(float* left, float* right, int numSamples)
{
    if (structuralTransition == StructuralTransition::idle && structuralGain >= 1.0f)
        return;

    const auto direction = structuralTransition == StructuralTransition::fadingOut ? -1.0f : 1.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        structuralGain = juce::jlimit(0.0f, 1.0f, structuralGain + direction * structuralStep);
        // Raised cosine, so the dip has no corner at either end.
        const auto shaped = 0.5f - 0.5f * std::cos(structuralGain * juce::MathConstants<float>::pi);
        left[sample] *= shaped;
        right[sample] *= shaped;
    }
}

void BqtAudioProcessor::processEq(float* samples, int numSamples, int sideIndex)
{
    const auto filterIndex = static_cast<size_t>(sideIndex);
    const auto lowFreq = clampShelfFrequency(currentSampleRate, bqt::lowShelfFrequenciesHz[static_cast<size_t>(activeConfig.lowFreq[filterIndex])]);
    const auto highFreq = clampShelfFrequency(currentSampleRate, bqt::highShelfFrequenciesHz[static_cast<size_t>(activeConfig.highFreq[filterIndex])]);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Advance the smoothers every sample so ramp timing is exact, but redesign the biquads
        // only at the control rate. A full matched-shelf redesign is ~10 sqrt plus exp/cos/cosh
        // in double, and doing it per sample for two shelves on two sides meant ~200k redesigns
        // a second for the entire duration of an EQ knob drag.
        const auto lowSmoothing = eqLowGainDb[filterIndex].isSmoothing();
        const auto highSmoothing = eqHighGainDb[filterIndex].isSmoothing();
        const auto lowGainDb = lowSmoothing ? eqLowGainDb[filterIndex].getNextValue() : 0.0f;
        const auto highGainDb = highSmoothing ? eqHighGainDb[filterIndex].getNextValue() : 0.0f;

        if (sample % controlRateInterval == 0)
        {
            if (lowSmoothing)
                *filters[filterIndex].lowShelf.coefficients = bqt::makeMatchedShelf(currentSampleRate, lowFreq, baxShelfQ, dbToGain(lowGainDb), false);

            if (highSmoothing)
                *filters[filterIndex].highShelf.coefficients = bqt::makeMatchedShelf(currentSampleRate, highFreq, baxShelfQ, dbToGain(highGainDb), true);
        }

        auto value = samples[sample];
        value = filters[filterIndex].lowShelf.processSample(value);
        value = filters[filterIndex].highShelf.processSample(value);
        samples[sample] = value;
    }
}

void BqtAudioProcessor::processSide(float* samples, int numSamples, int sideIndex)
{
    const auto smoothIndex = static_cast<size_t>(sideIndex);
    const auto dryBufferIndex = smoothIndex;
    const auto driveDb = loadValue(paramPtrs.drive[smoothIndex]);
    const auto satType = static_cast<bqt::SaturationType>(activeConfig.satType[smoothIndex]);
    const auto autoGainEnabled = loadFlag(paramPtrs.autoGain);

    driveAmount[smoothIndex].setTargetValue(driveDb / 18.0f);
    driveGain[smoothIndex].setTargetValue(dbToGain(driveDb * saturationDriveScale));
    saturationMix[smoothIndex].setTargetValue(loadValue(paramPtrs.mix[smoothIndex]) / 100.0f);
    outputTrimGain[smoothIndex].setTargetValue(dbToGain(loadValue(paramPtrs.outputTrim[smoothIndex])));

    // Engage the saturation path when drive and mix are non-zero now or by the end of the
    // block, so the wet signal is processed across the whole transition rather than snapping
    // on/off at a block boundary.
    const auto driveActive = driveAmount[smoothIndex].getCurrentValue() > 0.0f
                          || driveAmount[smoothIndex].getTargetValue() > 0.0f;
    const auto mixActive = saturationMix[smoothIndex].getCurrentValue() > 0.0f
                        || saturationMix[smoothIndex].getTargetValue() > 0.0f;

    if (driveActive && mixActive)
    {
        // Sized to preparedBlockSize * 8 in prepareToPlay, and processBlock chunks to
        // preparedBlockSize, so 8x oversampling is an exact ceiling. No audio-thread realloc.
        auto& dryBuffer = dryBuffers[dryBufferIndex];
        jassert(dryBuffer.getNumSamples() >= numSamples);

        dryBuffer.copyFrom(0, 0, samples, numSamples);
        const auto* dry = dryBuffer.getReadPointer(0);

        auto& sideFilters = filters[smoothIndex];
        const auto isDensity = satType == bqt::SaturationType::density;

        // Grit's coloration filters (and the shared Vintage shelf) are blended toward their input
        // by a drive-derived amount so the stage converges to unity as drive -> 0: the branch is
        // gated on drive being non-zero, and applying them at full strength would snap a tilt into
        // place the instant Drive leaves 0.0. The filters still process every sample, so their
        // state stays warm and the blend can rise without a transient. Cream ramps its own tone
        // shaping and DC blocker inside bqt::CreamModel.
        const auto dcBlockCoefficient = std::exp(-2.0f * juce::MathConstants<float>::pi
                                                 * dcBlockerHz / static_cast<float>(currentSampleRate));
        auto& previousInput = dcBlockPreviousInput[smoothIndex];
        auto& previousOutput = dcBlockPreviousOutput[smoothIndex];
        auto compensation = 1.0f;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            // Smooth drive, pre-drive gain, mix and the drive-derived autogain per sample so
            // automating them ramps within the block instead of stepping once per block.
            const auto drive = driveAmount[smoothIndex].getNextValue();
            const auto driveGainValue = driveGain[smoothIndex].getNextValue();
            const auto mix = saturationMix[smoothIndex].getNextValue();

            // saturationAutoGain calls std::pow, which at 8x oversampling ran ~768k times a
            // second in stereo whenever drive was non-zero. It is a smooth function of drive, so
            // refreshing it at the control rate is inaudible; when drive is not smoothing it is
            // exactly constant anyway.
            if (sample % controlRateInterval == 0)
            {
                compensation = autoGainEnabled ? bqt::saturationAutoGain(drive, satType) : 1.0f;
                if (isDensity)
                    creamModels[smoothIndex].setKnob(static_cast<double>(drive) * 18.0);
            }

            const auto color = juce::jmin(1.0f, drive * colorationRampScale);
            const auto colored = [color](Filter& filter, float input)
            {
                return input + (filter.processSample(input) - input) * color;
            };

            auto value = samples[sample];
            if (isDensity)
            {
                value = static_cast<float>(creamModels[smoothIndex].process(value));
                value = colored(sideFilters.vintage, value);
            }
            else
            {
                value = colored(sideFilters.saturationLowGuardPre, value);
                value = colored(sideFilters.transformerLowDrive, value);
                value = colored(sideFilters.transformerWeight, value);
                value *= driveGainValue;
                value = bqt::transformerSaturate(value, drive);
                value = colored(sideFilters.transformerLowRestore, value);
                value = colored(sideFilters.transformerTop, value);
                value = colored(sideFilters.saturationLowGuardPost, value);
                value = colored(sideFilters.vintage, value);

                // Grit's curve is asymmetric and only subtracts a constant tanh(bias), so it leaves
                // a signal-dependent DC offset. Blended by the colour factor so drive -> 0 stays
                // exactly transparent; there is negligible DC to remove at low drive anyway.
                const auto blocked = value - previousInput + dcBlockCoefficient * previousOutput;
                previousInput = value;
                previousOutput = blocked;
                value += (blocked - value) * color;
            }

            const auto wet = value * compensation;
            samples[sample] = dry[sample] + (wet - dry[sample]) * mix;
        }
    }
    else
    {
        // Saturation inactive this block: still advance the smoothers so re-engaging is smooth.
        driveAmount[smoothIndex].skip(numSamples);
        driveGain[smoothIndex].skip(numSamples);
        saturationMix[smoothIndex].skip(numSamples);
    }

    for (int sample = 0; sample < numSamples; ++sample)
        samples[sample] *= outputTrimGain[smoothIndex].getNextValue();

    updateMeter(sideIndex, samples, numSamples);
}

void BqtAudioProcessor::applyLatencyDelay(float* samples, int numSamples, int sideIndex)
{
    const auto latencySamples = currentLatencySamples.load();
    if (latencySamples <= 0)
        return;

    auto& delay = dryMixDelays[static_cast<size_t>(sideIndex)];
    delay.setDelay(static_cast<float>(latencySamples));

    for (int sample = 0; sample < numSamples; ++sample)
    {
        delay.pushSample(0, samples[sample]);
        samples[sample] = delay.popSample(0);
    }
}

void BqtAudioProcessor::updateMeter(int sideIndex, const float* samples, int numSamples)
{
    // A zero-length block would divide by zero here. The resulting NaN is not transient: it
    // latches into meterRms permanently, because the release/attack blend leaves 0 * NaN = NaN.
    if (numSamples <= 0)
        return;

    auto rectifiedSum = 0.0f;
    for (int sample = 0; sample < numSamples; ++sample)
        rectifiedSum += std::abs(samples[sample]);

    const auto blockAverage = (rectifiedSum / static_cast<float>(numSamples)) * vuSineAverageToRms;
    const auto blockSeconds = static_cast<float>(numSamples / currentSampleRate);
    const auto release = std::exp(-blockSeconds / vuTimeConstantSeconds);
    const auto attack = 1.0f - release;

    const auto index = static_cast<size_t>(sideIndex);
    meterRms[index] = meterRms[index] * release + blockAverage * attack;
    meterLevels[index].store(meterRms[index]);
}

float BqtAudioProcessor::getMeterLevel(int sideIndex) const
{
    return meterLevels[static_cast<size_t>(juce::jlimit(0, 1, sideIndex))].load();
}

// Runs at the host sample rate, before any upsampling. The shelves are linear, so oversampling
// them bought nothing but CPU -- and because their coefficients were designed at whatever the
// oversampled rate happened to be, the EQ curve moved when the oversampling control moved.
// bqt::makeMatchedShelf now tracks the analog prototype at the host rate, so the curve is fixed.
void BqtAudioProcessor::processEqStage(float* left, float* right, int numSamples)
{
    updateFilters();

    const auto eqMidSide = activeConfig.eqMode == static_cast<int>(bqt::ChannelMode::midSide);
    const auto eqBypassed = activeConfig.eqBypassed;

    inputTrimGain.setTargetValue(dbToGain(loadValue(paramPtrs.inputTrim)));
    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto gain = inputTrimGain.getNextValue();
        left[sample] *= gain;
        right[sample] *= gain;
    }

    if (!eqBypassed && eqMidSide)
        bqt::encodeMidSide(left, right, numSamples);

    if (!eqBypassed)
    {
        processEq(left, numSamples, 0);
        processEq(right, numSamples, 1);
    }

    if (!eqBypassed && eqMidSide)
        bqt::decodeMidSide(left, right, numSamples);
}

// Runs inside the oversampled region: this is the stage that actually generates harmonics.
void BqtAudioProcessor::processSaturationStage(float* left, float* right, int numSamples)
{
    updateSaturationToneFilters();

    const auto satMidSide = activeConfig.satMode == static_cast<int>(bqt::ChannelMode::midSide);
    const auto satBypassed = activeConfig.satBypassed;

    if (!satBypassed && satMidSide)
        bqt::encodeMidSide(left, right, numSamples);

    if (!satBypassed)
    {
        processSide(left, numSamples, 0);
        processSide(right, numSamples, 1);
    }
    else
    {
        updateMeter(0, left, numSamples);
        updateMeter(1, right, numSamples);
    }

    if (!satBypassed && satMidSide)
        bqt::decodeMidSide(left, right, numSamples);
}

int BqtAudioProcessor::getActiveOversamplingIndex() const
{
    const auto choice = loadChoice(isNonRealtime() ? paramPtrs.osRender : paramPtrs.osRealtime);

    if (choice <= 0)
        return -1;

    return juce::jlimit(0, numOversamplingFactors - 1, choice - 1);
}

void BqtAudioProcessor::applyOversamplingFactorChange(int oversamplingIndex, double hostSampleRate, bool resetState)
{
    const auto factor = oversamplingIndex >= 0
                      ? static_cast<double>(oversamplers[static_cast<size_t>(oversamplingIndex)]->getOversamplingFactor())
                      : 1.0;
    const auto effectiveRate = hostSampleRate * factor;

    // These smoothers are consumed once per OVERSAMPLED sample inside processSide, so their ramp
    // length has to be derived from the oversampled rate. Resetting them at the base rate made a
    // 20 ms ramp finish in 2.5 ms at 8x, and made the same automation move sound different
    // depending on the oversampling setting. Preserve both the current and target value so
    // re-rating mid-ramp continues the ramp rather than snapping (SmoothedValue::reset() would
    // jump straight to the target).
    const auto rerate = [effectiveRate](auto& smoother)
    {
        const auto current = smoother.getCurrentValue();
        const auto target = smoother.getTargetValue();
        smoother.reset(effectiveRate, parameterSmoothingSeconds);
        smoother.setCurrentAndTargetValue(current);
        smoother.setTargetValue(target);
    };

    for (int side = 0; side < 2; ++side)
    {
        const auto index = static_cast<size_t>(side);
        rerate(driveAmount[index]);
        rerate(driveGain[index]);
        rerate(saturationMix[index]);
        rerate(outputTrimGain[index]);
    }

    if (resetState)
    {
        // The selected oversampler may not have run for minutes, and the saturation filters were
        // designed for the previous rate while holding state accumulated at it. The dry delay
        // lines are also misaligned because the reported latency just changed.
        if (oversamplingIndex >= 0)
            oversamplers[static_cast<size_t>(oversamplingIndex)]->reset();

        resetSaturationState();

        for (auto& delay : dryMixDelays)
            delay.reset();
    }
}

int BqtAudioProcessor::computeLatencySamples() const
{
    const auto oversamplingIndex = getActiveOversamplingIndex();

    if (oversamplingIndex >= 0 && oversamplers[static_cast<size_t>(oversamplingIndex)] != nullptr)
        return static_cast<int>(std::round(oversamplers[static_cast<size_t>(oversamplingIndex)]->getLatencyInSamples()));

    return 0;
}

void BqtAudioProcessor::updateLatency()
{
    const auto latency = computeLatencySamples();

    if (latency != currentLatencySamples.load())
    {
        currentLatencySamples.store(latency);
        // Only raise a flag here. setLatencySamples() notifies the host and can take locks, so it
        // cannot run on the audio thread -- but neither can triggerAsyncUpdate(), which posts to
        // the message queue behind a CriticalSection and may reallocate. The editor's timer polls
        // this flag, and prepareToPlay reports directly since it runs on the message thread.
        latencyNeedsReporting.store(true);
    }
}

void BqtAudioProcessor::timerCallback()
{
    if (latencyNeedsReporting.exchange(false))
        setLatencySamples(currentLatencySamples.load());
}

void BqtAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Supported layout is stereo-in/stereo-out, but never index channel 1 blindly: clear
    // any outputs past the inputs and bail if a host hands us fewer than two channels (e.g.
    // a layout probe), since the chain writes left/right pointers directly.
    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    if (buffer.getNumChannels() < 2)
        return;

    // A host is not obliged to honour the samplesPerBlock it passed to prepareToPlay, and every
    // internal buffer plus each oversampler's internal storage is sized from that value.
    // juce::dsp::Oversampling guards its bounds with jassert only, which compiles out in release,
    // so an oversized block would write past the end of its buffer. Split into prepared-size
    // chunks instead: that makes those sizes provable ceilings and keeps the audio thread free of
    // any reallocation. A zero-length block falls straight out of the loop.
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    const auto totalSamples = buffer.getNumSamples();

    for (int offset = 0; offset < totalSamples; offset += preparedBlockSize)
        processSubBlock(left + offset, right + offset, juce::jmin(preparedBlockSize, totalSamples - offset));
}

void BqtAudioProcessor::processSubBlock(float* left, float* right, int numSamples)
{
    const auto hostSampleRate = getSampleRate();
    currentSampleRate = hostSampleRate;
    updateLatency();

    const auto bypassEnabled = loadFlag(paramPtrs.bypass);
    globalBypassMix.setTargetValue(bypassEnabled ? 1.0f : 0.0f);
    const auto needsBypassCrossfade = bypassEnabled || globalBypassMix.isSmoothing() || globalBypassMix.getCurrentValue() > 0.0f;

    // Feed the latency-compensating dry delay lines on every block, not just while a crossfade
    // is running. If they are only fed during a crossfade they hold reset() zeros the first time
    // bypass is engaged (so the crossfade dips toward silence) and stale audio from the previous
    // un-bypass every time after that. The delayed copy is still only *consumed* when crossfading.
    // processBlock chunks to preparedBlockSize, which is what this buffer was sized from.
    jassert(bypassDryBuffer.getNumSamples() >= numSamples);
    auto* dryLeft = bypassDryBuffer.getWritePointer(0);
    auto* dryRight = bypassDryBuffer.getWritePointer(1);
    juce::FloatVectorOperations::copy(dryLeft, left, numSamples);
    juce::FloatVectorOperations::copy(dryRight, right, numSamples);
    applyLatencyDelay(dryLeft, numSamples, 0);
    applyLatencyDelay(dryRight, numSamples, 1);

    if (needsBypassCrossfade && bypassEnabled && !globalBypassMix.isSmoothing()
        && globalBypassMix.getCurrentValue() >= 1.0f)
    {
        juce::FloatVectorOperations::copy(left, dryLeft, numSamples);
        juce::FloatVectorOperations::copy(right, dryRight, numSamples);
        return;
    }

    // A discrete switch changing mid-signal steps coefficients or reroutes channels with live
    // filter state, which clicks. Adopt any change at the bottom of a short fade instead.
    advanceStructuralTransition(hostSampleRate);

    const auto oversamplingIndex = getActiveOversamplingIndex();

    // Covers both a user change to the oversampling control and the realtime -> render
    // transition, which switches from osRealtime to osRender without a prepareToPlay in hosts
    // that do not re-prepare for offline bounces.
    if (oversamplingIndex != lastActiveOversamplingIndex)
    {
        applyOversamplingFactorChange(oversamplingIndex, hostSampleRate, true);
        lastActiveOversamplingIndex = oversamplingIndex;
    }

    // The EQ is linear, so it runs at the host rate outside the oversampled region: cheaper, and
    // its curve no longer depends on the oversampling factor. Only the saturation, which is what
    // actually generates harmonics, needs the oversampled region.
    processEqStage(left, right, numSamples);

    if (oversamplingIndex >= 0)
    {
        auto& oversampler = *oversamplers[static_cast<size_t>(oversamplingIndex)];
        float* channels[2] { left, right };
        juce::dsp::AudioBlock<float> block(channels, 2, static_cast<size_t>(numSamples));
        const auto upsampledBlock = oversampler.processSamplesUp(block);
        currentSampleRate = hostSampleRate * static_cast<double>(oversampler.getOversamplingFactor());
        processSaturationStage(upsampledBlock.getChannelPointer(0),
                               upsampledBlock.getChannelPointer(1),
                               static_cast<int>(upsampledBlock.getNumSamples()));
        oversampler.processSamplesDown(block);
        currentSampleRate = hostSampleRate;
    }
    else
    {
        processSaturationStage(left, right, numSamples);
    }

    // Applied to the processed signal only, so the bypass dry path below stays untouched.
    applyStructuralTransitionGain(left, right, numSamples);

    if (needsBypassCrossfade)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto bypassMix = globalBypassMix.getNextValue();
            left[sample] = left[sample] + (dryLeft[sample] - left[sample]) * bypassMix;
            right[sample] = right[sample] + (dryRight[sample] - right[sample]) * bypassMix;
        }
    }
}
