#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "BqtEditorLogic.h"
#include "BqtEditorSize.h"
#include "BqtEditorWidgets.h"
#include "BqtVuMeter.h"
#include "BqtPresetManager.h"
#include "PluginProcessor.h"

#include <array>
#include <functional>
#include <vector>

// Stops a resize from growing the editor past the bottom of the screen it is on, so the corner
// grip stays reachable. It only limits growth: a window that is already too tall (a project saved
// on a larger screen) is left alone and can still be made smaller.
class BqtScreenFitConstrainer final : public juce::ComponentBoundsConstrainer
{
public:
    explicit BqtScreenFitConstrainer(juce::Component& editorToFit) : editor(editorToFit) {}

    void checkBounds(juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous,
                     const juce::Rectangle<int>& limits, bool top, bool left, bool bottom, bool right) override;

private:
    juce::Component& editor;
};

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

    // For tests: the clock that moves the native needles.
    const bqst::vu::NeedleClock& needleClockForTest() const { return needleClock; }

private:
    BqtScreenFitConstrainer screenFit { *this };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct SideControls
    {
        juce::Slider lowGain;
        juce::Slider lowFreq;
        juce::Slider highGain;
        juce::Slider highFreq;
        juce::Slider drive;
        // Never shown: it only holds the attachment for this side's sat type parameter, which
        // the shared satTypeButton reads its toggle state from.
        juce::ComboBox satType;
        juce::Slider mix;
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

    // Covers the whole editor while open, without dimming it: a click anywhere (on the card or
    // beside it) or Escape closes it. Read as a dialog. It paints only the card, which it reports
    // as its opaque shape, so the VU needle layers leave out just the card (BqtVuNeedleLayer.h).
    class AboutPanel final : public juce::Component, public bqst::vu::OpaqueCover
    {
    public:
        AboutPanel();
        void paint(juce::Graphics& g) override;
        bqst::vu::OpaqueCoverShape opaqueShape() const override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;
        bool keyPressed(const juce::KeyPress& key) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
        juce::Rectangle<int> cardBounds() const;

        std::function<void()> onClose;
    };

    class RackComponent final : public juce::Component
    {
    public:
        explicit RackComponent(BqtAudioProcessorEditor& editorToUse) : editor(editorToUse)
        {
            setOpaque(true);
        }

        void paint(juce::Graphics& g) override;
        // The bypass dimming is painted here, in the same pass as the rack's children (meters
        // included), so no part of the rack can show a different bypass state than the rest.
        void paintOverChildren(juce::Graphics& g) override;
        void setBypassed(bool shouldBeBypassed);
        bool isBypassed() const { return bypassed; }

    private:
        BqtAudioProcessorEditor& editor;
        bool bypassed = false;

        // The faceplate is static (only its size/scale changes), but it is expensive to draw
        // (panel artwork and lettering). Cache it to an offscreen image and only rebuild on a size/scale
        // change, so repaints (undo/redo, bypass toggles) just blit instead of redrawing.
        juce::Image faceplateCache;
        int faceplateCacheWidth = 0;
        int faceplateCacheHeight = 0;
        float faceplateCacheScale = 0.0f;
    };

    void configureSlider(juce::Slider& slider);
    void configureCombo(juce::ComboBox& combo);
    void configureLabel(juce::Label& label, const juce::String& text, juce::Justification justification = juce::Justification::centred);
    void configureSide(SideControls& controls, int sideIndex);
    void paintRack(juce::Graphics& g);
    void timerCallback() override;
    bool isGroupLinked(bqt::editor::LinkGroup group) const;
    bool shouldMirrorToOtherSide(bqt::editor::LinkGroup group) const;
    void updateLinkedAttachments();
    void unlinkGroupFromUi(bqt::editor::LinkGroup group);
    void beginLinkedMirrorGestureFor(juce::Slider& slider);
    void beginMirroredParameterGesture(const juce::String& parameterId);
    void endLinkedMirrorGestures();
    void mirrorLinkedSteppedFrequencyVisual(juce::Slider& slider);
    void commitMirroredSteppedFrequency(juce::Slider& slider);
    void commitParameterGesture(const juce::String& parameterId, float plainValue);
    void toggleSatTypeBothSides(juce::Button& button);
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
    void restorePluginEditState(const bqt::editor::ParameterSnapshot& snapshot);
    bqt::editor::ParameterSnapshot capturePluginEditState() const;
    void refreshPresetMenu();
    void showPresetMenu();
    bool loadPreset(int index);
    void selectRelativePreset(int offset);
    void saveUserPreset();
    void updatePresetButtonText();
    void showAboutPanel();
    void hideAboutPanel();
    bool barHelpBlocked() const;
    void setFocusRingsVisible(bool shouldShow);

    BqtHardwareLookAndFeel hardwareLookAndFeel;
    BqtAudioProcessor& audioProcessor;
    BqtPresetManager presetManager;
    RackComponent rackComponent;
    juce::TextButton presetPrevious;
    juce::TextButton presetMenuButton;
    juce::TextButton presetNext;
    juce::TextButton presetSave;
    juce::TextButton aboutButton;
    BqtSwitch eqMode { "l/r", "m/s" };
    BqtSwitch satMode { "l/r", "m/s" };
    juce::ComboBox osRealtime;
    juce::ComboBox osRender;
    juce::Label inputTrimLabel;
    juce::Label eqCaption;
    juce::Label satCaption;
    juce::Slider inputTrim;
    juce::ToggleButton autoGain;
    juce::ToggleButton eqLink;
    juce::ToggleButton satLink;
    juce::ToggleButton vintage;
    // One button for both sides' sat type (see toggleSatTypeBothSides).
    juce::TextButton satTypeButton;
    juce::ToggleButton bypass;
    std::array<SideControls, 2> sideControls;
    // Moves both native needles at the screen's refresh, off the message thread. Declared before
    // the meters so it outlives them; stopped first thing in the destructor.
    bqst::vu::NeedleClock needleClock;
    BqtVuMeter meterA;
    BqtVuMeter meterB;
    AboutPanel aboutPanel;

    // The mode switches only show their parameter; a click writes it as one host gesture.
    std::unique_ptr<juce::ParameterAttachment> eqModeAttachment;
    std::unique_ptr<juce::ParameterAttachment> satModeAttachment;
    std::unique_ptr<ComboBoxAttachment> osRealtimeAttachment;
    std::unique_ptr<ComboBoxAttachment> osRenderAttachment;
    std::unique_ptr<SliderAttachment> inputTrimAttachment;
    std::unique_ptr<ButtonAttachment> autoGainAttachment;
    std::unique_ptr<ButtonAttachment> eqLinkAttachment;
    std::unique_ptr<ButtonAttachment> satLinkAttachment;
    std::unique_ptr<ButtonAttachment> vintageAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    // Drives the rack's bypassed look from the parameter itself (clicks, host automation, state
    // restore); callbacks arrive on the message thread. The new look is only applied on the next
    // display frame (see meterVBlank), so a frame never shows part of the rack in each state.
    std::unique_ptr<juce::ParameterAttachment> bypassLookAttachment;
    bool bypassLookPending = false;
    BqtReadoutBubble readoutBubble;
    // The meters are driven from the display's vertical blank rather than the 60 Hz message
    // timer. A 60 Hz juce::Timer beats against a 60 Hz refresh -- message-loop granularity
    // delivers two ticks inside one frame and none in the next -- which is what made the needle
    // look intermittently low-framerate.
    std::unique_ptr<bqt::FrameLog> frameLog;
    juce::VBlankAttachment meterVBlank;
    // Every child that paints over the meters and reports its shape (bqst::vu::OpaqueCover): a
    // change to one re-presents both needles at once, in the same message-thread turn (and so the
    // same Core Animation commit) as the change's own repaint, not at the next display callback.
    struct NeedleCoverWatcher final : juce::ComponentListener
    {
        std::function<void()> changed;
        void componentVisibilityChanged(juce::Component&) override { changed(); }
        void componentMovedOrResized(juce::Component&, bool, bool) override { changed(); }
        void componentBroughtToFront(juce::Component&) override { changed(); }
    };
    NeedleCoverWatcher needleCoverWatcher;
    std::vector<juce::Component*> needleCovers;
    bool isMirroringLinkedControl = false;
    // Per link group (eq, sat): whether side B's controls are currently attached to side A's
    // parameters. See updateLinkedAttachments.
    std::array<bool, 2> sideBAttachedToA { false, false };
    // Controls inside a genuine user gesture; only these may drive linked mirroring.
    bqt::editor::UserGestureTracker userGestures;
    juce::Component::SafePointer<juce::Slider> activeReadoutSlider;
    // SafePointers, and only ever set to components inside this editor: the hover source is the
    // process-global mouse, so these used to be able to latch controls belonging to another BQST
    // instance and dangle when that instance's editor closed.
    juce::Component::SafePointer<juce::Slider> hoveredReadoutSlider;
    juce::uint32 hoverReadoutStartMs = 0;
    bool hoverReadoutVisible = false;
    juce::Component::SafePointer<juce::Component> hoveredHelpComponent;
    juce::String hoveredHelpText;
    juce::uint32 helpHoverStartMs = 0;
    bool helpVisible = false;
    // A press on a bar control hides its hover help until the pointer moves or a menu closes.
    bool helpClickLatched = false;
    bool helpLatchSawBlock = false;
    juce::Point<float> helpLatchPosition;
    // The first bar row, in base editor pixels: its hover help sits at the window's top edge.
    juce::Rectangle<int> presetBarBounds;
    juce::Component::SafePointer<juce::Component> focusBeforeAbout;
    std::vector<juce::Component::SafePointer<juce::Component>> hiddenFromAccessibility;
    static constexpr juce::uint32 hoverHelpDelayMs = 700;
    static constexpr juce::uint32 hoverValueDelayMs = 300;
    double inputTrimCompensationStart = 0.0;
    std::array<double, 2> outputTrimCompensationStart { 0.0, 0.0 };
    int selectedPresetIndex = 0;
    juce::String selectedPresetKey;
    juce::Array<juce::RangedAudioParameter*> activeMirroredGestureParameters;
    juce::StringArray undoableParameterIds;
    bqt::editor::ParameterSnapshot pendingUndoState;
    std::vector<bqt::editor::ParameterSnapshot> undoStack;
    std::vector<bqt::editor::ParameterSnapshot> redoStack;
    bool restoringPluginEditState = false;
    bool undoCaptureActive = false;
    std::unique_ptr<juce::FileChooser> presetFileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BqtAudioProcessorEditor)
};
