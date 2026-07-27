#include "PluginProcessor.h"

namespace
{
constexpr auto sqrtHalf = 0.70710678118654752440f;
constexpr auto numOversamplingFactors = 3;
constexpr auto vuRiseTo99Seconds = 0.3f;
constexpr auto vuTimeConstantSeconds = vuRiseTo99Seconds / 4.605170186f;
constexpr auto vuSineAverageToRms = 1.110720735f;
constexpr auto parameterSmoothingSeconds = 0.02;
constexpr auto baxShelfQ = 0.38f;
constexpr auto saturationDriveScale = 0.40f;
// The saturation stage's linear coloration fades in over the first third of the drive range, so
// it reaches full strength at 6 dB of the 18 dB range. Above that the chain is bit-identical to
// the previous voicing, which also keeps the autogain calibration valid where it matters.
constexpr auto colorationRampScale = 3.0f;
constexpr auto dcBlockerHz = 5.0f;

// Stack-based coefficient factory: returns a std::array by value, so assigning it into
// an already-sized Filter coefficients object updates it in place with no heap allocation
// (unlike Coefficients::makeXxx, which news a ref-counted object every call).
using ArrayCoeffs = juce::dsp::IIR::ArrayCoefficients<float>;

juce::String sidePrefix(int sideIndex)
{
    return sideIndex == 0 ? "a" : "b";
}

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
        const auto prefix = sidePrefix(side);
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
    {
        side.lowShelf.prepare(spec);
        side.highShelf.prepare(spec);
        side.vintage.prepare(spec);
        side.densityBodyFocus.prepare(spec);
        side.densityPreEmphasis.prepare(spec);
        side.densityDeEmphasis.prepare(spec);
        side.saturationLowGuardPre.prepare(spec);
        side.saturationLowGuardPost.prepare(spec);
        side.transformerLowDrive.prepare(spec);
        side.transformerLowRestore.prepare(spec);
        side.transformerWeight.prepare(spec);
        side.transformerTop.prepare(spec);
        side.lowShelf.reset();
        side.highShelf.reset();
        side.vintage.reset();
        side.densityBodyFocus.reset();
        side.densityPreEmphasis.reset();
        side.densityDeEmphasis.reset();
        side.saturationLowGuardPre.reset();
        side.saturationLowGuardPost.reset();
        side.transformerLowDrive.reset();
        side.transformerLowRestore.reset();
        side.transformerWeight.reset();
        side.transformerTop.reset();
    }

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
    dcBlockPreviousInput = {};
    dcBlockPreviousOutput = {};
}

void BqtAudioProcessor::updateFilters()
{
    for (int side = 0; side < 2; ++side)
    {
        const auto sideIndex = static_cast<size_t>(side);
        const auto lowGainDb = loadValue(paramPtrs.lowGain[sideIndex]);
        const auto highGainDb = loadValue(paramPtrs.highGain[sideIndex]);
        const auto lowFreq = clampShelfFrequency(currentSampleRate, bqt::lowShelfFrequenciesHz[static_cast<size_t>(loadChoice(paramPtrs.lowFreq[sideIndex]))]);
        const auto highFreq = clampShelfFrequency(currentSampleRate, bqt::highShelfFrequenciesHz[static_cast<size_t>(loadChoice(paramPtrs.highFreq[sideIndex]))]);

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
    const auto vintageEnabled = loadFlag(paramPtrs.vintage) ? 1 : 0;

    // These coefficients only change with the sample rate or the vintage flag, so skip the
    // rebuild (and its trig) on every block where neither moved. currentSampleRate already
    // reflects the active oversampling factor when this runs inside the oversampled chain.
    if (juce::exactlyEqual(satToneSampleRate, currentSampleRate) && satToneVintage == vintageEnabled)
        return;

    satToneSampleRate = currentSampleRate;
    satToneVintage = vintageEnabled;

    const auto vintageGainDb = vintageEnabled != 0 ? -3.2f : 0.0f;

    for (int side = 0; side < 2; ++side)
    {
        const auto sideIndex = static_cast<size_t>(side);
        *filters[sideIndex].vintage.coefficients = ArrayCoeffs::makeHighShelf(currentSampleRate, 12000.0f, 0.42f, dbToGain(vintageGainDb));
        *filters[sideIndex].densityBodyFocus.coefficients = ArrayCoeffs::makePeakFilter(currentSampleRate, 720.0f, 0.52f, dbToGain(0.85f));
        *filters[sideIndex].densityPreEmphasis.coefficients = ArrayCoeffs::makeHighShelf(currentSampleRate, 6200.0f, 0.55f, dbToGain(-0.65f));
        *filters[sideIndex].densityDeEmphasis.coefficients = ArrayCoeffs::makeHighShelf(currentSampleRate, 7000.0f, 0.50f, dbToGain(-0.55f));
        *filters[sideIndex].saturationLowGuardPre.coefficients = ArrayCoeffs::makeLowShelf(currentSampleRate, 95.0f, 0.55f, dbToGain(-2.2f));
        *filters[sideIndex].saturationLowGuardPost.coefficients = ArrayCoeffs::makeLowShelf(currentSampleRate, 95.0f, 0.55f, dbToGain(2.2f));
        *filters[sideIndex].transformerLowDrive.coefficients = ArrayCoeffs::makeLowShelf(currentSampleRate, 165.0f, 0.62f, dbToGain(1.10f));
        *filters[sideIndex].transformerLowRestore.coefficients = ArrayCoeffs::makeLowShelf(currentSampleRate, 165.0f, 0.62f, dbToGain(-0.70f));
        *filters[sideIndex].transformerWeight.coefficients = ArrayCoeffs::makePeakFilter(currentSampleRate, 245.0f, 0.72f, dbToGain(0.55f));
        *filters[sideIndex].transformerTop.coefficients = ArrayCoeffs::makeHighShelf(currentSampleRate, 7800.0f, 0.50f, dbToGain(-0.75f));
    }
}

void BqtAudioProcessor::processEq(float* samples, int numSamples, int sideIndex)
{
    const auto filterIndex = static_cast<size_t>(sideIndex);
    const auto lowFreq = clampShelfFrequency(currentSampleRate, bqt::lowShelfFrequenciesHz[static_cast<size_t>(loadChoice(paramPtrs.lowFreq[filterIndex]))]);
    const auto highFreq = clampShelfFrequency(currentSampleRate, bqt::highShelfFrequenciesHz[static_cast<size_t>(loadChoice(paramPtrs.highFreq[filterIndex]))]);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (eqLowGainDb[filterIndex].isSmoothing())
            *filters[filterIndex].lowShelf.coefficients = bqt::makeMatchedShelf(currentSampleRate, lowFreq, baxShelfQ, dbToGain(eqLowGainDb[filterIndex].getNextValue()), false);

        if (eqHighGainDb[filterIndex].isSmoothing())
            *filters[filterIndex].highShelf.coefficients = bqt::makeMatchedShelf(currentSampleRate, highFreq, baxShelfQ, dbToGain(eqHighGainDb[filterIndex].getNextValue()), true);

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
    const auto satType = static_cast<bqt::SaturationType>(loadChoice(paramPtrs.satType[smoothIndex]));
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

        // The coloration filters below are linear and were previously applied at full,
        // drive-independent strength inside this branch. Because the branch itself is gated on
        // drive being non-zero, nudging Drive from 0.0 to 0.1 dB snapped roughly 1.9 dB of tilt
        // into place with no ramp (+0.85 dB at 720 Hz, -1.2 dB at the top, for Cream). Blending
        // each filter toward its input by a drive-derived amount makes the whole stage converge
        // to unity as drive -> 0, so zero drive really is transparent. The filters still process
        // every sample, so their state stays warm and the blend can rise without a transient.
        const auto dcBlockCoefficient = std::exp(-2.0f * juce::MathConstants<float>::pi
                                                 * dcBlockerHz / static_cast<float>(currentSampleRate));
        auto& previousInput = dcBlockPreviousInput[smoothIndex];
        auto& previousOutput = dcBlockPreviousOutput[smoothIndex];

        for (int sample = 0; sample < numSamples; ++sample)
        {
            // Smooth drive, pre-drive gain, mix and the drive-derived autogain per sample so
            // automating them ramps within the block instead of stepping once per block.
            const auto drive = driveAmount[smoothIndex].getNextValue();
            const auto driveGainValue = driveGain[smoothIndex].getNextValue();
            const auto mix = saturationMix[smoothIndex].getNextValue();
            const auto compensation = autoGainEnabled ? bqt::saturationAutoGain(drive, satType) : 1.0f;

            const auto color = juce::jmin(1.0f, drive * colorationRampScale);
            const auto colored = [color](Filter& filter, float input)
            {
                return input + (filter.processSample(input) - input) * color;
            };

            auto value = colored(sideFilters.saturationLowGuardPre, samples[sample]);
            if (isDensity)
            {
                value = colored(sideFilters.densityBodyFocus, value);
                value = colored(sideFilters.densityPreEmphasis, value);
            }
            else
            {
                value = colored(sideFilters.transformerLowDrive, value);
                value = colored(sideFilters.transformerWeight, value);
            }

            value *= driveGainValue;
            value = isDensity ? bqt::densitySaturate(value, drive) : bqt::transformerSaturate(value, drive);

            if (isDensity)
            {
                value = colored(sideFilters.densityDeEmphasis, value);
            }
            else
            {
                value = colored(sideFilters.transformerLowRestore, value);
                value = colored(sideFilters.transformerTop, value);
            }

            value = colored(sideFilters.saturationLowGuardPost, value);
            value = colored(sideFilters.vintage, value);

            // Both curves are asymmetric and only subtract a constant tanh(bias), so they leave a
            // signal-dependent DC offset (measured -0.059, about -24.6 dBFS, at full Cream drive)
            // which the +2.2 dB post low-shelf then lifts further. Nothing downstream removed it.
            // Blended by the same colour factor so drive -> 0 stays exactly transparent; there is
            // negligible DC to remove at low drive anyway.
            const auto blocked = value - previousInput + dcBlockCoefficient * previousOutput;
            previousInput = value;
            previousOutput = blocked;
            value += (blocked - value) * color;

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

    const auto eqMidSide = loadChoice(paramPtrs.eqMode) == static_cast<int>(bqt::ChannelMode::midSide);
    const auto eqBypassed = loadFlag(paramPtrs.eqBypass);

    inputTrimGain.setTargetValue(dbToGain(loadValue(paramPtrs.inputTrim)));
    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto gain = inputTrimGain.getNextValue();
        left[sample] *= gain;
        right[sample] *= gain;
    }

    if (!eqBypassed && eqMidSide)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto mid = (left[i] + right[i]) * sqrtHalf;
            const auto side = (left[i] - right[i]) * sqrtHalf;
            left[i] = mid;
            right[i] = side;
        }
    }

    if (!eqBypassed)
    {
        processEq(left, numSamples, 0);
        processEq(right, numSamples, 1);
    }

    if (!eqBypassed && eqMidSide)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto l = (left[i] + right[i]) * sqrtHalf;
            const auto r = (left[i] - right[i]) * sqrtHalf;
            left[i] = l;
            right[i] = r;
        }
    }
}

// Runs inside the oversampled region: this is the stage that actually generates harmonics.
void BqtAudioProcessor::processSaturationStage(float* left, float* right, int numSamples)
{
    updateSaturationToneFilters();

    const auto satMidSide = loadChoice(paramPtrs.satMode) == static_cast<int>(bqt::ChannelMode::midSide);
    const auto satBypassed = loadFlag(paramPtrs.satBypass);

    if (!satBypassed && satMidSide)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto mid = (left[i] + right[i]) * sqrtHalf;
            const auto side = (left[i] - right[i]) * sqrtHalf;
            left[i] = mid;
            right[i] = side;
        }
    }

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
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto l = (left[i] + right[i]) * sqrtHalf;
            const auto r = (left[i] - right[i]) * sqrtHalf;
            left[i] = l;
            right[i] = r;
        }
    }
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

        for (auto& side : filters)
        {
            side.vintage.reset();
            side.densityBodyFocus.reset();
            side.densityPreEmphasis.reset();
            side.densityDeEmphasis.reset();
            side.saturationLowGuardPre.reset();
            side.saturationLowGuardPost.reset();
            side.transformerLowDrive.reset();
            side.transformerLowRestore.reset();
            side.transformerWeight.reset();
            side.transformerTop.reset();
        }

        for (auto& delay : dryMixDelays)
            delay.reset();

        dcBlockPreviousInput = {};
        dcBlockPreviousOutput = {};
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
        // Report to the host from the message thread: setLatencySamples() notifies the host
        // (updateHostDisplay) and can take locks, so it must not run on the audio thread.
        triggerAsyncUpdate();
    }
}

void BqtAudioProcessor::handleAsyncUpdate()
{
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
