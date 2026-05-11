#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr auto sqrtHalf = 0.70710678118654752440f;
constexpr auto numOversamplingFactors = 3;
constexpr auto minRmsForMatching = 1.0e-5f;
constexpr auto vuBallisticsSeconds = 0.3f;
constexpr auto parameterSmoothingSeconds = 0.02;
constexpr auto baxShelfQ = 0.38f;
constexpr auto saturationDriveScale = 0.36f;

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

    params.push_back(std::make_unique<juce::AudioParameterChoice>("eqMode", "EQ Mode", juce::StringArray { "L/R", "M/S" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("satMode", "Saturation Mode", juce::StringArray { "L/R", "M/S" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("osRealtime", "Realtime Oversampling", juce::StringArray { "Off", "2x", "4x", "8x" }, 1));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("osRender", "Render Oversampling", juce::StringArray { "Off", "2x", "4x", "8x" }, 2));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("inputTrim", "Input Trim", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("autoGain", "Auto Gain", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("eqBypass", "EQ Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("satBypass", "Saturation Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("eqLink", "EQ Link", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("satLink", "Saturation Link", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("boom", "Boom", juce::StringArray { "Off", "A", "B" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterBool>("vintage", "Vintage", false));

    for (int side = 0; side < 2; ++side)
    {
        const auto prefix = sidePrefix(side);
        const juce::String label = side == 0 ? "Left/Mid" : "Right/Side";

        params.push_back(std::make_unique<juce::AudioParameterFloat>(prefix + "LowGain", label + " Low Gain", juce::NormalisableRange<float>(-6.0f, 6.0f, 0.1f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(prefix + "LowFreq", label + " Low Frequency", juce::StringArray { "74", "84", "98", "116", "131", "166", "230", "361" }, 3));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(prefix + "HighGain", label + " High Gain", juce::NormalisableRange<float>(-6.0f, 6.0f, 0.1f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(prefix + "HighFreq", label + " High Frequency", juce::StringArray { "1.6k", "1.8k", "2.1k", "2.5k", "3.4k", "4.8k", "7.1k", "18k" }, 4));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(prefix + "Drive", label + " Drive", juce::NormalisableRange<float>(0.0f, 18.0f, 0.1f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(prefix + "SatType", label + " Saturation Type", juce::StringArray { "Density", "Transformer" }, 0));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(prefix + "Mix", label + " Saturation Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(prefix + "OutputTrim", label + " Output Trim", juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
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
        side.boom.prepare(spec);
        side.vintage.prepare(spec);
        side.densityPreEmphasis.prepare(spec);
        side.densityDeEmphasis.prepare(spec);
        side.saturationLowGuardPre.prepare(spec);
        side.saturationLowGuardPost.prepare(spec);
        side.transformerWeight.prepare(spec);
        side.transformerTop.prepare(spec);
        side.lowShelf.reset();
        side.highShelf.reset();
        side.boom.reset();
        side.vintage.reset();
        side.densityPreEmphasis.reset();
        side.densityDeEmphasis.reset();
        side.saturationLowGuardPre.reset();
        side.saturationLowGuardPost.reset();
        side.transformerWeight.reset();
        side.transformerTop.reset();
    }

    for (auto& dryBuffer : dryBuffers)
        dryBuffer.setSize(1, samplesPerBlock, false, false, true);

    for (auto& delay : dryMixDelays)
    {
        delay.prepare(spec);
        delay.setMaximumDelayInSamples(4096);
        delay.reset();
    }

    inputTrimGain.reset(sampleRate, parameterSmoothingSeconds);
    inputTrimGain.setCurrentAndTargetValue(1.0f);

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

    for (auto& sideOversamplers : oversamplers)
    {
        for (int factorIndex = 0; factorIndex < numOversamplingFactors; ++factorIndex)
        {
            sideOversamplers[static_cast<size_t>(factorIndex)] = std::make_unique<juce::dsp::Oversampling<float>>(
                1,
                static_cast<size_t>(factorIndex + 1),
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                true,
                true);
            sideOversamplers[static_cast<size_t>(factorIndex)]->initProcessing(static_cast<size_t>(samplesPerBlock));
            sideOversamplers[static_cast<size_t>(factorIndex)]->reset();
        }
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
        const auto linkedSide = parameters.getRawParameterValue("eqLink")->load() > 0.5f ? 0 : side;
        const auto prefix = sidePrefix(linkedSide);
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
    const auto boomChoice = getChoice(parameters, "boom");
    const auto vintageEnabled = parameters.getRawParameterValue("vintage")->load() > 0.5f;
    const auto boomGainDb = boomChoice == 1 ? 1.4f : (boomChoice == 2 ? 1.4f : 0.0f);
    const auto boomFreq = boomChoice == 2 ? 210.0f : 55.0f;
    const auto vintageGainDb = vintageEnabled ? -4.2f : 0.0f;

    for (int side = 0; side < 2; ++side)
    {
        const auto sideIndex = static_cast<size_t>(side);
        *filters[sideIndex].boom.coefficients = *Coefficients::makeLowShelf(currentSampleRate, boomFreq, 0.50f, dbToGain(boomGainDb));
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
    const auto linkedSide = parameters.getRawParameterValue("eqLink")->load() > 0.5f ? 0 : sideIndex;
    const auto prefix = sidePrefix(linkedSide);
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
    const auto linkedSideIndex = parameters.getRawParameterValue("satLink")->load() > 0.5f ? 0 : sideIndex;
    const auto prefix = sidePrefix(linkedSideIndex);
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
        auto delayedDry = dryBuffer.getWritePointer(0);
        const auto oversamplingIndex = getActiveOversamplingIndex();
        const auto dryDelaySamples = oversamplingIndex >= 0 && oversamplers[0][static_cast<size_t>(oversamplingIndex)] != nullptr
            ? static_cast<int>(std::round(oversamplers[0][static_cast<size_t>(oversamplingIndex)]->getLatencyInSamples()))
            : 0;

        auto& dryDelay = dryMixDelays[dryBufferIndex];
        dryDelay.setDelay(static_cast<float>(dryDelaySamples));
        for (int sample = 0; sample < numSamples; ++sample)
        {
            dryDelay.pushSample(0, dry[sample]);
            delayedDry[sample] = dryDelay.popSample(0);
        }

        if (autoGainEnabled)
        {
            auto dryEnergy = 0.0f;
            auto wetEnergy = 0.0f;

            for (int sample = 0; sample < numSamples; ++sample)
            {
                dryEnergy += delayedDry[sample] * delayedDry[sample];
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
            samples[sample] = delayedDry[sample] + (samples[sample] - delayedDry[sample]) * mix;
    }
    else
    {
        applyLatencyDelay(samples, numSamples, sideIndex);
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

    const auto oversamplingIndex = getActiveOversamplingIndex();

    if (oversamplingIndex < 0)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            auto value = filters[static_cast<size_t>(sideIndex)].boom.processSample(samples[sample]);
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
        return;
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        auto value = filters[static_cast<size_t>(sideIndex)].boom.processSample(samples[sample]);
        value = filters[static_cast<size_t>(sideIndex)].saturationLowGuardPre.processSample(value);
        if (satType == bqt::SaturationType::density)
            value = filters[static_cast<size_t>(sideIndex)].densityPreEmphasis.processSample(value);
        else
            value = filters[static_cast<size_t>(sideIndex)].transformerWeight.processSample(value);
        samples[sample] = value;
    }

    auto& oversampler = *oversamplers[static_cast<size_t>(sideIndex)][static_cast<size_t>(oversamplingIndex)];
    juce::dsp::AudioBlock<float> block(&samples, 1, static_cast<size_t>(numSamples));
    const auto upsampledBlock = oversampler.processSamplesUp(block);
    auto* upsampledSamples = upsampledBlock.getChannelPointer(0);
    const auto upsampledCount = static_cast<int>(upsampledBlock.getNumSamples());

    for (int sample = 0; sample < upsampledCount; ++sample)
        upsampledSamples[sample] = processSample(upsampledSamples[sample]);

    oversampler.processSamplesDown(block);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        auto value = samples[sample];
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
    auto energy = 0.0f;
    for (int sample = 0; sample < numSamples; ++sample)
        energy += samples[sample] * samples[sample];

    const auto blockRms = std::sqrt(energy / static_cast<float>(numSamples));
    const auto blockSeconds = static_cast<float>(numSamples / currentSampleRate);
    const auto release = std::exp(-blockSeconds / vuBallisticsSeconds);
    const auto attack = 1.0f - release;

    const auto index = static_cast<size_t>(sideIndex);
    meterRms[index] = meterRms[index] * release + blockRms * attack;
    meterLevels[index].store(meterRms[index]);
}

float BqtAudioProcessor::getMeterLevel(int sideIndex) const
{
    return meterLevels[static_cast<size_t>(juce::jlimit(0, 1, sideIndex))].load();
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

    if (oversamplingIndex >= 0 && oversamplers[0][static_cast<size_t>(oversamplingIndex)] != nullptr)
        latency = static_cast<int>(std::round(oversamplers[0][static_cast<size_t>(oversamplingIndex)]->getLatencyInSamples()));

    if (latency != currentLatencySamples)
    {
        currentLatencySamples = latency;
        setLatencySamples(currentLatencySamples);
    }
}

void BqtAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    updateLatency();

    if (parameters.getRawParameterValue("bypass")->load() > 0.5f)
    {
        applyLatencyDelay(buffer.getWritePointer(0), buffer.getNumSamples(), 0);
        applyLatencyDelay(buffer.getWritePointer(1), buffer.getNumSamples(), 1);
        return;
    }

    updateFilters();
    updateSaturationToneFilters();

    const auto numSamples = buffer.getNumSamples();
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
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
        applyLatencyDelay(left, numSamples, 0);
        applyLatencyDelay(right, numSamples, 1);
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
