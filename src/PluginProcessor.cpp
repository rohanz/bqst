#include "PluginProcessor.h"

// The chain tests link the processor without the editor, its binary assets or a GUI, so that
// DSP behaviour can be asserted headlessly. Nothing else defines this.
#if ! BQST_HEADLESS_TESTS
 #include "PluginEditor.h"
#endif

#include <cmath>

namespace
{
// Stamped into saved state so a future format change can detect and migrate older data.
// Bump this when the on-disk parameter format changes incompatibly.
constexpr int bqstStateVersion = 1;
} // namespace

BqtAudioProcessor::BqtAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    cacheParameterPointers();
    startTimerHz(20);
}

void BqtAudioProcessor::releaseResources()
{
}


bool BqtAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void BqtAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
    {
        xml->setAttribute("bqstStateVersion", bqstStateVersion);
        copyXmlToBinary(*xml, destData);
    }
}

void BqtAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);

    // Only adopt state that is actually ours. A foreign or corrupt block that still
    // parses as XML would otherwise replace our tree with a mismatched one and silently
    // reset every parameter.
    if (xml == nullptr || ! xml->hasTagName(parameters.state.getType()))
        return;

    // State without the attribute reads as version 0. Branch here to migrate older formats if
    // bqstStateVersion is ever bumped; today every version loads directly.
    const auto loadedStateVersion = xml->getIntAttribute("bqstStateVersion", 0);
    juce::ignoreUnused(loadedStateVersion);

    // Strip any non-finite parameter value. The APVTS replaceState path applies values
    // without a finite check, and convertTo0to1/jlimit pass NaN/Inf through unchanged,
    // so a hostile/corrupt project could otherwise inject NaN into the DSP (including
    // roundToInt(NaN) -> an out-of-bounds choice index). Removing the attribute makes the
    // parameter fall back to its default.
    for (auto* child : xml->getChildIterator())
        if (child->hasTagName("PARAM") && child->hasAttribute("value")
            && ! std::isfinite(child->getDoubleAttribute("value")))
            child->removeAttribute("value");

    // Fill in any parameter the incoming state does not mention. JUCE's replaceState only applies
    // values for adapters that have a matching PARAM child; for the rest it creates a fresh child
    // and flushes the adapter's CURRENT value into it (juce_AudioProcessorValueTreeState.cpp,
    // updateParameterConnectionsToChildTrees). So an older state that predates a parameter would
    // silently inherit whatever the previous preset left there, making the same file recall
    // differently depending on what was loaded before it.
    juce::StringArray present;
    for (auto* child : xml->getChildIterator())
        if (child->hasTagName("PARAM"))
            present.add(child->getStringAttribute("id"));

    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            if (! present.contains(withId->paramID))
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                {
                    auto* child = xml->createNewChildElement("PARAM");
                    child->setAttribute("id", withId->paramID);
                    child->setAttribute("value", ranged->convertFrom0to1(ranged->getDefaultValue()));
                }

    parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* BqtAudioProcessor::createEditor()
{
#if BQST_HEADLESS_TESTS
    return nullptr;
#else
    return new BqtAudioProcessorEditor(*this);
#endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BqtAudioProcessor();
}
