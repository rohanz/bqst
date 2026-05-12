#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class BqtPresetManager final
{
public:
    struct PresetInfo
    {
        juce::String name;
        bool factory = false;
        juce::File file;
    };

    explicit BqtPresetManager(juce::AudioProcessorValueTreeState&);

    const juce::Array<PresetInfo>& getPresets() const { return presets; }
    juce::File getUserPresetDirectory() const;
    void refresh();
    bool loadPreset(int index);
    bool saveUserPreset(const juce::File& file) const;

private:
    void loadFactoryPreset(int index);
    void setParameter(const juce::String& parameterId, float rawValue);

    juce::AudioProcessorValueTreeState& state;
    juce::Array<PresetInfo> presets;
    int factoryPresetCount = 0;
};
