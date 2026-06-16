#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class BqtPresetManager final
{
public:
    struct PresetInfo
    {
        juce::String name;
        juce::String category;
        bool factory = false;
        juce::File file;
    };

    explicit BqtPresetManager(juce::AudioProcessorValueTreeState&);

    const juce::Array<PresetInfo>& getPresets() const { return presets; }
    juce::File getUserPresetDirectory() const;
    void refresh();
    bool loadPreset(int index);
    bool saveUserPreset(const juce::File& file) const;

    // Stable identity for a preset, so selection survives the list being rebuilt (user
    // presets are sorted and shift index when others are added/removed on disk).
    juce::String getPresetKey(int index) const;
    int findPreset(const juce::String& key) const;

private:
    void loadFactoryPreset(int index);
    void setParameter(const juce::String& parameterId, float rawValue);

    juce::AudioProcessorValueTreeState& state;
    juce::Array<PresetInfo> presets;
    int factoryPresetCount = 0;
};
