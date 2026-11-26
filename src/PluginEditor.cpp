#include "PluginEditor.h"

#include "BqtEditorStyle.h"

#include <cmath>

namespace
{
using namespace bqst::ui;
} // namespace

BqtAudioProcessorEditor::BqtAudioProcessorEditor(BqtAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), presetManager(p.state()), rackComponent(*this), meterA(p, 0), meterB(p, 1)
{
    setLookAndFeel(&hardwareLookAndFeel);
    undoableParameterIds = bqt::editor::undoableParameterIds(audioProcessor);
    setWantsKeyboardFocus(true);
    addKeyListener(this);
    const auto savedWidth = bqt::editor::restoredWidth(audioProcessor.state().state);

    addAndMakeVisible(rackComponent);
    addAndMakeVisible(eqMode);
    addAndMakeVisible(satMode);
    configureCombo(osRealtime);
    configureCombo(osRender);
    // Bar captions: quiet ink, smaller than the controls' own words.
    for (auto [label, text] : { std::pair { &inputTrimLabel, "input" }, std::pair { &eqCaption, "eq" },
                                std::pair { &satCaption, "sat" } })
    {
        configureLabel(*label, text, juce::Justification::centredLeft);
        label->setColour(juce::Label::textColourId, juce::Colour(ink).withAlpha(0.6f));
        label->setFont(faceFont(15.0f));
        label->setBorderSize({});
        label->setInterceptsMouseClicks(false, false);
    }
    configureSlider(inputTrim);
    inputTrim.getProperties().set("bqtTopInputKnob", true);
    inputTrim.setRepaintsOnMouseActivity(true);
    inputTrim.textFromValueFunction = [](double value) { return juce::String(value, 1) + " dB"; };
    inputTrim.setDoubleClickReturnValue(true, 0.0);
    addAndMakeVisible(presetPrevious);
    addAndMakeVisible(presetNext);
    addAndMakeVisible(presetSave);
    addAndMakeVisible(presetMenuButton);
    addAndMakeVisible(aboutButton);
    addAndMakeVisible(autoGain);
    addAndMakeVisible(eqLink);
    addAndMakeVisible(satLink);
    rackComponent.addAndMakeVisible(vintage);
    addAndMakeVisible(bypass);
    rackComponent.addAndMakeVisible(meterA);
    rackComponent.addAndMakeVisible(meterB);
    meterA.setInterceptsMouseClicks(false, false);
    meterB.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(readoutBubble);
    readoutBubble.setVisible(false);
    readoutBubble.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(aboutPanel);
    aboutPanel.setVisible(false);

    presetPrevious.setButtonText("<");
    presetMenuButton.setButtonText("Default");
    presetNext.setButtonText(">");
    presetSave.setButtonText("save");
    aboutButton.setButtonText("about");
    autoGain.setButtonText("autogain");
    // The "eq" and "sat" captions name each group, so the link buttons say only "link".
    eqLink.setButtonText("link");
    satLink.setButtonText("link");
    eqLink.setTitle("eq link");
    satLink.setTitle("sat link");
    eqMode.setTitle("eq mode");
    satMode.setTitle("sat mode");
    eqMode.getHalf(0).setTitle("EQ left/right");
    eqMode.getHalf(1).setTitle("EQ mid/side");
    satMode.getHalf(0).setTitle("Saturation left/right");
    satMode.getHalf(1).setTitle("Saturation mid/side");
    vintage.setButtonText("vint.");
    bypass.setButtonText("bypass");
    osRealtime.addItemList(juce::StringArray { "realtime off", "realtime 2x", "realtime 4x", "realtime 8x" }, 1);
    osRender.addItemList(juce::StringArray { "render off", "render 2x", "render 4x", "render 8x" }, 1);
    selectedPresetKey = audioProcessor.state().state.getProperty("selectedPresetKey", juce::String()).toString();
    refreshPresetMenu();

    setTopBarHelp(presetPrevious, "Loads the previous preset.");
    // The preset field's hover shows the current preset's name (see updatePresetButtonText).
    setTopBarHelp(presetMenuButton, presetMenuButton.getButtonText());
    setTopBarHelp(presetNext, "Loads the next preset.");
    setTopBarHelp(presetSave, "Saves the current settings as a user preset.");
    setTopBarHelp(aboutButton, "Version and credits.");
    setTopBarHelp(inputTrim, "Adjusts level before the EQ and saturation. Control-drag compensates output trim.");
    setTopBarHelp(eqMode, "Chooses whether the EQ controls process left/right or mid/side.");
    setTopBarHelp(eqLink, "Links the two EQ sides: R/S follows L/M. Turn off to edit one side.");
    setTopBarHelp(satMode, "Chooses whether the saturation controls process left/right or mid/side.");
    setTopBarHelp(satLink, "Links the two saturation sides: R/S follows L/M. Turn off to edit one side.");
    setTopBarHelp(osRealtime, "Sets oversampling used during normal playback.");
    setTopBarHelp(osRender, "Sets oversampling used for offline export or render.");
    setTopBarHelp(autoGain, "Compensates saturation drive level so changes are easier to compare.");
    setTopBarHelp(bypass, "Bypasses the whole plugin.");
    setTopBarHelp(vintage, "Gently rounds the top end after saturation.");

    for (auto* button : { &autoGain, &eqLink, &satLink, &bypass })
        button->getProperties().set("bqtPushButton", true);
    for (auto* button : { &presetPrevious, &presetMenuButton, &presetNext, &presetSave })
        button->getProperties().set("bqtPushButton", true);
    aboutButton.getProperties().set("bqtPushButton", true);

    for (auto* control : { static_cast<juce::Component*>(&presetPrevious), static_cast<juce::Component*>(&presetMenuButton),
                           static_cast<juce::Component*>(&presetNext), static_cast<juce::Component*>(&presetSave),
                           static_cast<juce::Component*>(&aboutButton), static_cast<juce::Component*>(&bypass),
                           static_cast<juce::Component*>(&autoGain), static_cast<juce::Component*>(&eqLink),
                           static_cast<juce::Component*>(&satLink), static_cast<juce::Component*>(&eqMode),
                           static_cast<juce::Component*>(&satMode), static_cast<juce::Component*>(&osRealtime),
                           static_cast<juce::Component*>(&osRender) })
        control->getProperties().set("bqtFlatBar", true);
    presetMenuButton.getProperties().set("bqtDropdown", true);
    // Action buttons never latch; only the parameter toggles show an on state.
    for (auto* button : { &presetPrevious, &presetMenuButton, &presetNext, &presetSave, &aboutButton })
        button->setClickingTogglesState(false);

    presetPrevious.onClick = [this] { selectRelativePreset(-1); };
    presetMenuButton.onClick = [this] { showPresetMenu(); };
    presetNext.onClick = [this] { selectRelativePreset(1); };
    presetSave.onClick = [this] { saveUserPreset(); };
    aboutButton.onClick = [this] { showAboutPanel(); };
    aboutPanel.onClose = [this] { hideAboutPanel(); };

    setResizable(true, true);
    screenFit.setSizeLimits(bqt::editor::minimumWidth, bqt::editor::heightForWidth(bqt::editor::minimumWidth),
                            bqt::editor::maximumWidth, bqt::editor::heightForWidth(bqt::editor::maximumWidth));
    screenFit.setFixedAspectRatio(static_cast<double>(baseEditorWidth) / baseEditorHeight);
    setConstrainer(&screenFit);
    // Double-clicking the grip returns the editor to its default size (see mouseDoubleClick).
    if (resizableCorner != nullptr)
        resizableCorner->addMouseListener(this, false);
    setSize(savedWidth, bqt::editor::heightForWidth(savedWidth));

    auto bindSwitch = [this](BqtSwitch& modeSwitch, const juce::String& parameterId)
    {
        auto attachment = std::make_unique<juce::ParameterAttachment>(
            *audioProcessor.state().getParameter(parameterId),
            [&modeSwitch](float index) { modeSwitch.setSelected(juce::roundToInt(index)); });
        attachment->sendInitialUpdate();
        modeSwitch.onSelect = [raw = attachment.get()](int index) { raw->setValueAsCompleteGesture(static_cast<float>(index)); };
        return attachment;
    };
    eqModeAttachment = bindSwitch(eqMode, "eqMode");
    satModeAttachment = bindSwitch(satMode, "satMode");
    osRealtimeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "osRealtime", osRealtime);
    osRenderAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "osRender", osRender);
    inputTrimAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), "inputTrim", inputTrim);
    autoGainAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "autoGain", autoGain);
    eqLinkAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "eqLink", eqLink);
    satLinkAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "satLink", satLink);
    // The link attachments write the parameter before onClick runs, so a click that turned a
    // link off finds the button already off here.
    eqLink.onClick = [this]
    {
        if (! eqLink.getToggleState())
            unlinkGroupFromUi(bqt::editor::LinkGroup::eq);
        updateLinkedAttachments();
    };
    satLink.onClick = [this]
    {
        if (! satLink.getToggleState())
            unlinkGroupFromUi(bqt::editor::LinkGroup::sat);
        updateLinkedAttachments();
    };
    vintageAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "vintage", vintage);
    bypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "bypass", bypass);
    bypassLookAttachment = std::make_unique<juce::ParameterAttachment>(
        *audioProcessor.state().getParameter("bypass"),
        [this](float value) { bypassLookPending = value > 0.5f; });
    bypassLookAttachment->sendInitialUpdate();
    rackComponent.setBypassed(bypassLookPending);
    inputTrimCompensationStart = inputTrim.getValue();
    for (size_t index = 0; index < sideControls.size(); ++index)
        outputTrimCompensationStart[index] = sideControls[index].outputTrim.getValue();

    for (int side = 0; side < 2; ++side)
        configureSide(sideControls[static_cast<size_t>(side)], side);

    // configureSide attached side B to its own parameters; point linked groups at side A's.
    updateLinkedAttachments();

    // Sat type is not linked: this one button always sets both sides, whatever Sat Link or
    // Control say.
    satTypeButton.getProperties().set("bqtSatTypeSelector", true);
    satTypeButton.setButtonText("sat type");
    satTypeButton.addMouseListener(this, true);
    satTypeButton.getProperties().set("bqtCreamHelp", "Combination of op-amps, transistors and diodes for a smooth, thick saturation.");
    satTypeButton.getProperties().set("bqtGritHelp", "Transformer-inspired saturation with firmer edge and bite.");
    satTypeButton.onClick = [this] { toggleSatTypeBothSides(satTypeButton); };
    rackComponent.addAndMakeVisible(satTypeButton);

    // Ctrl-drag on an unlinked group moves both sides (see shouldMirrorToOtherSide). Linked groups
    // are never mirrored here: the processor links them and side B's knobs edit side A's
    // parameters (updateLinkedAttachments), so a linked move writes a single parameter.
    //
    // Mirror only for a real user gesture on the source control. onValueChange also fires when a
    // SliderAttachment writes the slider in response to a parameter change, which is how host
    // automation, preset loads and setStateInformation all arrive. Mirroring those corrupted
    // state: restoring an asymmetric pair made side A mirror onto B, then B mirror back onto A,
    // collapsing both onto whichever was restored last and pushing the wrong values back to the
    // host via setValueNotifyingHost.
    //
    // The gesture is tracked explicitly from the slider's drag notifications (mouse drags, wheel
    // steps, double-click resets and accessibility sets all send them), not inferred from the
    // mouse: a hover test also passed for automation arriving while the pointer rested on a knob.
    auto mirrorSlider = [this](bqt::editor::LinkGroup group, juce::Slider& source, juce::Slider& dest)
    {
        if (! bqt::editor::shouldMirrorLinkedEdit(shouldMirrorToOtherSide(group),
                                                  userGestures.isActive(&source),
                                                  isMirroringLinkedControl))
            return;

        const juce::ScopedValueSetter<bool> scopedMirror(isMirroringLinkedControl, true);
        dest.setValue(source.getValue(), juce::sendNotificationSync);
    };

    auto linkSides = [this, mirrorSlider](bqt::editor::LinkGroup group, juce::Slider SideControls::* control)
    {
        auto& left = sideControls[0].*control;
        auto& right = sideControls[1].*control;
        left.onValueChange = [mirrorSlider, group, &left, &right] { mirrorSlider(group, left, right); };
        right.onValueChange = [mirrorSlider, group, &left, &right] { mirrorSlider(group, right, left); };
    };

    using bqt::editor::LinkGroup;
    linkSides(LinkGroup::eq, &SideControls::lowGain);
    linkSides(LinkGroup::eq, &SideControls::lowFreq);
    linkSides(LinkGroup::eq, &SideControls::highGain);
    linkSides(LinkGroup::eq, &SideControls::highFreq);
    linkSides(LinkGroup::sat, &SideControls::drive);
    linkSides(LinkGroup::sat, &SideControls::mix);
    linkSides(LinkGroup::sat, &SideControls::outputTrim);

    const auto logPath = juce::SystemStats::getEnvironmentVariable("BQST_FRAME_LOG", {});
    if (logPath.isNotEmpty())
    {
        auto log = std::make_unique<bqt::FrameLog>(logPath.toStdString(), audioProcessor.frameLogAudio, true,
                                                   bqt::FrameLog::nowMs(), &needleClock.frameLogStats());
        if (log->enabled()) frameLog = std::move(log);
    }

    meterA.setNeedleClock(&needleClock);
    meterB.setNeedleClock(&needleClock);
    needleCoverWatcher.changed = [this]
    {
        meterA.coversChanged();
        meterB.coversChanged();
    };
    for (auto* child : getChildren())
        if (dynamic_cast<bqst::vu::OpaqueCover*>(child) != nullptr)
        {
            child->addComponentListener(&needleCoverWatcher);
            needleCovers.push_back(child);
        }

    // Display callbacks present the sample-time movement published by the processor.
    meterVBlank = juce::VBlankAttachment(this, [this]
    {
        bqt::FrameLog::Scope frameTiming(frameLog.get(), true);
        // Apply a bypass change here rather than when the parameter changes. On macOS, JUCE hands
        // the frame's repaint areas to the OS right after this callback, but areas already handed
        // over (the meters, every frame) can be drawn before then; flipping the state mid-frame
        // drew the meters dimmed one frame ahead of the rest of the rack.
        rackComponent.setBypassed(bypassLookPending);
        const auto bypassed = rackComponent.isBypassed();

        // Each meter marks only the strip its needle swept.
        meterA.updateLevel();
        meterB.updateLevel();

        // On macOS the needles may be presented in a native layer that the needle clock moves at
        // the display's own rate, however often this callback runs (see BqtVuNeedleLayer.h).
        // While bypassed, the needles stay in that layer, dimmed like the rack, fall to rest and
        // stand still there (the clock then commits nothing).
        meterA.syncNeedlePresentation(bypassed);
        meterB.syncNeedlePresentation(bypassed);
        if (frameLog)
        {
            frameTiming.nativeA = meterA.isNeedleNative();
            frameTiming.nativeB = meterB.isNeedleNative();
        }
    });

    startTimerHz(60);
}

BqtAudioProcessorEditor::~BqtAudioProcessorEditor()
{
    for (auto* cover : needleCovers)
        cover->removeComponentListener(&needleCoverWatcher);
    // Before anything the clock thread reads (the meters' layers, the frame log's counters) goes.
    needleClock.stop();
    stopTimer();
    endLinkedMirrorGestures();
    removeKeyListener(this);

    inputTrim.removeListener(this);
    inputTrim.removeMouseListener(this);

    for (auto& controls : sideControls)
    {
        controls.lowGain.removeListener(this);
        controls.lowFreq.removeListener(this);
        controls.highGain.removeListener(this);
        controls.highFreq.removeListener(this);
        controls.drive.removeListener(this);
        controls.mix.removeListener(this);
        controls.outputTrim.removeListener(this);
        controls.lowGain.removeMouseListener(this);
        controls.lowFreq.removeMouseListener(this);
        controls.highGain.removeMouseListener(this);
        controls.highFreq.removeMouseListener(this);
        controls.drive.removeMouseListener(this);
        controls.mix.removeMouseListener(this);
        controls.outputTrim.removeMouseListener(this);
    }

    satTypeButton.removeMouseListener(this);

    for (auto* component : { static_cast<juce::Component*>(&presetPrevious), static_cast<juce::Component*>(&presetMenuButton),
                             static_cast<juce::Component*>(&presetNext), static_cast<juce::Component*>(&presetSave),
                             static_cast<juce::Component*>(&eqMode), static_cast<juce::Component*>(&satMode),
                             static_cast<juce::Component*>(&osRealtime), static_cast<juce::Component*>(&osRender),
                             static_cast<juce::Component*>(&autoGain), static_cast<juce::Component*>(&eqLink),
                             static_cast<juce::Component*>(&satLink), static_cast<juce::Component*>(&bypass),
                             static_cast<juce::Component*>(&vintage) })
        component->removeMouseListener(this);

    setLookAndFeel(nullptr);
}

void BqtAudioProcessorEditor::configureSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setVelocityBasedMode(false);
    slider.setVelocityModeParameters(0.35, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    slider.textFromValueFunction = [](double value) { return juce::String(value, 1); };
    slider.setTooltip({});
    slider.addListener(this);
    slider.addMouseListener(this, false);
    addAndMakeVisible(slider);
}

void BqtAudioProcessorEditor::configureCombo(juce::ComboBox& combo)
{
    // No JUCE tooltip: combos use the custom top-bar help system (setTopBarHelp), so a tooltip
    // here would show a second, redundant hint on hover.
    combo.setColour(juce::ComboBox::textColourId, juce::Colour(ink).withAlpha(0.85f));
    addAndMakeVisible(combo);
}

void BqtAudioProcessorEditor::configureLabel(juce::Label& label, const juce::String& text, juce::Justification justification)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(justification);
    label.setColour(juce::Label::textColourId, juce::Colour(panelText));
    label.setFont(faceFont(14.0f));
    addAndMakeVisible(label);
}

void BqtAudioProcessorEditor::configureSide(SideControls& controls, int sideIndex)
{
    configureSlider(controls.highGain);
    controls.highGain.getProperties().set("bqtLargeCreamKnob", true);
    controls.highGain.textFromValueFunction = [](double value) { return juce::String(value, 1) + " dB"; };
    configureSlider(controls.highFreq);
    controls.highFreq.getProperties().set("bqtKnobCombo", true);
    controls.highFreq.setRange(0.0, static_cast<double>(highFreqLabels.size() - 1), 1.0);
    controls.highFreq.setChangeNotificationOnlyOnRelease(true);
    controls.highFreq.textFromValueFunction = [](double value) { return indexedLabel(highFreqLabels, value); };
    configureSlider(controls.lowGain);
    controls.lowGain.getProperties().set("bqtLargeCreamKnob", true);
    controls.lowGain.textFromValueFunction = [](double value) { return juce::String(value, 1) + " dB"; };
    configureSlider(controls.lowFreq);
    controls.lowFreq.getProperties().set("bqtKnobCombo", true);
    controls.lowFreq.setRange(0.0, static_cast<double>(lowFreqLabels.size() - 1), 1.0);
    controls.lowFreq.setChangeNotificationOnlyOnRelease(true);
    controls.lowFreq.textFromValueFunction = [](double value) { return indexedLabel(lowFreqLabels, value); };
    configureSlider(controls.drive);
    controls.drive.getProperties().set("bqtLargeCreamKnob", true);
    controls.drive.textFromValueFunction = [](double value) { return juce::String(value, 1) + " dB"; };
    configureSlider(controls.mix);
    controls.mix.getProperties().set("bqtSmallCreamKnob", true);
    controls.mix.textFromValueFunction = [](double value) { return juce::String(value, 1) + "%"; };
    configureSlider(controls.outputTrim);
    controls.outputTrim.getProperties().set("bqtSmallCreamKnob", true);
    controls.outputTrim.textFromValueFunction = [](double value) { return juce::String(value, 1) + " dB"; };
    controls.lowGain.setDoubleClickReturnValue(true, 0.0);
    controls.highGain.setDoubleClickReturnValue(true, 0.0);
    controls.drive.setDoubleClickReturnValue(true, 0.0);
    controls.mix.setDoubleClickReturnValue(true, 100.0);
    controls.outputTrim.setDoubleClickReturnValue(true, 0.0);
    controls.satType.addItemList(juce::StringArray { "cream", "grit" }, 1);

    const auto prefix = sidePrefix(sideIndex);
    controls.lowGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "LowGain", controls.lowGain);
    controls.lowFreqAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "LowFreq", controls.lowFreq);
    controls.highGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "HighGain", controls.highGain);
    controls.highFreqAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "HighFreq", controls.highFreq);
    controls.driveAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "Drive", controls.drive);
    controls.satTypeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), prefix + "SatType", controls.satType);
    controls.mixAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "Mix", controls.mix);
    controls.outputTrimAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "OutputTrim", controls.outputTrim);

    for (auto* component : { static_cast<juce::Component*>(&controls.lowGain),
                             static_cast<juce::Component*>(&controls.lowFreq),
                             static_cast<juce::Component*>(&controls.highGain),
                             static_cast<juce::Component*>(&controls.highFreq),
                             static_cast<juce::Component*>(&controls.drive),
                             static_cast<juce::Component*>(&controls.mix),
                             static_cast<juce::Component*>(&controls.outputTrim) })
        rackComponent.addAndMakeVisible(*component);
}

void BqtScreenFitConstrainer::checkBounds(juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous,
                                          const juce::Rectangle<int>& limits, bool top, bool left, bool bottom, bool right)
{
    juce::ComponentBoundsConstrainer::checkBounds(bounds, previous, limits, top, left, bottom, right);

    if (bounds.getWidth() <= previous.getWidth() || editor.getPeer() == nullptr || editor.getHeight() <= 0)
        return;

    const auto onScreen = editor.getScreenBounds();
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(onScreen);
    if (display == nullptr || onScreen.getHeight() <= 0)
        return;

    // A host may scale the editor, so convert the room left on screen into editor units.
    const auto editorUnitsPerScreenUnit = static_cast<double>(editor.getHeight()) / onScreen.getHeight();
    const auto room = (display->userArea.getBottom() - onScreen.getY()) * editorUnitsPerScreenUnit;
    const auto width = juce::jmax(previous.getWidth(), bqt::editor::widthFittingHeight(room));
    if (bounds.getWidth() > width)
        bounds.setSize(width, bqt::editor::heightForWidth(width));
}
