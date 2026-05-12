#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr auto sqrtHalf = 0.70710678118654752440f;
constexpr auto numOversamplingFactors = 3;
constexpr auto minRmsForMatching = 1.0e-5f;
constexpr auto vuRiseTo99Seconds = 0.3f;
constexpr auto vuTimeConstantSeconds = vuRiseTo99Seconds / 4.605170186f;
constexpr auto vuSineAverageToRms = 1.110720735f;
constexpr auto parameterSmoothingSeconds = 0.02;
constexpr auto baxShelfQ = 0.38f;
constexpr auto saturationDriveScale = 0.40f;

juce::String sidePrefix(int sideIndex)
{
    return sideIndex == 0 ? "a" : "b";
}

float dbToGain(float db)
{
    return juce::Decibels::decibelsToGain(db);
}

float getFloat(juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    return state.getRawParameterValue(id)->load();
}

int getChoice(juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    return static_cast<int>(state.getRawParameterValue(id)->load());
}

float clampShelfFrequency(double sampleRate, float frequency)
{
    return juce::jlimit(10.0f, static_cast<float>(sampleRate * 0.42), frequency);
}
} // namespace

BqtAudioProcessor::BqtAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout BqtAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    const auto hiddenBool = juce::AudioParameterBoolAttributes().withAutomatable(false);

    params.push_back(std::make_unique<juce::AudioParameterChoice>("eqMode", "EQ Mode", juce::StringArray { "L/R", "M/S" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("satMode", "Sat Mode", juce::StringArray { "L/R", "M/S" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("osRealtime", "Realtime OS", juce::StringArray { "Off", "2x", "4x", "8x" }, 1));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("osRender", "Render OS", juce::StringArray { "Off", "2x", "4x", "8x" }, 2));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("inputTrim", "Input", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("autoGain", "Autogain", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "eqBypass", 1 }, "EQ In", false, hiddenBool));
    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "satBypass", 1 }, "Sat In", false, hiddenBool));
    params.push_back(std::make_unique<juce::AudioParameterBool>("eqLink", "EQ Link", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("satLink", "Sat Link", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("vintage", "Vintage", false));

    for (int side = 0; side < 2; ++side)
    {
        const auto prefix = sidePrefix(side);
        const juce::String label = side == 0 ? "L/M" : "R/S";

        params.push_back(std::make_unique<juce::AudioParameterFloat>(prefix + "LowGain", label + " LF", juce::NormalisableRange<float>(-6.0f, 6.0f, 0.1f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(prefix + "LowFreq", label + " LF Freq", juce::StringArray { "74", "84", "98", "116", "131", "166", "230", "361" }, 3));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(prefix + "HighGain", label + " HF", juce::NormalisableRange<float>(-6.0f, 6.0f, 0.1f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(prefix + "HighFreq", label + " HF Freq", juce::StringArray { "1.6k", "1.8k", "2.1k", "2.5k", "3.4k", "4.8k", "7.1k", "18k" }, 4));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(prefix + "Drive", label + " Drive", juce::NormalisableRange<float>(0.0f, 18.0f, 0.1f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(prefix + "SatType", label + " Type", juce::StringArray { "Cream", "Grit" }, 0));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(prefix + "Mix", label + " Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(prefix + "OutputTrim", label + " Output", juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    }

    return { params.begin(), params.end() };
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
        side.densityPreEmphasis.prepare(spec);
        side.densityDeEmphasis.prepare(spec);
        side.saturationLowGuardPre.prepare(spec);
        side.saturationLowGuardPost.prepare(spec);
        side.transformerWeight.prepare(spec);
        side.transformerTop.prepare(spec);
        side.lowShelf.reset();
        side.highShelf.reset();
        side.vintage.reset();
        side.densityPreEmphasis.reset();
        side.densityDeEmphasis.reset();
        side.saturationLowGuardPre.reset();
        side.saturationLowGuardPost.reset();
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
    updateLatency();
    meterRms = {};
}

void BqtAudioProcessor::releaseResources()
{
}

bool BqtAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void BqtAudioProcessor::updateFilters()
{
    for (int side = 0; side < 2; ++side)
    {
        const auto prefix = sidePrefix(side);
        const auto lowGainDb = getFloat(parameters, prefix + "LowGain");
        const auto highGainDb = getFloat(parameters, prefix + "HighGain");
        const auto lowFreq = clampShelfFrequency(currentSampleRate, bqt::lowShelfFrequenciesHz[static_cast<size_t>(getChoice(parameters, prefix + "LowFreq"))]);
        const auto highFreq = clampShelfFrequency(currentSampleRate, bqt::highShelfFrequenciesHz[static_cast<size_t>(getChoice(parameters, prefix + "HighFreq"))]);

        const auto sideIndex = static_cast<size_t>(side);
        eqLowGainDb[sideIndex].setTargetValue(lowGainDb);
        eqHighGainDb[sideIndex].setTargetValue(highGainDb);

        if (! eqLowGainDb[sideIndex].isSmoothing())
            *filters[sideIndex].lowShelf.coefficients = *Coefficients::makeLowShelf(currentSampleRate, lowFreq, baxShelfQ, dbToGain(lowGainDb));

        if (! eqHighGainDb[sideIndex].isSmoothing())
            *filters[sideIndex].highShelf.coefficients = *Coefficients::makeHighShelf(currentSampleRate, highFreq, baxShelfQ, dbToGain(highGainDb));
    }
}

void BqtAudioProcessor::updateSaturationToneFilters()
{
    const auto vintageEnabled = parameters.getRawParameterValue("vintage")->load() > 0.5f;
    const auto vintageGainDb = vintageEnabled ? -4.2f : 0.0f;

    for (int side = 0; side < 2; ++side)
    {
        const auto sideIndex = static_cast<size_t>(side);
        *filters[sideIndex].vintage.coefficients = *Coefficients::makeHighShelf(currentSampleRate, 12000.0f, 0.42f, dbToGain(vintageGainDb));
        *filters[sideIndex].densityPreEmphasis.coefficients = *Coefficients::makeHighShelf(currentSampleRate, 6200.0f, 0.55f, dbToGain(0.7f));
        *filters[sideIndex].densityDeEmphasis.coefficients = *Coefficients::makeHighShelf(currentSampleRate, 6200.0f, 0.55f, dbToGain(-0.9f));
        *filters[sideIndex].saturationLowGuardPre.coefficients = *Coefficients::makeLowShelf(currentSampleRate, 95.0f, 0.55f, dbToGain(-2.2f));
        *filters[sideIndex].saturationLowGuardPost.coefficients = *Coefficients::makeLowShelf(currentSampleRate, 95.0f, 0.55f, dbToGain(2.2f));
        *filters[sideIndex].transformerWeight.coefficients = *Coefficients::makePeakFilter(currentSampleRate, 240.0f, 0.75f, dbToGain(0.35f));
        *filters[sideIndex].transformerTop.coefficients = *Coefficients::makeHighShelf(currentSampleRate, 8200.0f, 0.50f, dbToGain(-0.6f));
    }
}

void BqtAudioProcessor::processEq(float* samples, int numSamples, int sideIndex)
{
    const auto filterIndex = static_cast<size_t>(sideIndex);
    const auto prefix = sidePrefix(sideIndex);
    const auto lowFreq = clampShelfFrequency(currentSampleRate, bqt::lowShelfFrequenciesHz[static_cast<size_t>(getChoice(parameters, prefix + "LowFreq"))]);
    const auto highFreq = clampShelfFrequency(currentSampleRate, bqt::highShelfFrequenciesHz[static_cast<size_t>(getChoice(parameters, prefix + "HighFreq"))]);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (eqLowGainDb[filterIndex].isSmoothing())
            *filters[filterIndex].lowShelf.coefficients = *Coefficients::makeLowShelf(currentSampleRate, lowFreq, baxShelfQ, dbToGain(eqLowGainDb[filterIndex].getNextValue()));

        if (eqHighGainDb[filterIndex].isSmoothing())
            *filters[filterIndex].highShelf.coefficients = *Coefficients::makeHighShelf(currentSampleRate, highFreq, baxShelfQ, dbToGain(eqHighGainDb[filterIndex].getNextValue()));

        auto value = samples[sample];
        value = filters[filterIndex].lowShelf.processSample(value);
        value = filters[filterIndex].highShelf.processSample(value);
        samples[sample] = value;
    }
}

void BqtAudioProcessor::processSide(float* samples, int numSamples, int sideIndex)
{
    const auto prefix = sidePrefix(sideIndex);
    const auto driveDb = getFloat(parameters, prefix + "Drive");
    const auto satType = static_cast<bqt::SaturationType>(getChoice(parameters, prefix + "SatType"));
    const auto autoGainEnabled = parameters.getRawParameterValue("autoGain")->load() > 0.5f;
    const auto dryBufferIndex = static_cast<size_t>(sideIndex);
    const auto smoothIndex = static_cast<size_t>(sideIndex);

    driveAmount[smoothIndex].setTargetValue(driveDb / 18.0f);
    driveGain[smoothIndex].setTargetValue(dbToGain(driveDb * saturationDriveScale));
    saturationMix[smoothIndex].setTargetValue(getFloat(parameters, prefix + "Mix") / 100.0f);
    outputTrimGain[smoothIndex].setTargetValue(dbToGain(getFloat(parameters, prefix + "OutputTrim")));

    const auto drive = driveAmount[smoothIndex].skip(numSamples);
    const auto currentDriveGain = driveGain[smoothIndex].skip(numSamples);
    const auto mix = saturationMix[smoothIndex].skip(numSamples);
    const auto compensation = autoGainEnabled ? bqt::saturationAutoGain(drive, satType) : 1.0f;

    if (drive > 0.0f && mix > 0.0f)
    {
        auto& dryBuffer = dryBuffers[dryBufferIndex];
        if (dryBuffer.getNumSamples() < numSamples)
            dryBuffer.setSize(1, numSamples, false, false, true);

        dryBuffer.copyFrom(0, 0, samples, numSamples);

        processSaturation(samples, numSamples, sideIndex, drive, currentDriveGain, satType, compensation);

        const auto* dry = dryBuffer.getReadPointer(0);

        if (autoGainEnabled)
        {
            auto dryEnergy = 0.0f;
            auto wetEnergy = 0.0f;

            for (int sample = 0; sample < numSamples; ++sample)
            {
                dryEnergy += dry[sample] * dry[sample];
                wetEnergy += samples[sample] * samples[sample];
            }

            const auto dryRms = std::sqrt(dryEnergy / static_cast<float>(numSamples));
            const auto wetRms = std::sqrt(wetEnergy / static_cast<float>(numSamples));

            if (dryRms > minRmsForMatching && wetRms > minRmsForMatching)
            {
                const auto wetMatchGain = juce::jlimit(0.25f, 4.0f, dryRms / wetRms);
                juce::FloatVectorOperations::multiply(samples, wetMatchGain, numSamples);
            }
        }

        for (int sample = 0; sample < numSamples; ++sample)
            samples[sample] = dry[sample] + (samples[sample] - dry[sample]) * mix;
    }

    for (int sample = 0; sample < numSamples; ++sample)
        samples[sample] *= outputTrimGain[smoothIndex].getNextValue();

    updateMeter(sideIndex, samples, numSamples);
}

void BqtAudioProcessor::processSaturation(float* samples, int numSamples, int sideIndex, float drive, float driveGainValue, bqt::SaturationType satType, float compensation)
{
    const auto processSample = [drive, driveGainValue, satType, compensation](float value)
    {
        value *= driveGainValue;
        value = satType == bqt::SaturationType::density
            ? bqt::densitySaturate(value, drive)
            : bqt::transformerSaturate(value, drive);
        return value * compensation;
    };

    for (int sample = 0; sample < numSamples; ++sample)
    {
        auto value = samples[sample];
        value = filters[static_cast<size_t>(sideIndex)].saturationLowGuardPre.processSample(value);
        if (satType == bqt::SaturationType::density)
            value = filters[static_cast<size_t>(sideIndex)].densityPreEmphasis.processSample(value);
        else
            value = filters[static_cast<size_t>(sideIndex)].transformerWeight.processSample(value);

        value = processSample(value);
        if (satType == bqt::SaturationType::density)
            value = filters[static_cast<size_t>(sideIndex)].densityDeEmphasis.processSample(value);
        else
            value = filters[static_cast<size_t>(sideIndex)].transformerTop.processSample(value);

        value = filters[static_cast<size_t>(sideIndex)].saturationLowGuardPost.processSample(value);
        samples[sample] = filters[static_cast<size_t>(sideIndex)].vintage.processSample(value);
    }
}

void BqtAudioProcessor::applyLatencyDelay(float* samples, int numSamples, int sideIndex)
{
    if (currentLatencySamples <= 0)
        return;

    auto& delay = dryMixDelays[static_cast<size_t>(sideIndex)];
    delay.setDelay(static_cast<float>(currentLatencySamples));

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

    const auto eqMidSide = getChoice(parameters, "eqMode") == static_cast<int>(bqt::ChannelMode::midSide);
    const auto satMidSide = getChoice(parameters, "satMode") == static_cast<int>(bqt::ChannelMode::midSide);
    const auto eqBypassed = parameters.getRawParameterValue("eqBypass")->load() > 0.5f;
    const auto satBypassed = parameters.getRawParameterValue("satBypass")->load() > 0.5f;

    inputTrimGain.setTargetValue(dbToGain(getFloat(parameters, "inputTrim")));
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
    const auto parameterId = isNonRealtime() ? "osRender" : "osRealtime";
    const auto choice = static_cast<int>(parameters.getRawParameterValue(parameterId)->load());

    if (choice <= 0)
        return -1;

    return juce::jlimit(0, numOversamplingFactors - 1, choice - 1);
}

void BqtAudioProcessor::updateLatency()
{
    const auto oversamplingIndex = getActiveOversamplingIndex();
    auto latency = 0;

    if (oversamplingIndex >= 0 && oversamplers[static_cast<size_t>(oversamplingIndex)] != nullptr)
        latency = static_cast<int>(std::round(oversamplers[static_cast<size_t>(oversamplingIndex)]->getLatencyInSamples()));

    if (latency != currentLatencySamples)
    {
        currentLatencySamples = latency;
        setLatencySamples(currentLatencySamples);
    }
}

void BqtAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto hostSampleRate = getSampleRate();
    currentSampleRate = hostSampleRate;
    updateLatency();

    const auto numSamples = buffer.getNumSamples();
    const auto bypassEnabled = parameters.getRawParameterValue("bypass")->load() > 0.5f;
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

void BqtAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void BqtAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* BqtAudioProcessor::createEditor()
{
    return new BqtAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BqtAudioProcessor();
}
