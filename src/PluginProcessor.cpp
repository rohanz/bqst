#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr auto sqrtHalf = 0.70710678118654752440f;
constexpr auto numOversamplingFactors = 3;
constexpr auto minRmsForMatching = 1.0e-5f;
constexpr auto vuBallisticsSeconds = 0.3f;

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
    params.push_back(std::make_unique<juce::AudioParameterChoice>("osRealtime", "Realtime Oversampling", juce::StringArray { "Off", "2x", "4x", "8x" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("osRender", "Render Oversampling", juce::StringArray { "Off", "2x", "4x", "8x" }, 2));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("inputTrim", "Input Trim", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("autoGain", "Auto Gain", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("eqBypass", "EQ Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("satBypass", "Saturation Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("eqLink", "EQ Link", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("satLink", "Saturation Link", false));
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
        params.push_back(std::make_unique<juce::AudioParameterFloat>(prefix + "Drive", label + " Drive", juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 0.0f));
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
        side.lowShelf.reset();
        side.highShelf.reset();
        side.boom.reset();
        side.vintage.reset();
    }

    for (auto& dryBuffer : dryBuffers)
        dryBuffer.setSize(1, samplesPerBlock, false, false, true);

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
        const auto lowGain = dbToGain(getFloat(parameters, prefix + "LowGain"));
        const auto highGain = dbToGain(getFloat(parameters, prefix + "HighGain"));
        const auto lowFreq = bqt::lowShelfFrequenciesHz[static_cast<size_t>(getChoice(parameters, prefix + "LowFreq"))];
        const auto highFreq = bqt::highShelfFrequenciesHz[static_cast<size_t>(getChoice(parameters, prefix + "HighFreq"))];

        const auto sideIndex = static_cast<size_t>(side);
        *filters[sideIndex].lowShelf.coefficients = *Coefficients::makeLowShelf(currentSampleRate, lowFreq, 0.707f, lowGain);
        *filters[sideIndex].highShelf.coefficients = *Coefficients::makeHighShelf(currentSampleRate, highFreq, 0.707f, highGain);
    }
}

void BqtAudioProcessor::updateSaturationToneFilters()
{
    const auto boomChoice = getChoice(parameters, "boom");
    const auto vintageEnabled = parameters.getRawParameterValue("vintage")->load() > 0.5f;
    const auto boomGainDb = boomChoice == 1 ? 0.8f : (boomChoice == 2 ? 1.5f : 0.0f);
    const auto boomFreq = boomChoice == 2 ? 95.0f : 70.0f;
    const auto vintageGainDb = vintageEnabled ? -1.25f : 0.0f;

    for (int side = 0; side < 2; ++side)
    {
        const auto sideIndex = static_cast<size_t>(side);
        *filters[sideIndex].boom.coefficients = *Coefficients::makeLowShelf(currentSampleRate, boomFreq, 0.65f, dbToGain(boomGainDb));
        *filters[sideIndex].vintage.coefficients = *Coefficients::makeHighShelf(currentSampleRate, 9000.0f, 0.55f, dbToGain(vintageGainDb));
    }
}

void BqtAudioProcessor::processEq(float* samples, int numSamples, int sideIndex)
{
    const auto filterIndex = static_cast<size_t>(sideIndex);

    for (int sample = 0; sample < numSamples; ++sample)
    {
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
    const auto drive = driveDb / 24.0f;
    const auto driveGain = dbToGain(driveDb);
    const auto mix = getFloat(parameters, prefix + "Mix") / 100.0f;
    const auto outputGain = dbToGain(getFloat(parameters, prefix + "OutputTrim"));
    const auto satType = static_cast<bqt::SaturationType>(getChoice(parameters, prefix + "SatType"));
    const auto autoGainEnabled = parameters.getRawParameterValue("autoGain")->load() > 0.5f;
    const auto compensation = autoGainEnabled ? bqt::saturationAutoGain(drive, satType) : 1.0f;
    const auto dryBufferIndex = static_cast<size_t>(sideIndex);

    if (drive > 0.0f && mix > 0.0f)
    {
        auto& dryBuffer = dryBuffers[dryBufferIndex];
        if (dryBuffer.getNumSamples() < numSamples)
            dryBuffer.setSize(1, numSamples, false, false, true);

        dryBuffer.copyFrom(0, 0, samples, numSamples);

        processSaturation(samples, numSamples, sideIndex, drive, driveGain, satType, compensation);

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

    juce::FloatVectorOperations::multiply(samples, outputGain, numSamples);
    updateMeter(sideIndex, samples, numSamples);
}

void BqtAudioProcessor::processSaturation(float* samples, int numSamples, int sideIndex, float drive, float driveGain, bqt::SaturationType satType, float compensation)
{
    const auto processSample = [drive, driveGain, satType, compensation](float value)
    {
        value *= driveGain;
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
            value = processSample(value);
            samples[sample] = filters[static_cast<size_t>(sideIndex)].vintage.processSample(value);
        }
        return;
    }

    for (int sample = 0; sample < numSamples; ++sample)
        samples[sample] = filters[static_cast<size_t>(sideIndex)].boom.processSample(samples[sample]);

    auto& oversampler = *oversamplers[static_cast<size_t>(sideIndex)][static_cast<size_t>(oversamplingIndex)];
    juce::dsp::AudioBlock<float> block(&samples, 1, static_cast<size_t>(numSamples));
    const auto upsampledBlock = oversampler.processSamplesUp(block);
    auto* upsampledSamples = upsampledBlock.getChannelPointer(0);
    const auto upsampledCount = static_cast<int>(upsampledBlock.getNumSamples());

    for (int sample = 0; sample < upsampledCount; ++sample)
        upsampledSamples[sample] = processSample(upsampledSamples[sample]);

    oversampler.processSamplesDown(block);

    for (int sample = 0; sample < numSamples; ++sample)
        samples[sample] = filters[static_cast<size_t>(sideIndex)].vintage.processSample(samples[sample]);
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
        return;

    updateFilters();
    updateSaturationToneFilters();

    const auto numSamples = buffer.getNumSamples();
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    const auto inputGain = dbToGain(getFloat(parameters, "inputTrim"));
    const auto eqMidSide = getChoice(parameters, "eqMode") == static_cast<int>(bqt::ChannelMode::midSide);
    const auto satMidSide = getChoice(parameters, "satMode") == static_cast<int>(bqt::ChannelMode::midSide);
    const auto eqBypassed = parameters.getRawParameterValue("eqBypass")->load() > 0.5f;
    const auto satBypassed = parameters.getRawParameterValue("satBypass")->load() > 0.5f;

    buffer.applyGain(inputGain);

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
