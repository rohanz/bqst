#include "PluginProcessor.h"

namespace
{
juce::String sidePrefix(int sideIndex)
{
    return sideIndex == 0 ? "a" : "b";
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout BqtAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    const auto hiddenBool = juce::AudioParameterBoolAttributes().withAutomatable(false);

    params.push_back(std::make_unique<juce::AudioParameterChoice>("eqMode", "EQ Mode", juce::StringArray { "L/R", "M/S" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("satMode", "Sat Mode", juce::StringArray { "L/R", "M/S" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("osRealtime", "Realtime OS", juce::StringArray { "Off", "2x", "4x", "8x" }, 1));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("osRender", "Render OS", juce::StringArray { "Off", "2x", "4x", "8x" }, 2));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("inputTrim", "Input", juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
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
