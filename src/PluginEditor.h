#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

class BqtAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                      private juce::Timer,
                                      private juce::Slider::Listener
{
public:
    explicit BqtAudioProcessorEditor(BqtAudioProcessor&);
    ~BqtAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct SideControls
    {
        juce::Label eqSectionLabel;
        juce::Label satSectionLabel;
        juce::Label lowGainLabel;
        juce::Slider lowGain;
        juce::Label lowFreqLabel;
        juce::Slider lowFreq;
        juce::Label highGainLabel;
        juce::Slider highGain;
        juce::Label highFreqLabel;
        juce::Slider highFreq;
        juce::Label driveLabel;
        juce::Slider drive;
        juce::Label satTypeLabel;
        juce::ComboBox satType;
        juce::TextButton satTypeButton;
        juce::Label mixLabel;
        juce::Slider mix;
        juce::Label outputTrimLabel;
        juce::Slider outputTrim;

        std::unique_ptr<SliderAttachment> lowGainAttachment;
        std::unique_ptr<SliderAttachment> lowFreqAttachment;
        std::unique_ptr<SliderAttachment> highGainAttachment;
        std::unique_ptr<SliderAttachment> highFreqAttachment;
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
        float targetLevel = 0.0f;
        float displayedLevel = 0.0f;
    };

    class HardwareLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        HardwareLookAndFeel();
        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                              float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) override;
        void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                          int buttonW, int buttonH, juce::ComboBox& box) override;
        void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
        void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
        void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                  bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
        void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) override;
        juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                              juce::Rectangle<int> parentArea) override;
        void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override;
    };

    class ReadoutBubble final : public juce::Component
    {
    public:
        void setText(juce::String newText);
        void paint(juce::Graphics& g) override;

    private:
        juce::String text;
    };

    void configureSlider(juce::Slider& slider);
    void configureCombo(juce::ComboBox& combo);
    void configureLabel(juce::Label& label, const juce::String& text, juce::Justification justification = juce::Justification::centred);
    void configureSide(SideControls& controls, int sideIndex);
    void timerCallback() override;
    void sliderValueChanged(juce::Slider* slider) override;
    void sliderDragStarted(juce::Slider* slider) override;
    void sliderDragEnded(juce::Slider* slider) override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void updateLinkedControlStates();
    void updateDynamicTooltips();
    void updateDragValueReadout(juce::Slider& slider);
    void hideDragValueReadout();
    void syncHoverTargetsFromMouse();
    void updateHoverValueReadout();
    void hideHoverValueReadout();
    void updateTopBarHelp();
    void hideTopBarHelp();
    void setTopBarHelp(juce::Component& component, const juce::String& text);
    void showReadout(juce::Component& target, const juce::String& text);
    void hideReadout();

    HardwareLookAndFeel hardwareLookAndFeel;
    BqtAudioProcessor& audioProcessor;
    juce::ComboBox eqMode;
    juce::ComboBox satMode;
    juce::ComboBox osRealtime;
    juce::ComboBox osRender;
    juce::Label inputTrimLabel;
    juce::Slider inputTrim;
    juce::ToggleButton autoGain;
    juce::ToggleButton eqBypass;
    juce::ToggleButton satBypass;
    juce::ToggleButton eqLink;
    juce::ToggleButton satLink;
    juce::ToggleButton vintage;
    juce::ToggleButton bypass;
    juce::ToggleButton sizeToggle;
    std::array<SideControls, 2> sideControls;
    VuMeter meterA;
    VuMeter meterB;

    std::unique_ptr<ComboBoxAttachment> eqModeAttachment;
    std::unique_ptr<ComboBoxAttachment> satModeAttachment;
    std::unique_ptr<ComboBoxAttachment> osRealtimeAttachment;
    std::unique_ptr<ComboBoxAttachment> osRenderAttachment;
    std::unique_ptr<SliderAttachment> inputTrimAttachment;
    std::unique_ptr<ButtonAttachment> autoGainAttachment;
    std::unique_ptr<ButtonAttachment> eqBypassAttachment;
    std::unique_ptr<ButtonAttachment> satBypassAttachment;
    std::unique_ptr<ButtonAttachment> eqLinkAttachment;
    std::unique_ptr<ButtonAttachment> satLinkAttachment;
    std::unique_ptr<ButtonAttachment> vintageAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    juce::TooltipWindow tooltipWindow { this, 700 };
    ReadoutBubble readoutBubble;
    bool isMirroringLinkedControl = false;
    juce::Slider* activeReadoutSlider = nullptr;
    juce::Slider* hoveredReadoutSlider = nullptr;
    juce::uint32 hoverReadoutStartMs = 0;
    bool hoverReadoutVisible = false;
    juce::Component* hoveredHelpComponent = nullptr;
    juce::String hoveredHelpText;
    juce::uint32 helpHoverStartMs = 0;
    bool helpVisible = false;
    double previousInputTrimForCompensation = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BqtAudioProcessorEditor)
};
