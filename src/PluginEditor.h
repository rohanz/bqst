#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

class BqtAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit BqtAudioProcessorEditor(BqtAudioProcessor&);
    ~BqtAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct SideControls
    {
        juce::Slider lowGain;
        juce::ComboBox lowFreq;
        juce::Slider highGain;
        juce::ComboBox highFreq;
        juce::Slider drive;
        juce::ComboBox satType;
        juce::Slider outputTrim;

        std::unique_ptr<SliderAttachment> lowGainAttachment;
        std::unique_ptr<ComboBoxAttachment> lowFreqAttachment;
        std::unique_ptr<SliderAttachment> highGainAttachment;
        std::unique_ptr<ComboBoxAttachment> highFreqAttachment;
        std::unique_ptr<SliderAttachment> driveAttachment;
        std::unique_ptr<ComboBoxAttachment> satTypeAttachment;
        std::unique_ptr<SliderAttachment> outputTrimAttachment;
    };

    void configureSlider(juce::Slider& slider);
    void configureCombo(juce::ComboBox& combo);
    void configureSide(SideControls& controls, int sideIndex);

    BqtAudioProcessor& audioProcessor;
    juce::ComboBox mode;
    juce::ComboBox osRealtime;
    juce::ComboBox osRender;
    juce::ToggleButton autoGain;
    juce::ToggleButton bypass;
    std::array<SideControls, 2> sideControls;

    std::unique_ptr<ComboBoxAttachment> modeAttachment;
    std::unique_ptr<ComboBoxAttachment> osRealtimeAttachment;
    std::unique_ptr<ComboBoxAttachment> osRenderAttachment;
    std::unique_ptr<ButtonAttachment> autoGainAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BqtAudioProcessorEditor)
};
