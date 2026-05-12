#include "BqtPresetManager.h"

namespace
{
struct ParameterValue
{
    const char* id;
    float value;
};

struct FactoryPreset
{
    const char* name;
    std::initializer_list<ParameterValue> values;
};

constexpr FactoryPreset factoryPresets[] {
    { "Default", {} },
    { "Clean Bax Lift",
      {
          { "aHighGain", 1.2f }, { "bHighGain", 1.2f },
          { "aHighFreq", 5.0f }, { "bHighFreq", 5.0f },
          { "aLowGain", 0.7f },  { "bLowGain", 0.7f },
          { "aLowFreq", 3.0f },  { "bLowFreq", 3.0f },
      } },
    { "Cream Glue",
      {
          { "aDrive", 5.0f }, { "bDrive", 5.0f },
          { "aMix", 72.0f },  { "bMix", 72.0f },
          { "aSatType", 0.0f }, { "bSatType", 0.0f },
          { "vintage", 1.0f },
      } },
    { "Grit Console Push",
      {
          { "aDrive", 7.5f }, { "bDrive", 7.5f },
          { "aMix", 58.0f },  { "bMix", 58.0f },
          { "aSatType", 1.0f }, { "bSatType", 1.0f },
          { "aOutputTrim", -0.8f }, { "bOutputTrim", -0.8f },
      } },
    { "Wide Air MS",
      {
          { "eqMode", 1.0f },
          { "aHighGain", 0.4f }, { "bHighGain", 1.5f },
          { "aHighFreq", 5.0f }, { "bHighFreq", 7.0f },
          { "aLowGain", 0.2f },  { "bLowGain", -0.3f },
      } },
    { "Subtle Master Polish",
      {
          { "aHighGain", 0.6f }, { "bHighGain", 0.6f },
          { "aHighFreq", 6.0f }, { "bHighFreq", 6.0f },
          { "aDrive", 3.2f },    { "bDrive", 3.2f },
          { "aMix", 45.0f },     { "bMix", 45.0f },
          { "aSatType", 0.0f },  { "bSatType", 0.0f },
      } },
};

constexpr ParameterValue defaultValues[] {
    { "eqMode", 0.0f }, { "satMode", 0.0f },
    { "osRealtime", 1.0f }, { "osRender", 2.0f },
    { "inputTrim", 0.0f }, { "autoGain", 1.0f },
    { "eqBypass", 0.0f }, { "satBypass", 0.0f },
    { "eqLink", 1.0f }, { "satLink", 1.0f },
    { "bypass", 0.0f }, { "vintage", 0.0f },
    { "aLowGain", 0.0f }, { "aLowFreq", 3.0f },
    { "aHighGain", 0.0f }, { "aHighFreq", 4.0f },
    { "aDrive", 0.0f }, { "aSatType", 0.0f },
    { "aMix", 100.0f }, { "aOutputTrim", 0.0f },
    { "bLowGain", 0.0f }, { "bLowFreq", 3.0f },
    { "bHighGain", 0.0f }, { "bHighFreq", 4.0f },
    { "bDrive", 0.0f }, { "bSatType", 0.0f },
    { "bMix", 100.0f }, { "bOutputTrim", 0.0f },
};

void setValueNotifyingHost(juce::RangedAudioParameter& parameter, float rawValue)
{
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost(parameter.convertTo0to1(rawValue));
    parameter.endChangeGesture();
}
} // namespace

BqtPresetManager::BqtPresetManager(juce::AudioProcessorValueTreeState& stateToUse)
    : state(stateToUse)
{
    refresh();
}

juce::File BqtPresetManager::getUserPresetDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("BQST")
        .getChildFile("Presets");
}

void BqtPresetManager::refresh()
{
    presets.clear();

    for (const auto& preset : factoryPresets)
        presets.add({ preset.name, true, {} });

    factoryPresetCount = presets.size();

    const auto directory = getUserPresetDirectory();
    if (! directory.exists())
        return;

    juce::Array<juce::File> files;
    directory.findChildFiles(files, juce::File::findFiles, false, "*.bqstpreset");
    files.sort();

    for (const auto& file : files)
        presets.add({ file.getFileNameWithoutExtension(), false, file });
}

bool BqtPresetManager::loadPreset(int index)
{
    if (! juce::isPositiveAndBelow(index, presets.size()))
        return false;

    if (presets.getReference(index).factory)
    {
        loadFactoryPreset(index);
        return true;
    }

    if (auto xml = juce::parseXML(presets.getReference(index).file))
    {
        state.replaceState(juce::ValueTree::fromXml(*xml));
        return true;
    }

    return false;
}

bool BqtPresetManager::saveUserPreset(const juce::File& file) const
{
    if (auto xml = state.copyState().createXml())
    {
        const auto target = file.withFileExtension(".bqstpreset");
        target.getParentDirectory().createDirectory();
        return xml->writeTo(target);
    }

    return false;
}

void BqtPresetManager::loadFactoryPreset(int index)
{
    for (const auto& value : defaultValues)
        setParameter(value.id, value.value);

    const auto factoryIndex = juce::jlimit(0, factoryPresetCount - 1, index);
    for (const auto& value : factoryPresets[factoryIndex].values)
        setParameter(value.id, value.value);
}

void BqtPresetManager::setParameter(const juce::String& parameterId, float rawValue)
{
    if (auto* parameter = state.getParameter(parameterId))
        setValueNotifyingHost(*parameter, rawValue);
}
