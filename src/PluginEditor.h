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
        juce::Slider mix;
        juce::Slider outputTrim;

        std::unique_ptr<SliderAttachment> lowGainAttachment;
        std::unique_ptr<ComboBoxAttachment> lowFreqAttachment;
        std::unique_ptr<SliderAttachment> highGainAttachment;
        std::unique_ptr<ComboBoxAttachment> highFreqAttachment;
        std::unique_ptr<SliderAttachment> driveAttachment;
        std::unique_ptr<ComboBoxAttachment> satTypeAttachment;
        std::unique_ptr<SliderAttachment> mixAttachment;
        std::unique_ptr<SliderAttachment> outputTrimAttachment;
    };

    class VuMeter final : public juce::Component, private juce::Timer
    {
    public:
        VuMeter(BqtAudioProcessor& processor, int sideIndex);
        void paint(juce::Graphics& g) override;

    private:
        void timerCallback() override;

        BqtAudioProcessor& audioProcessor;
        int side = 0;
        float level = 0.0f;
    };

    void configureSlider(juce::Slider& slider);
    void configureCombo(juce::ComboBox& combo);
    void configureSide(SideControls& controls, int sideIndex);

    BqtAudioProcessor& audioProcessor;
    juce::ComboBox eqMode;
    juce::ComboBox satMode;
    juce::ComboBox osRealtime;
    juce::ComboBox osRender;
    juce::Slider inputTrim;
    juce::ComboBox boom;
    juce::ToggleButton autoGain;
    juce::ToggleButton eqBypass;
    juce::ToggleButton satBypass;
    juce::ToggleButton eqLink;
    juce::ToggleButton satLink;
    juce::ToggleButton vintage;
    juce::ToggleButton bypass;
    std::array<SideControls, 2> sideControls;
    VuMeter meterA;
    VuMeter meterB;

    std::unique_ptr<ComboBoxAttachment> eqModeAttachment;
    std::unique_ptr<ComboBoxAttachment> satModeAttachment;
    std::unique_ptr<ComboBoxAttachment> osRealtimeAttachment;
    std::unique_ptr<ComboBoxAttachment> osRenderAttachment;
    std::unique_ptr<SliderAttachment> inputTrimAttachment;
    std::unique_ptr<ComboBoxAttachment> boomAttachment;
    std::unique_ptr<ButtonAttachment> autoGainAttachment;
    std::unique_ptr<ButtonAttachment> eqBypassAttachment;
    std::unique_ptr<ButtonAttachment> satBypassAttachment;
    std::unique_ptr<ButtonAttachment> eqLinkAttachment;
    std::unique_ptr<ButtonAttachment> satLinkAttachment;
    std::unique_ptr<ButtonAttachment> vintageAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    juce::TooltipWindow tooltipWindow { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BqtAudioProcessorEditor)
};
