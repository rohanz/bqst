#include "BqtPresetManager.h"

#include <cmath>

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
    const char* category;
    std::initializer_list<ParameterValue> values;
};

constexpr FactoryPreset factoryPresets[] {
    { "Default", "General", {} },
    { "Clean Bax Lift", "Master",
      {
          { "aDrive", 3.0f }, { "bDrive", 3.0f },
          { "aHighGain", 1.2f }, { "bHighGain", 1.2f },
          { "aHighFreq", 5.0f }, { "bHighFreq", 5.0f },
          { "aLowGain", 1.4f },  { "bLowGain", 1.4f },
          { "aLowFreq", 3.0f },  { "bLowFreq", 3.0f },
      } },
    { "Cream Glue", "Saturation",
      {
          { "aDrive", 10.3f }, { "bDrive", 10.3f },
          { "aMix", 74.4f },  { "bMix", 74.4f },
          { "aSatType", 0.0f }, { "bSatType", 0.0f },
          { "vintage", 1.0f },
      } },
    { "Grit Console Push", "Saturation",
      {
          { "aDrive", 8.8f }, { "bDrive", 8.8f },
          { "aHighGain", 0.2f }, { "bHighGain", 0.2f },
          { "aLowGain", 0.4f }, { "bLowGain", 0.4f },
          { "aMix", 58.0f },  { "bMix", 58.0f },
          { "aSatType", 1.0f }, { "bSatType", 1.0f },
          { "aOutputTrim", -0.8f }, { "bOutputTrim", -0.8f },
      } },
    { "Cream Sheen", "Saturation",
      {
          { "aDrive", 7.4f }, { "bDrive", 7.4f },
          { "aHighFreq", 6.0f }, { "bHighFreq", 6.0f },
          { "aHighGain", 1.1f }, { "bHighGain", 1.1f },
          { "aLowGain", 0.6f }, { "bLowGain", 0.6f },
          { "aMix", 58.0f }, { "bMix", 58.0f },
          { "aSatType", 0.0f }, { "bSatType", 0.0f },
      } },
    { "Wide Air MS", "Master",
      {
          { "eqMode", 1.0f },
          { "eqLink", 0.0f },
          { "aHighGain", 0.5f }, { "bHighGain", 2.6f },
          { "aHighFreq", 5.0f }, { "bHighFreq", 7.0f },
          { "aLowFreq", 4.0f }, { "bLowFreq", 7.0f },
          { "aLowGain", 1.7f },  { "bLowGain", 0.7f },
      } },
    { "Subtle Master Polish", "Master",
      {
          { "aHighGain", 0.6f }, { "bHighGain", 0.6f },
          { "aHighFreq", 6.0f }, { "bHighFreq", 6.0f },
          { "aLowFreq", 1.0f }, { "bLowFreq", 1.0f },
          { "aLowGain", 0.8f }, { "bLowGain", 0.8f },
          { "aDrive", 3.2f },    { "bDrive", 3.2f },
          { "aMix", 45.0f },     { "bMix", 45.0f },
          { "aSatType", 0.0f },  { "bSatType", 0.0f },
      } },
};

constexpr ParameterValue defaultValues[] {
    { "eqMode", 0.0f }, { "satMode", 0.0f },
    { "inputTrim", 0.0f }, { "autoGain", 1.0f },
    { "eqLink", 1.0f }, { "satLink", 1.0f },
    { "vintage", 0.0f },
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
    // Presets and host state are untrusted input: a crafted/corrupt file can carry
    // NaN/Inf (juce::String parses "nan"/"inf") or out-of-range values, which would
    // otherwise pass straight through convertTo0to1 (jlimit leaves NaN unchanged) and
    // reach the DSP as e.g. roundToInt(NaN) -> an out-of-bounds choice index.
    if (! std::isfinite(rawValue))
        return;

    const auto& range = parameter.getNormalisableRange();
    const auto clamped = juce::jlimit(range.start, range.end, rawValue);

    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost(parameter.convertTo0to1(clamped));
    parameter.endChangeGesture();
}

bool shouldStoreInPreset(const juce::String& parameterId)
{
    static const juce::StringArray utilityParameters {
        "osRealtime",
        "osRender",
        "eqBypass",
        "satBypass",
        "bypass",
    };

    return ! utilityParameters.contains(parameterId);
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
        presets.add({ preset.name, preset.category, true, {} });

    factoryPresetCount = presets.size();

    const auto directory = getUserPresetDirectory();
    if (! directory.exists())
        return;

    juce::Array<juce::File> files;
    directory.findChildFiles(files, juce::File::findFiles, true, "*.bqstpreset");
    files.sort();

    for (const auto& file : files)
    {
        auto relativeParent = file.getParentDirectory().getRelativePathFrom(directory);
        if (relativeParent == "." || relativeParent.isEmpty())
            relativeParent = {};

        presets.add({ file.getFileNameWithoutExtension(), relativeParent.replaceCharacter(juce::File::getSeparatorChar(), '/'), false, file });
    }
}

juce::String BqtPresetManager::getPresetKey(int index) const
{
    if (! juce::isPositiveAndBelow(index, presets.size()))
        return {};

    const auto& preset = presets.getReference(index);
    return preset.factory ? "factory:" + preset.category + "/" + preset.name
                          : "user:" + preset.file.getFullPathName();
}

int BqtPresetManager::findPreset(const juce::String& key) const
{
    if (key.isEmpty())
        return -1;

    for (int i = 0; i < presets.size(); ++i)
        if (getPresetKey(i) == key)
            return i;

    return -1;
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
        // Reset to defaults first (like the factory path) so a user preset is self-contained:
        // any musical parameter the file omits returns to its default instead of keeping the
        // previously loaded preset's value. defaultValues excludes workflow state (oversampling,
        // bypass), so this does not disturb the user's session settings.
        for (const auto& value : defaultValues)
            setParameter(value.id, value.value);

        for (auto* child : xml->getChildIterator())
        {
            if (! child->hasTagName("PARAM"))
                continue;

            const auto id = child->getStringAttribute("id");
            if (shouldStoreInPreset(id))
                setParameter(id, static_cast<float>(child->getDoubleAttribute("value")));
        }

        return true;
    }

    return false;
}

bool BqtPresetManager::saveUserPreset(const juce::File& file) const
{
    if (auto xml = state.copyState().createXml())
    {
        // Stamp a format version so a future BQST can detect and migrate older presets.
        xml->setAttribute("bqstPresetVersion", 1);

        for (int i = xml->getNumChildElements(); --i >= 0;)
        {
            if (auto* child = xml->getChildElement(i))
            {
                if (child->hasTagName("PARAM") && ! shouldStoreInPreset(child->getStringAttribute("id")))
                    xml->removeChildElement(child, true);
            }
        }

        const auto target = file.withFileExtension(".bqstpreset");
        target.getParentDirectory().createDirectory();

        // Write to a sibling temp file and atomically swap it in, so an interrupted or failed
        // write can't leave a truncated/corrupt .bqstpreset where a valid one used to be.
        juce::TemporaryFile temp(target);
        if (xml->writeTo(temp.getFile()))
            return temp.overwriteTargetFileWithTemporary();
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
