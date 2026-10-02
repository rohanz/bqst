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
    setSize(baseEditorWidth, baseEditorHeight);

    addAndMakeVisible(rackComponent);
    configureCombo(eqMode);
    configureCombo(satMode);
    configureCombo(osRealtime);
    configureCombo(osRender);
    configureCombo(sizeSelect);
    configureLabel(inputTrimLabel, "input", juce::Justification::centredLeft);
    inputTrimLabel.setColour(juce::Label::textColourId, juce::Colour(panelText));
    inputTrimLabel.setFont(faceFont(19.5f));
    configureSlider(inputTrim);
    inputTrim.getProperties().set("bqtTopInputKnob", true);
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
    addAndMakeVisible(sizeSelect);
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
    eqLink.setButtonText("eq link");
    satLink.setButtonText("sat link");
    vintage.setButtonText("vint.");
    bypass.setButtonText("bypass");
    eqMode.addItemList(juce::StringArray { "eq l/r", "eq m/s" }, 1);
    satMode.addItemList(juce::StringArray { "sat l/r", "sat m/s" }, 1);
    osRealtime.addItemList(juce::StringArray { "realtime off", "realtime 2x", "realtime 4x", "realtime 8x" }, 1);
    osRender.addItemList(juce::StringArray { "render off", "render 2x", "render 4x", "render 8x" }, 1);
    sizeSelect.addItemList(juce::StringArray { "75%", "100%", "125%", "150%" }, 1);
    // Both the view size and the selected preset are session state, restored from the APVTS tree
    // so reopening the editor does not silently reset the size or claim "Default" while the DSP
    // is still on a loaded preset.
    sizeSelect.setSelectedId(audioProcessor.state().state.getProperty("editorScale", 2),
                             juce::dontSendNotification);
    selectedPresetKey = audioProcessor.state().state.getProperty("selectedPresetKey", juce::String()).toString();
    refreshPresetMenu();

    setTopBarHelp(presetPrevious, "Loads the previous preset.");
    setTopBarHelp(presetMenuButton, "Loads factory and user presets.");
    setTopBarHelp(presetNext, "Loads the next preset.");
    setTopBarHelp(presetSave, "Saves the current settings as a user preset.");
    setTopBarHelp(aboutButton, "Shows plugin version, credits, and install details.");
    setTopBarHelp(inputTrim, "Adjusts level before the EQ and saturation. Control-drag compensates output trim.");
    setTopBarHelp(eqMode, "Chooses whether the EQ controls process left/right or mid/side.");
    setTopBarHelp(eqLink, "Links the two EQ sides: R/S follows L/M. Turn off to edit one side.");
    setTopBarHelp(satMode, "Chooses whether the saturation controls process left/right or mid/side.");
    setTopBarHelp(satLink, "Links the two saturation sides: R/S follows L/M. Turn off to edit one side.");
    setTopBarHelp(osRealtime, "Sets oversampling used during normal playback.");
    setTopBarHelp(osRender, "Sets oversampling used for offline export or render.");
    setTopBarHelp(autoGain, "Compensates saturation drive level so changes are easier to compare.");
    setTopBarHelp(bypass, "Bypasses the whole plugin.");
    setTopBarHelp(sizeSelect, "Scales the plugin window to 75%, 100%, 125% or 150%.");
    setTopBarHelp(vintage, "Gently rounds the top end after saturation.");

    for (auto* button : { &autoGain, &eqLink, &satLink, &bypass })
        button->getProperties().set("bqtPushButton", true);
    for (auto* button : { &presetPrevious, &presetMenuButton, &presetNext, &presetSave })
        button->getProperties().set("bqtPushButton", true);
    aboutButton.getProperties().set("bqtPushButton", true);

    presetPrevious.onClick = [this] { selectRelativePreset(-1); };
    presetMenuButton.onClick = [this] { showPresetMenu(); };
    presetNext.onClick = [this] { selectRelativePreset(1); };
    presetSave.onClick = [this] { saveUserPreset(); };
    aboutButton.onClick = [this] { showAboutPanel(); };
    aboutPanel.onClose = [this] { hideAboutPanel(); };

    sizeSelect.onChange = [this]
    {
        const auto nextScale = [this]
        {
            switch (sizeSelect.getSelectedId())
            {
                case 1: return 0.75f;
                case 3: return 1.25f;
                case 4: return 1.50f;
                default: return 1.0f;
            }
        }();

        audioProcessor.state().state.setProperty("editorScale", sizeSelect.getSelectedId(), nullptr);
        setSize(static_cast<int>(std::round(static_cast<float>(baseEditorWidth) * nextScale)),
                static_cast<int>(std::round(static_cast<float>(baseEditorHeight) * nextScale)));
    };

    // The restored view size was set with dontSendNotification (the handler did not exist yet),
    // so apply it now that it does.
    if (sizeSelect.getSelectedId() != 2 && sizeSelect.onChange != nullptr)
        sizeSelect.onChange();

    eqModeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "eqMode", eqMode);
    satModeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "satMode", satMode);
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
        [this](float value) { rackComponent.setBypassed(value > 0.5f); });
    bypassLookAttachment->sendInitialUpdate();
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
    satTypeButton.getProperties().set("bqtGritHelp", "Transformer-style saturation with firmer edge and bite.");
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

    // Meter animation runs on the display refresh, not the message timer, and advances by real
    // elapsed time so a late frame still covers the right distance.
    lastMeterTickSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    meterVBlank = juce::VBlankAttachment(this, [this]
    {
        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const auto elapsed = now - lastMeterTickSeconds;
        lastMeterTickSeconds = now;

        if (rackComponent.isBypassed())
            return;

        if (meterA.updateLevel(elapsed))
            meterA.repaint();

        if (meterB.updateLevel(elapsed))
            meterB.repaint();
    });

    startTimerHz(60);
}

BqtAudioProcessorEditor::~BqtAudioProcessorEditor()
{
    stopTimer();
    endLinkedMirrorGestures();
    removeKeyListener(this);

    inputTrim.removeListener(this);
    inputTrim.removeMouseListener(this);
    sizeSelect.removeMouseListener(this);

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
                             static_cast<juce::Component*>(&vintage), static_cast<juce::Component*>(&sizeSelect) })
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
    combo.setColour(juce::ComboBox::textColourId, juce::Colour(ink));
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
