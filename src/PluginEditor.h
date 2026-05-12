#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "BqtEditorWidgets.h"
#include "BqtPresetManager.h"
#include "PluginProcessor.h"

#include <vector>

class BqtAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                      private juce::Timer,
                                      private juce::Slider::Listener,
                                      private juce::KeyListener
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

    class RackComponent final : public juce::Component
    {
    public:
        explicit RackComponent(BqtAudioProcessorEditor& editorToUse) : editor(editorToUse) {}
        void paint(juce::Graphics& g) override { editor.paintRack(g); }
        void paintOverChildren(juce::Graphics& g) override;
        void setBypassed(bool shouldBeBypassed);
        bool isBypassed() const { return bypassed; }

    private:
        BqtAudioProcessorEditor& editor;
        bool bypassed = false;
    };

    void configureSlider(juce::Slider& slider);
    void configureCombo(juce::ComboBox& combo);
    void configureLabel(juce::Label& label, const juce::String& text, juce::Justification justification = juce::Justification::centred);
    void configureSide(SideControls& controls, int sideIndex);
    void paintRack(juce::Graphics& g);
    void requestRackBypassVisualState(bool shouldBeBypassed);
    void timerCallback() override;
    bool shouldMirrorLinkedControls(const char* linkParameterId) const;
    void beginLinkedMirrorGestureFor(juce::Slider& slider);
    void beginMirroredParameterGesture(const juce::String& parameterId);
    void endLinkedMirrorGestures();
    void mirrorLinkedSteppedFrequencyVisual(juce::Slider& slider);
    void commitMirroredSteppedFrequency(juce::Slider& slider);
    void commitParameterGesture(const juce::String& parameterId, float plainValue);
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void sliderValueChanged(juce::Slider* slider) override;
    void sliderDragStarted(juce::Slider* slider) override;
    void sliderDragEnded(juce::Slider* slider) override;
    void mouseDrag(const juce::MouseEvent& event) override;
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
    void beginUndoableEdit();
    void finishUndoableEdit();
    void cancelUndoableEdit();
    bool undoLastPluginEdit();
    bool redoLastPluginEdit();
    void restorePluginEditState(const std::vector<std::pair<juce::String, float>>& snapshot);
    std::vector<std::pair<juce::String, float>> capturePluginEditState() const;
    void refreshPresetMenu();
    void showPresetMenu();
    void loadPreset(int index);
    void selectRelativePreset(int offset);
    void saveUserPreset();
    void updatePresetButtonText();

    BqtHardwareLookAndFeel hardwareLookAndFeel;
    BqtAudioProcessor& audioProcessor;
    BqtPresetManager presetManager;
    RackComponent rackComponent;
    juce::TextButton presetPrevious;
    juce::TextButton presetMenuButton;
    juce::TextButton presetNext;
    juce::TextButton presetSave;
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
    juce::ComboBox sizeSelect;
    std::array<SideControls, 2> sideControls;
    BqtVuMeter meterA;
    BqtVuMeter meterB;

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
    BqtReadoutBubble readoutBubble;
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
    int selectedPresetIndex = 0;
    juce::Array<juce::RangedAudioParameter*> activeMirroredGestureParameters;
    std::vector<std::pair<juce::String, float>> pendingUndoState;
    std::vector<std::vector<std::pair<juce::String, float>>> undoStack;
    std::vector<std::vector<std::pair<juce::String, float>>> redoStack;
    bool restoringPluginEditState = false;
    bool undoCaptureActive = false;
    std::unique_ptr<juce::FileChooser> presetFileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BqtAudioProcessorEditor)
};
