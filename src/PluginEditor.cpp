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
    addAndMakeVisible(autoGain);
    addAndMakeVisible(eqBypass);
    addAndMakeVisible(satBypass);
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

    presetPrevious.setButtonText("<");
    presetMenuButton.setButtonText("Default");
    presetNext.setButtonText(">");
    presetSave.setButtonText("save");
    autoGain.setButtonText("autogain");
    eqBypass.setButtonText("eq in");
    satBypass.setButtonText("sat in");
    eqLink.setButtonText("eq link");
    satLink.setButtonText("sat link");
    vintage.setButtonText("vint.");
    bypass.setButtonText("bypass");
    eqMode.addItemList(juce::StringArray { "eq l/r", "eq m/s" }, 1);
    satMode.addItemList(juce::StringArray { "sat l/r", "sat m/s" }, 1);
    osRealtime.addItemList(juce::StringArray { "realtime off", "realtime 2x", "realtime 4x", "realtime 8x" }, 1);
    osRender.addItemList(juce::StringArray { "render off", "render 2x", "render 4x", "render 8x" }, 1);
    sizeSelect.addItemList(juce::StringArray { "75%", "100%", "125%", "150%" }, 1);
    sizeSelect.setSelectedId(2, juce::dontSendNotification);
    refreshPresetMenu();

    setTopBarHelp(presetPrevious, "Loads the previous preset.");
    setTopBarHelp(presetMenuButton, "Loads factory and user presets.");
    setTopBarHelp(presetNext, "Loads the next preset.");
    setTopBarHelp(presetSave, "Saves the current settings as a user preset.");
    setTopBarHelp(inputTrim, "Adjusts level before the EQ and saturation. Control-drag compensates output trim.");
    setTopBarHelp(eqMode, "Chooses whether the EQ controls process left/right or mid/side.");
    setTopBarHelp(eqLink, "Links the two EQ sides.");
    setTopBarHelp(satMode, "Chooses whether the saturation controls process left/right or mid/side.");
    setTopBarHelp(satLink, "Links the two saturation sides.");
    setTopBarHelp(osRealtime, "Sets oversampling used during normal playback.");
    setTopBarHelp(osRender, "Sets oversampling used for offline export or render.");
    setTopBarHelp(autoGain, "Compensates saturation drive level so changes are easier to compare.");
    setTopBarHelp(bypass, "Bypasses the whole plugin.");
    setTopBarHelp(sizeSelect, "Switches between normal and large plugin size.");
    setTopBarHelp(vintage, "Gently rounds the top end after saturation.");

    for (auto* button : { &autoGain, &eqBypass, &satBypass, &eqLink, &satLink, &bypass })
        button->getProperties().set("bqtPushButton", true);
    for (auto* button : { &presetPrevious, &presetMenuButton, &presetNext, &presetSave })
        button->getProperties().set("bqtPushButton", true);

    presetPrevious.onClick = [this] { selectRelativePreset(-1); };
    presetMenuButton.onClick = [this] { showPresetMenu(); };
    presetNext.onClick = [this] { selectRelativePreset(1); };
    presetSave.onClick = [this] { saveUserPreset(); };

    sizeSelect.onChange = [this]
    {
        const auto nextScale = [this]
        {
            switch (sizeSelect.getSelectedId())
            {
                case 1: return 0.75f;
                case 3: return fixedEditorScale;
                case 4: return 1.50f;
                default: return 1.0f;
            }
        }();

        setSize(static_cast<int>(std::round(static_cast<float>(baseEditorWidth) * nextScale)),
                static_cast<int>(std::round(static_cast<float>(baseEditorHeight) * nextScale)));
    };

    eqModeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "eqMode", eqMode);
    satModeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "satMode", satMode);
    osRealtimeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "osRealtime", osRealtime);
    osRenderAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "osRender", osRender);
    inputTrimAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), "inputTrim", inputTrim);
    autoGainAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "autoGain", autoGain);
    eqLinkAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "eqLink", eqLink);
    satLinkAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "satLink", satLink);
    vintageAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "vintage", vintage);
    bypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "bypass", bypass);
    bypass.onClick = [this]
    {
        if (auto* param = audioProcessor.state().getParameter("bypass"))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost(bypass.getToggleState() ? 1.0f : 0.0f);
            param->endChangeGesture();
        }

        requestRackBypassVisualState(bypass.getToggleState());
    };
    inputTrimCompensationStart = inputTrim.getValue();
    for (size_t index = 0; index < sideControls.size(); ++index)
        outputTrimCompensationStart[index] = sideControls[index].outputTrim.getValue();

    auto setInButtonTarget = [this](const char* parameterId, juce::ToggleButton& button)
    {
        button.onClick = [this, parameterId, &button]
        {
            if (auto* param = audioProcessor.state().getParameter(parameterId))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost(button.getToggleState() ? 0.0f : 1.0f);
                param->endChangeGesture();
            }
        };
    };

    setInButtonTarget("eqBypass", eqBypass);
    setInButtonTarget("satBypass", satBypass);

    for (int side = 0; side < 2; ++side)
        configureSide(sideControls[static_cast<size_t>(side)], side);

    auto mirrorSlider = [this](juce::Slider& source, juce::Slider& dest)
    {
        if (isMirroringLinkedControl)
            return;

        const juce::ScopedValueSetter<bool> scopedMirror(isMirroringLinkedControl, true);
        dest.setValue(source.getValue(), juce::sendNotificationSync);
    };

    auto mirrorChoice = [this](juce::ComboBox& source, juce::ComboBox& dest)
    {
        if (isMirroringLinkedControl)
            return;

        const juce::ScopedValueSetter<bool> scopedMirror(isMirroringLinkedControl, true);
        dest.setSelectedItemIndex(source.getSelectedItemIndex(), juce::sendNotificationSync);
    };

    auto& left = sideControls[0];
    auto& right = sideControls[1];
    left.lowGain.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (shouldMirrorLinkedControls("eqLink"))
            mirrorSlider(left.lowGain, right.lowGain);
    };
    right.lowGain.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (shouldMirrorLinkedControls("eqLink"))
            mirrorSlider(right.lowGain, left.lowGain);
    };
    left.lowFreq.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (shouldMirrorLinkedControls("eqLink"))
            mirrorSlider(left.lowFreq, right.lowFreq);
    };
    right.lowFreq.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (shouldMirrorLinkedControls("eqLink"))
            mirrorSlider(right.lowFreq, left.lowFreq);
    };
    left.highGain.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (shouldMirrorLinkedControls("eqLink"))
            mirrorSlider(left.highGain, right.highGain);
    };
    right.highGain.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (shouldMirrorLinkedControls("eqLink"))
            mirrorSlider(right.highGain, left.highGain);
    };
    left.highFreq.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (shouldMirrorLinkedControls("eqLink"))
            mirrorSlider(left.highFreq, right.highFreq);
    };
    right.highFreq.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (shouldMirrorLinkedControls("eqLink"))
            mirrorSlider(right.highFreq, left.highFreq);
    };

    left.drive.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (shouldMirrorLinkedControls("satLink"))
            mirrorSlider(left.drive, right.drive);
    };
    right.drive.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (shouldMirrorLinkedControls("satLink"))
            mirrorSlider(right.drive, left.drive);
    };
    left.mix.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (shouldMirrorLinkedControls("satLink"))
            mirrorSlider(left.mix, right.mix);
    };
    right.mix.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (shouldMirrorLinkedControls("satLink"))
            mirrorSlider(right.mix, left.mix);
    };
    left.outputTrim.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (shouldMirrorLinkedControls("satLink"))
            mirrorSlider(left.outputTrim, right.outputTrim);
    };
    right.outputTrim.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (shouldMirrorLinkedControls("satLink"))
            mirrorSlider(right.outputTrim, left.outputTrim);
    };
    left.satType.onChange = [this, &left, &right, mirrorChoice]
    {
        if (shouldMirrorLinkedControls("satLink"))
            mirrorChoice(left.satType, right.satType);
    };
    right.satType.onChange = [this, &left, &right, mirrorChoice]
    {
        if (shouldMirrorLinkedControls("satLink"))
            mirrorChoice(right.satType, left.satType);
    };

    startTimerHz(60);
    updateLinkedControlStates();
}

BqtAudioProcessorEditor::~BqtAudioProcessorEditor()
{
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
        controls.satTypeButton.removeMouseListener(this);
    }

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
    combo.setTooltip("Click to choose a stepped setting.");
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
    const auto sideText = sideIndex == 0 ? "l/m" : "r/s";
    configureLabel(controls.eqSectionLabel, sideText, juce::Justification::centredLeft);
    configureLabel(controls.satSectionLabel, sideText, juce::Justification::centredLeft);
    configureLabel(controls.highGainLabel, "hf");
    configureSlider(controls.highGain);
    controls.highGain.getProperties().set("bqtLargeCreamKnob", true);
    controls.highGain.textFromValueFunction = [](double value) { return juce::String(value, 1) + " dB"; };
    configureLabel(controls.highFreqLabel, "freq");
    configureSlider(controls.highFreq);
    controls.highFreq.getProperties().set("bqtKnobCombo", true);
    controls.highFreq.setRange(0.0, 7.0, 1.0);
    controls.highFreq.setChangeNotificationOnlyOnRelease(true);
    controls.highFreq.textFromValueFunction = [](double value) { return indexedLabel(highFreqLabels, value); };
    configureLabel(controls.lowGainLabel, "lf");
    configureSlider(controls.lowGain);
    controls.lowGain.getProperties().set("bqtLargeCreamKnob", true);
    controls.lowGain.textFromValueFunction = [](double value) { return juce::String(value, 1) + " dB"; };
    configureLabel(controls.lowFreqLabel, "freq");
    configureSlider(controls.lowFreq);
    controls.lowFreq.getProperties().set("bqtKnobCombo", true);
    controls.lowFreq.setRange(0.0, 7.0, 1.0);
    controls.lowFreq.setChangeNotificationOnlyOnRelease(true);
    controls.lowFreq.textFromValueFunction = [](double value) { return indexedLabel(lowFreqLabels, value); };
    configureLabel(controls.driveLabel, "drive");
    configureSlider(controls.drive);
    controls.drive.getProperties().set("bqtLargeCreamKnob", true);
    controls.drive.textFromValueFunction = [](double value) { return juce::String(value, 1) + " dB"; };
    configureLabel(controls.satTypeLabel, "sat. type");
    configureCombo(controls.satType);
    controls.satType.setVisible(false);
    controls.satTypeButton.getProperties().set("bqtSatTypeSelector", true);
    controls.satTypeButton.setButtonText("sat type");
    controls.satTypeButton.addMouseListener(this, true);
    addAndMakeVisible(controls.satTypeButton);
    controls.satTypeButton.getProperties().set("bqtCreamHelp", "Combination of op-amps, transistors and diodes for a smooth, thick saturation.");
    controls.satTypeButton.getProperties().set("bqtGritHelp", "Transformer-style saturation with firmer edge and bite.");
    configureLabel(controls.mixLabel, "mix");
    configureSlider(controls.mix);
    controls.mix.getProperties().set("bqtSmallCreamKnob", true);
    controls.mix.textFromValueFunction = [](double value) { return juce::String(value, 1) + "%"; };
    configureLabel(controls.outputTrimLabel, "output");
    configureSlider(controls.outputTrim);
    controls.outputTrim.getProperties().set("bqtSmallCreamKnob", true);
    controls.outputTrim.textFromValueFunction = [](double value) { return juce::String(value, 1) + " dB"; };
    controls.lowGain.setDoubleClickReturnValue(true, 0.0);
    controls.highGain.setDoubleClickReturnValue(true, 0.0);
    controls.drive.setDoubleClickReturnValue(true, 0.0);
    controls.mix.setDoubleClickReturnValue(true, 100.0);
    controls.outputTrim.setDoubleClickReturnValue(true, 0.0);
    controls.satType.addItemList(juce::StringArray { "cream", "grit" }, 1);
    controls.satTypeButton.onClick = [this, &combo = controls.satType, &button = controls.satTypeButton]
    {
        const auto next = combo.getSelectedItemIndex() == 0 ? 1 : 0;
        combo.setSelectedItemIndex(next, juce::sendNotificationSync);
        button.setToggleState(next == 1, juce::dontSendNotification);

        if (hoveredHelpComponent == &button && helpVisible)
        {
            hoveredHelpText = button.getProperties()[next == 1 ? "bqtGritHelp" : "bqtCreamHelp"].toString();
            showReadout(button, hoveredHelpText);
        }
    };

    const auto prefix = sidePrefix(sideIndex);
    controls.lowGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "LowGain", controls.lowGain);
    controls.lowFreqAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "LowFreq", controls.lowFreq);
    controls.highGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "HighGain", controls.highGain);
    controls.highFreqAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "HighFreq", controls.highFreq);
    controls.driveAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "Drive", controls.drive);
    controls.satTypeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), prefix + "SatType", controls.satType);
    controls.mixAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "Mix", controls.mix);
    controls.outputTrimAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "OutputTrim", controls.outputTrim);

    for (auto* component : { static_cast<juce::Component*>(&controls.eqSectionLabel),
                             static_cast<juce::Component*>(&controls.satSectionLabel),
                             static_cast<juce::Component*>(&controls.lowGainLabel),
                             static_cast<juce::Component*>(&controls.lowGain),
                             static_cast<juce::Component*>(&controls.lowFreqLabel),
                             static_cast<juce::Component*>(&controls.lowFreq),
                             static_cast<juce::Component*>(&controls.highGainLabel),
                             static_cast<juce::Component*>(&controls.highGain),
                             static_cast<juce::Component*>(&controls.highFreqLabel),
                             static_cast<juce::Component*>(&controls.highFreq),
                             static_cast<juce::Component*>(&controls.driveLabel),
                             static_cast<juce::Component*>(&controls.drive),
                             static_cast<juce::Component*>(&controls.satTypeLabel),
                             static_cast<juce::Component*>(&controls.satType),
                             static_cast<juce::Component*>(&controls.satTypeButton),
                             static_cast<juce::Component*>(&controls.mixLabel),
                             static_cast<juce::Component*>(&controls.mix),
                             static_cast<juce::Component*>(&controls.outputTrimLabel),
                             static_cast<juce::Component*>(&controls.outputTrim) })
        rackComponent.addAndMakeVisible(*component);
}
