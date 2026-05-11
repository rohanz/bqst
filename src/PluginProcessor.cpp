#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr auto sqrtHalf = 0.70710678118654752440f;

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

    params.push_back(std::make_unique<juce::AudioParameterChoice>("mode", "Mode", juce::StringArray { "L/R", "M/S" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("osRealtime", "Realtime Oversampling", juce::StringArray { "Off", "2x", "4x", "8x" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("osRender", "Render Oversampling", juce::StringArray { "Off", "2x", "4x", "8x" }, 2));
    params.push_back(std::make_unique<juce::AudioParameterBool>("autoGain", "Auto Gain", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));

    for (int side = 0; side < 2; ++side)
    {
        const auto prefix = sidePrefix(side);
        const juce::String label = side == 0 ? "Left/Mid" : "Right/Side";

        params.push_back(std::make_unique<juce::AudioParameterFloat>(prefix + "LowGain", label + " Low Gain", juce::NormalisableRange<float>(-6.0f, 6.0f, 0.1f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(prefix + "LowFreq", label + " Low Frequency", juce::StringArray { "74", "84", "98", "116", "131", "166", "230", "361" }, 3));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(prefix + "HighGain", label + " High Gain", juce::NormalisableRange<float>(-6.0f, 6.0f, 0.1f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(prefix + "HighFreq", label + " High Frequency", juce::StringArray { "1.6k", "1.8k", "2.1k", "2.5k", "3.4k", "4.8k", "7.1k", "18k" }, 4));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(prefix + "Drive", label + " Drive", juce::NormalisableRange<float>(0.0f, 10.0f, 0.01f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(prefix + "SatType", label + " Saturation Type", juce::StringArray { "Density", "Transformer" }, 0));
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
        side.lowShelf.reset();
        side.highShelf.reset();
    }

    updateFilters();
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
        const auto lowGain = dbToGain(getFloat(parameters, prefix + "LowGain"));
        const auto highGain = dbToGain(getFloat(parameters, prefix + "HighGain"));
        const auto lowFreq = bqt::lowShelfFrequenciesHz[static_cast<size_t>(getChoice(parameters, prefix + "LowFreq"))];
        const auto highFreq = bqt::highShelfFrequenciesHz[static_cast<size_t>(getChoice(parameters, prefix + "HighFreq"))];

        const auto sideIndex = static_cast<size_t>(side);
        *filters[sideIndex].lowShelf.coefficients = *Coefficients::makeLowShelf(currentSampleRate, lowFreq, 0.707f, lowGain);
        *filters[sideIndex].highShelf.coefficients = *Coefficients::makeHighShelf(currentSampleRate, highFreq, 0.707f, highGain);
    }
}

void BqtAudioProcessor::processSide(float* samples, int numSamples, int sideIndex)
{
    const auto prefix = sidePrefix(sideIndex);
    const auto drive = getFloat(parameters, prefix + "Drive") / 10.0f;
    const auto outputGain = dbToGain(getFloat(parameters, prefix + "OutputTrim"));
    const auto satType = static_cast<bqt::SaturationType>(getChoice(parameters, prefix + "SatType"));
    const auto autoGainEnabled = parameters.getRawParameterValue("autoGain")->load() > 0.5f;
    const auto compensation = autoGainEnabled ? bqt::saturationAutoGain(drive, satType) : 1.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        auto value = samples[sample];
        const auto filterIndex = static_cast<size_t>(sideIndex);
        value = filters[filterIndex].lowShelf.processSample(value);
        value = filters[filterIndex].highShelf.processSample(value);

        if (drive > 0.0f)
        {
            value = satType == bqt::SaturationType::density
                ? bqt::densitySaturate(value, drive)
                : bqt::transformerSaturate(value, drive);
            value *= compensation;
        }

        samples[sample] = value * outputGain;
    }
}

void BqtAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (parameters.getRawParameterValue("bypass")->load() > 0.5f)
        return;

    updateFilters();

    const auto numSamples = buffer.getNumSamples();
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    const auto midSide = getChoice(parameters, "mode") == static_cast<int>(bqt::ChannelMode::midSide);

    if (midSide)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto mid = (left[i] + right[i]) * sqrtHalf;
            const auto side = (left[i] - right[i]) * sqrtHalf;
            left[i] = mid;
            right[i] = side;
        }
    }

    processSide(left, numSamples, 0);
    processSide(right, numSamples, 1);

    if (midSide)
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
