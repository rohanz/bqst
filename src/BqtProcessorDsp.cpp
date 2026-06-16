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
    // prepareToPlay runs on the message thread, so set the host latency directly here.
    currentLatencySamples.store(computeLatencySamples());
    setLatencySamples(currentLatencySamples.load());
    meterRms = {};
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
            *filters[sideIndex].lowShelf.coefficients = ArrayCoeffs::makeLowShelf(currentSampleRate, lowFreq, baxShelfQ, dbToGain(lowGainDb));

        if (! eqHighGainDb[sideIndex].isSmoothing())
            *filters[sideIndex].highShelf.coefficients = ArrayCoeffs::makeHighShelf(currentSampleRate, highFreq, baxShelfQ, dbToGain(highGainDb));
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
            *filters[filterIndex].lowShelf.coefficients = ArrayCoeffs::makeLowShelf(currentSampleRate, lowFreq, baxShelfQ, dbToGain(eqLowGainDb[filterIndex].getNextValue()));

        if (eqHighGainDb[filterIndex].isSmoothing())
            *filters[filterIndex].highShelf.coefficients = ArrayCoeffs::makeHighShelf(currentSampleRate, highFreq, baxShelfQ, dbToGain(eqHighGainDb[filterIndex].getNextValue()));

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
        auto& dryBuffer = dryBuffers[dryBufferIndex];
        if (dryBuffer.getNumSamples() < numSamples)
            dryBuffer.setSize(1, numSamples, false, false, true);

        dryBuffer.copyFrom(0, 0, samples, numSamples);
        const auto* dry = dryBuffer.getReadPointer(0);

        auto& sideFilters = filters[smoothIndex];
        const auto isDensity = satType == bqt::SaturationType::density;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            // Smooth drive, pre-drive gain, mix and the drive-derived autogain per sample so
            // automating them ramps within the block instead of stepping once per block.
            const auto drive = driveAmount[smoothIndex].getNextValue();
            const auto driveGainValue = driveGain[smoothIndex].getNextValue();
            const auto mix = saturationMix[smoothIndex].getNextValue();
            const auto compensation = autoGainEnabled ? bqt::saturationAutoGain(drive, satType) : 1.0f;

            auto value = sideFilters.saturationLowGuardPre.processSample(samples[sample]);
            if (isDensity)
            {
                value = sideFilters.densityBodyFocus.processSample(value);
                value = sideFilters.densityPreEmphasis.processSample(value);
            }
            else
            {
                value = sideFilters.transformerLowDrive.processSample(value);
                value = sideFilters.transformerWeight.processSample(value);
            }

            value *= driveGainValue;
            value = isDensity ? bqt::densitySaturate(value, drive) : bqt::transformerSaturate(value, drive);

            if (isDensity)
            {
                value = sideFilters.densityDeEmphasis.processSample(value);
            }
            else
            {
                value = sideFilters.transformerLowRestore.processSample(value);
                value = sideFilters.transformerTop.processSample(value);
            }

            value = sideFilters.saturationLowGuardPost.processSample(value);
            value = sideFilters.vintage.processSample(value);

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

void BqtAudioProcessor::processChain(float* left, float* right, int numSamples)
{
    updateFilters();
    updateSaturationToneFilters();

    const auto eqMidSide = loadChoice(paramPtrs.eqMode) == static_cast<int>(bqt::ChannelMode::midSide);
    const auto satMidSide = loadChoice(paramPtrs.satMode) == static_cast<int>(bqt::ChannelMode::midSide);
    const auto eqBypassed = loadFlag(paramPtrs.eqBypass);
    const auto satBypassed = loadFlag(paramPtrs.satBypass);

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

    const auto hostSampleRate = getSampleRate();
    currentSampleRate = hostSampleRate;
    updateLatency();

    const auto numSamples = buffer.getNumSamples();
    const auto bypassEnabled = loadFlag(paramPtrs.bypass);
    globalBypassMix.setTargetValue(bypassEnabled ? 1.0f : 0.0f);
    const auto needsBypassCrossfade = bypassEnabled || globalBypassMix.isSmoothing() || globalBypassMix.getCurrentValue() > 0.0f;

    if (needsBypassCrossfade)
    {
        if (bypassDryBuffer.getNumSamples() < numSamples)
            bypassDryBuffer.setSize(2, numSamples, false, false, true);

        bypassDryBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
        bypassDryBuffer.copyFrom(1, 0, buffer, 1, 0, numSamples);
        applyLatencyDelay(bypassDryBuffer.getWritePointer(0), numSamples, 0);
        applyLatencyDelay(bypassDryBuffer.getWritePointer(1), numSamples, 1);

        if (bypassEnabled && !globalBypassMix.isSmoothing() && globalBypassMix.getCurrentValue() >= 1.0f)
        {
            buffer.copyFrom(0, 0, bypassDryBuffer, 0, 0, numSamples);
            buffer.copyFrom(1, 0, bypassDryBuffer, 1, 0, numSamples);
            return;
        }
    }

    const auto oversamplingIndex = getActiveOversamplingIndex();

    if (oversamplingIndex >= 0)
    {
        auto& oversampler = *oversamplers[static_cast<size_t>(oversamplingIndex)];
        juce::dsp::AudioBlock<float> block(buffer);
        const auto upsampledBlock = oversampler.processSamplesUp(block);
        currentSampleRate = hostSampleRate * static_cast<double>(oversampler.getOversamplingFactor());
        processChain(upsampledBlock.getChannelPointer(0),
                     upsampledBlock.getChannelPointer(1),
                     static_cast<int>(upsampledBlock.getNumSamples()));
        oversampler.processSamplesDown(block);
        currentSampleRate = hostSampleRate;
    }
    else
    {
        processChain(buffer.getWritePointer(0), buffer.getWritePointer(1), numSamples);
    }

    if (needsBypassCrossfade)
    {
        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        const auto* dryLeft = bypassDryBuffer.getReadPointer(0);
        const auto* dryRight = bypassDryBuffer.getReadPointer(1);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto bypassMix = globalBypassMix.getNextValue();
            left[sample] = left[sample] + (dryLeft[sample] - left[sample]) * bypassMix;
            right[sample] = right[sample] + (dryRight[sample] - right[sample]) * bypassMix;
        }
    }
}
