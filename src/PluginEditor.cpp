#include "PluginEditor.h"

#include "BqtEditorStyle.h"

#include <array>
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
    addAndMakeVisible(bypassOverlay);
    bypassOverlay.setVisible(false);
    bypassOverlay.setInterceptsMouseClicks(false, false);
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

        rackComponent.setBypassed(bypass.getToggleState());
    };
    previousInputTrimForCompensation = inputTrim.getValue();

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

    auto shouldMirror = [this](const char* linkParameterId)
    {
        const auto linked = audioProcessor.state().getRawParameterValue(linkParameterId)->load() > 0.5f;
        const auto controlDown = juce::ModifierKeys::currentModifiers.isCtrlDown();
        return linked != controlDown;
    };

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
    left.lowGain.onValueChange = [shouldMirror, &left, &right, mirrorSlider]
    {
        if (shouldMirror("eqLink"))
            mirrorSlider(left.lowGain, right.lowGain);
    };
    right.lowGain.onValueChange = [shouldMirror, &left, &right, mirrorSlider]
    {
        if (shouldMirror("eqLink"))
            mirrorSlider(right.lowGain, left.lowGain);
    };
    left.lowFreq.onValueChange = [shouldMirror, &left, &right, mirrorSlider]
    {
        if (shouldMirror("eqLink"))
            mirrorSlider(left.lowFreq, right.lowFreq);
    };
    right.lowFreq.onValueChange = [shouldMirror, &left, &right, mirrorSlider]
    {
        if (shouldMirror("eqLink"))
            mirrorSlider(right.lowFreq, left.lowFreq);
    };
    left.highGain.onValueChange = [shouldMirror, &left, &right, mirrorSlider]
    {
        if (shouldMirror("eqLink"))
            mirrorSlider(left.highGain, right.highGain);
    };
    right.highGain.onValueChange = [shouldMirror, &left, &right, mirrorSlider]
    {
        if (shouldMirror("eqLink"))
            mirrorSlider(right.highGain, left.highGain);
    };
    left.highFreq.onValueChange = [shouldMirror, &left, &right, mirrorSlider]
    {
        if (shouldMirror("eqLink"))
            mirrorSlider(left.highFreq, right.highFreq);
    };
    right.highFreq.onValueChange = [shouldMirror, &left, &right, mirrorSlider]
    {
        if (shouldMirror("eqLink"))
            mirrorSlider(right.highFreq, left.highFreq);
    };

    left.drive.onValueChange = [shouldMirror, &left, &right, mirrorSlider]
    {
        if (shouldMirror("satLink"))
            mirrorSlider(left.drive, right.drive);
    };
    right.drive.onValueChange = [shouldMirror, &left, &right, mirrorSlider]
    {
        if (shouldMirror("satLink"))
            mirrorSlider(right.drive, left.drive);
    };
    left.mix.onValueChange = [shouldMirror, &left, &right, mirrorSlider]
    {
        if (shouldMirror("satLink"))
            mirrorSlider(left.mix, right.mix);
    };
    right.mix.onValueChange = [shouldMirror, &left, &right, mirrorSlider]
    {
        if (shouldMirror("satLink"))
            mirrorSlider(right.mix, left.mix);
    };
    left.outputTrim.onValueChange = [shouldMirror, &left, &right, mirrorSlider]
    {
        if (shouldMirror("satLink"))
            mirrorSlider(left.outputTrim, right.outputTrim);
    };
    right.outputTrim.onValueChange = [shouldMirror, &left, &right, mirrorSlider]
    {
        if (shouldMirror("satLink"))
            mirrorSlider(right.outputTrim, left.outputTrim);
    };
    left.satType.onChange = [shouldMirror, &left, &right, mirrorChoice]
    {
        if (shouldMirror("satLink"))
            mirrorChoice(left.satType, right.satType);
    };
    right.satType.onChange = [shouldMirror, &left, &right, mirrorChoice]
    {
        if (shouldMirror("satLink"))
            mirrorChoice(right.satType, left.satType);
    };

    startTimerHz(12);
    updateLinkedControlStates();
}

BqtAudioProcessorEditor::~BqtAudioProcessorEditor()
{
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

bool BqtAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (! mods.isCommandDown() && ! mods.isCtrlDown())
        return false;

    const auto character = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (character != 'z')
        return false;

    if (mods.isShiftDown())
        return audioProcessor.undoManager().redo();

    return audioProcessor.undoManager().undo();
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
    controls.highFreq.textFromValueFunction = [](double value) { return indexedLabel(highFreqLabels, value); };
    configureLabel(controls.lowGainLabel, "lf");
    configureSlider(controls.lowGain);
    controls.lowGain.getProperties().set("bqtLargeCreamKnob", true);
    controls.lowGain.textFromValueFunction = [](double value) { return juce::String(value, 1) + " dB"; };
    configureLabel(controls.lowFreqLabel, "freq");
    configureSlider(controls.lowFreq);
    controls.lowFreq.getProperties().set("bqtKnobCombo", true);
    controls.lowFreq.setRange(0.0, 7.0, 1.0);
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

void BqtAudioProcessorEditor::refreshPresetMenu()
{
    presetManager.refresh();
    selectedPresetIndex = juce::jlimit(0, juce::jmax(0, presetManager.getPresets().size() - 1), selectedPresetIndex);
    updatePresetButtonText();
}

void BqtAudioProcessorEditor::showPresetMenu()
{
    const auto& presets = presetManager.getPresets();
    if (presets.isEmpty())
        return;

    juce::PopupMenu menu;
    juce::PopupMenu factoryMenu;
    juce::PopupMenu userMenu;
    juce::StringArray factoryCategories;
    juce::StringArray userCategories;

    for (const auto& preset : presets)
    {
        auto& categories = preset.factory ? factoryCategories : userCategories;
        if (preset.category.isNotEmpty() && ! categories.contains(preset.category))
            categories.add(preset.category);
    }

    auto addCategoryMenus = [&presets, this](juce::PopupMenu& parent, const juce::StringArray& categories, bool factory)
    {
        for (int i = 0; i < presets.size(); ++i)
        {
            const auto& preset = presets.getReference(i);
            if (preset.factory == factory && preset.category.isEmpty())
                parent.addItem(i + 1, preset.name, true, i == selectedPresetIndex);
        }

        for (const auto& category : categories)
        {
            juce::PopupMenu categoryMenu;
            for (int i = 0; i < presets.size(); ++i)
            {
                const auto& preset = presets.getReference(i);
                if (preset.factory == factory && preset.category == category)
                    categoryMenu.addItem(i + 1, preset.name, true, i == selectedPresetIndex);
            }

            parent.addSubMenu(category, categoryMenu);
        }
    };

    addCategoryMenus(factoryMenu, factoryCategories, true);
    addCategoryMenus(userMenu, userCategories, false);

    menu.addSubMenu("Factory", factoryMenu);
    if (userMenu.getNumItems() > 0)
        menu.addSubMenu("User", userMenu);
    else
        menu.addItem(-1, "No user presets", false);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetMenuButton),
                       [this](int result)
                       {
                           if (result > 0)
                               loadPreset(result - 1);
                       });
}

void BqtAudioProcessorEditor::loadPreset(int index)
{
    if (presetManager.loadPreset(index))
    {
        selectedPresetIndex = index;
        previousInputTrimForCompensation = inputTrim.getValue();
        updatePresetButtonText();
    }
}

void BqtAudioProcessorEditor::selectRelativePreset(int offset)
{
    const auto count = presetManager.getPresets().size();
    if (count <= 0)
        return;

    loadPreset((selectedPresetIndex + offset + count) % count);
}

void BqtAudioProcessorEditor::saveUserPreset()
{
    const auto directory = presetManager.getUserPresetDirectory();
    directory.createDirectory();

    presetFileChooser = std::make_unique<juce::FileChooser>("Save BQST preset",
                                                            directory.getChildFile("New Preset.bqstpreset"),
                                                            "*.bqstpreset");
    presetFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                       | juce::FileBrowserComponent::canSelectFiles
                                       | juce::FileBrowserComponent::warnAboutOverwriting,
                                   [this](const juce::FileChooser& chooser)
                                   {
                                       const auto file = chooser.getResult();
                                       if (file == juce::File{})
                                           return;

                                       if (presetManager.saveUserPreset(file))
                                       {
                                           refreshPresetMenu();
                                           const auto savedName = file.withFileExtension(".bqstpreset").getFileNameWithoutExtension();
                                           for (int i = 0; i < presetManager.getPresets().size(); ++i)
                                           {
                                               const auto& preset = presetManager.getPresets().getReference(i);
                                               if (! preset.factory && preset.name == savedName)
                                               {
                                                   selectedPresetIndex = i;
                                                   updatePresetButtonText();
                                                   break;
                                               }
                                           }
                                       }
                                   });
}

void BqtAudioProcessorEditor::updatePresetButtonText()
{
    const auto& presets = presetManager.getPresets();
    if (presets.isEmpty())
    {
        presetMenuButton.setButtonText("No Presets");
        return;
    }

    const auto& preset = presets.getReference(juce::jlimit(0, presets.size() - 1, selectedPresetIndex));
    presetMenuButton.setButtonText(preset.name);
}

void BqtAudioProcessorEditor::timerCallback()
{
    const auto eqIsIn = audioProcessor.state().getRawParameterValue("eqBypass")->load() < 0.5f;
    const auto satIsIn = audioProcessor.state().getRawParameterValue("satBypass")->load() < 0.5f;
    const auto bypassIsOn = audioProcessor.state().getRawParameterValue("bypass")->load() > 0.5f;
    eqBypass.setToggleState(eqIsIn, juce::dontSendNotification);
    satBypass.setToggleState(satIsIn, juce::dontSendNotification);
    bypass.setToggleState(bypassIsOn, juce::dontSendNotification);
    rackComponent.setBypassed(bypassIsOn);

    for (auto& controls : sideControls)
        controls.satTypeButton.setToggleState(controls.satType.getSelectedItemIndex() == 1, juce::dontSendNotification);

    updateLinkedControlStates();
    updateRackBypassVisualState();
    updateDynamicTooltips();

    if (activeReadoutSlider != nullptr)
    {
        if (activeReadoutSlider->isMouseButtonDown())
            updateDragValueReadout(*activeReadoutSlider);
        else
            hideDragValueReadout();
    }

    syncHoverTargetsFromMouse();
    updateHoverValueReadout();
    updateTopBarHelp();

}

void BqtAudioProcessorEditor::sliderValueChanged(juce::Slider* slider)
{
    if (slider == nullptr)
        return;

    if (slider == &inputTrim)
    {
        const auto currentInputTrim = inputTrim.getValue();
        const auto deltaDb = currentInputTrim - previousInputTrimForCompensation;
        previousInputTrimForCompensation = currentInputTrim;

        if (inputTrim.isMouseButtonDown()
            && juce::ModifierKeys::currentModifiers.isCtrlDown()
            && std::abs(deltaDb) > 0.0)
        {
            const juce::ScopedValueSetter<bool> scopedMirror(isMirroringLinkedControl, true);
            for (auto& controls : sideControls)
            {
                const auto compensatedValue = juce::jlimit(controls.outputTrim.getMinimum(),
                                                          controls.outputTrim.getMaximum(),
                                                          controls.outputTrim.getValue() - deltaDb);
                controls.outputTrim.setValue(compensatedValue, juce::sendNotificationSync);
            }
        }
    }

    updateDragValueReadout(*slider);
}

void BqtAudioProcessorEditor::sliderDragStarted(juce::Slider* slider)
{
    if (slider == nullptr)
        return;

    audioProcessor.undoManager().beginNewTransaction("Adjust " + slider->getName());
    hideHoverValueReadout();

    if (slider == &inputTrim)
        previousInputTrimForCompensation = inputTrim.getValue();

    updateDragValueReadout(*slider);
}

void BqtAudioProcessorEditor::sliderDragEnded(juce::Slider*)
{
    hideDragValueReadout();
}

void BqtAudioProcessorEditor::mouseEnter(const juce::MouseEvent& event)
{
    auto* component = event.originalComponent;
    while (component != nullptr && component != this)
    {
        if (component->getProperties().contains("bqtHelpText"))
        {
            hideHoverValueReadout();
            hoveredHelpComponent = component;
            hoveredHelpText = component->getProperties()["bqtHelpText"].toString();
            helpHoverStartMs = juce::Time::getMillisecondCounter();
            helpVisible = false;
            return;
        }

        component = component->getParentComponent();
    }

    if (auto* slider = dynamic_cast<juce::Slider*>(event.originalComponent))
    {
        hideTopBarHelp();
        hoveredReadoutSlider = slider;
        hoverReadoutStartMs = juce::Time::getMillisecondCounter();
        hoverReadoutVisible = false;
    }
}

void BqtAudioProcessorEditor::mouseMove(const juce::MouseEvent& event)
{
    auto* component = event.originalComponent;
    while (component != nullptr && component != this)
    {
        if (component->getProperties().contains("bqtHelpText"))
        {
            if (hoveredHelpComponent != component)
            {
                hideTopBarHelp();
                hoveredHelpComponent = component;
                hoveredHelpText = component->getProperties()["bqtHelpText"].toString();
            }

            helpHoverStartMs = juce::Time::getMillisecondCounter();
            return;
        }

        component = component->getParentComponent();
    }

    if (auto* slider = dynamic_cast<juce::Slider*>(event.originalComponent))
    {
        if (hoveredReadoutSlider != slider)
        {
            hoveredReadoutSlider = slider;
            hoverReadoutVisible = false;
        }

        hoverReadoutStartMs = juce::Time::getMillisecondCounter();

        if (hoverReadoutVisible)
            hideHoverValueReadout();
    }
}

void BqtAudioProcessorEditor::mouseExit(const juce::MouseEvent& event)
{
    if (event.originalComponent == hoveredReadoutSlider)
        hideHoverValueReadout();

    auto* component = event.originalComponent;
    while (component != nullptr && component != this)
    {
        if (component == hoveredHelpComponent)
        {
            hideTopBarHelp();
            return;
        }

        component = component->getParentComponent();
    }
}

void BqtAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (! event.mods.isCtrlDown())
        return;

    auto* component = event.originalComponent;
    while (component != nullptr && component != this)
    {
        if (component == &inputTrim)
        {
            const juce::ScopedValueSetter<bool> scopedMirror(isMirroringLinkedControl, true);
            for (auto& controls : sideControls)
                controls.outputTrim.setValue(0.0, juce::sendNotificationSync);

            previousInputTrimForCompensation = 0.0;
            return;
        }

        component = component->getParentComponent();
    }
}

void BqtAudioProcessorEditor::updateDragValueReadout(juce::Slider& slider)
{
    if (! slider.isMouseButtonDown())
        return;

    activeReadoutSlider = &slider;
    juce::Component::SafePointer<juce::Slider> safeSlider(&slider);
    juce::MessageManager::callAsync([this, safeSlider]
    {
        if (safeSlider == nullptr || activeReadoutSlider != safeSlider.getComponent())
            return;

        auto& currentSlider = *safeSlider;
        if (! currentSlider.isMouseButtonDown())
            return;

        showReadout(currentSlider, currentSlider.getTextFromValue(currentSlider.getValue()));
    });
}

void BqtAudioProcessorEditor::hideDragValueReadout()
{
    activeReadoutSlider = nullptr;
    if (hoveredReadoutSlider == nullptr && hoveredHelpComponent == nullptr)
        hideReadout();
}

void BqtAudioProcessorEditor::syncHoverTargetsFromMouse()
{
    if (activeReadoutSlider != nullptr || juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        return;

    auto* component = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();

    auto& mainControls = sideControls[0];
    if (mainControls.satTypeButton.isParentOf(component) || component == &mainControls.satTypeButton)
    {
        const auto gritSelected = mainControls.satType.getSelectedItemIndex() == 1;
        auto helpText = mainControls.satTypeButton.getProperties()[gritSelected ? "bqtGritHelp" : "bqtCreamHelp"].toString();

        if (hoveredHelpComponent != &mainControls.satTypeButton || hoveredHelpText != helpText)
        {
            hideHoverValueReadout();
            hoveredHelpComponent = &mainControls.satTypeButton;
            hoveredHelpText = helpText;
            helpHoverStartMs = juce::Time::getMillisecondCounter();
            helpVisible = false;
        }

        return;
    }

    auto* helpComponent = component;
    while (helpComponent != nullptr && helpComponent != this)
    {
        if (helpComponent->getProperties().contains("bqtHelpText"))
            break;

        helpComponent = helpComponent->getParentComponent();
    }

    if (helpComponent != nullptr && helpComponent != this)
    {
        if (hoveredHelpComponent != helpComponent)
        {
            hideHoverValueReadout();
            hoveredHelpComponent = helpComponent;
            hoveredHelpText = helpComponent->getProperties()["bqtHelpText"].toString();
            helpHoverStartMs = juce::Time::getMillisecondCounter();
            helpVisible = false;
        }

        return;
    }

    if (hoveredHelpComponent != nullptr)
        hideTopBarHelp();

    auto* sliderComponent = component;
    while (sliderComponent != nullptr && sliderComponent != this)
    {
        if (auto* slider = dynamic_cast<juce::Slider*>(sliderComponent))
        {
            if (hoveredReadoutSlider != slider)
            {
                hideHoverValueReadout();
                hoveredReadoutSlider = slider;
                hoverReadoutStartMs = juce::Time::getMillisecondCounter();
                hoverReadoutVisible = false;
            }

            return;
        }

        sliderComponent = sliderComponent->getParentComponent();
    }

    if (hoveredReadoutSlider != nullptr)
        hideHoverValueReadout();
}

void BqtAudioProcessorEditor::updateHoverValueReadout()
{
    if (hoveredReadoutSlider == nullptr || activeReadoutSlider != nullptr)
        return;

    if (hoveredReadoutSlider->isMouseButtonDown())
        return;

    if (! hoveredReadoutSlider->isMouseOver())
    {
        hideHoverValueReadout();
        return;
    }

    constexpr juce::uint32 hoverDelayMs = 300;
    if (! hoverReadoutVisible
        && juce::Time::getMillisecondCounter() - hoverReadoutStartMs >= hoverDelayMs)
    {
        showReadout(*hoveredReadoutSlider, hoveredReadoutSlider->getTextFromValue(hoveredReadoutSlider->getValue()));
        hoverReadoutVisible = true;
    }
}

void BqtAudioProcessorEditor::hideHoverValueReadout()
{
    if (activeReadoutSlider == nullptr && hoveredHelpComponent == nullptr)
        hideReadout();

    hoveredReadoutSlider = nullptr;
    hoverReadoutVisible = false;
}

void BqtAudioProcessorEditor::updateTopBarHelp()
{
    if (hoveredHelpComponent == nullptr || activeReadoutSlider != nullptr)
        return;

    if (! hoveredHelpComponent->isMouseOver(true))
    {
        hideTopBarHelp();
        return;
    }

    constexpr juce::uint32 hoverDelayMs = 300;
    if (! helpVisible
        && juce::Time::getMillisecondCounter() - helpHoverStartMs >= hoverDelayMs)
    {
        showReadout(*hoveredHelpComponent, hoveredHelpText);
        helpVisible = true;
    }
}

void BqtAudioProcessorEditor::hideTopBarHelp()
{
    if (activeReadoutSlider == nullptr && ! hoverReadoutVisible)
        hideReadout();

    hoveredHelpComponent = nullptr;
    hoveredHelpText.clear();
    helpVisible = false;
}

void BqtAudioProcessorEditor::setTopBarHelp(juce::Component& component, const juce::String& text)
{
    if (auto* slider = dynamic_cast<juce::Slider*>(&component))
        slider->setTooltip({});
    else if (auto* combo = dynamic_cast<juce::ComboBox*>(&component))
        combo->setTooltip({});
    else if (auto* button = dynamic_cast<juce::Button*>(&component))
        button->setTooltip({});

    component.getProperties().set("bqtHelpText", text);
    if (dynamic_cast<juce::Slider*>(&component) == nullptr)
        component.addMouseListener(this, true);
}

void BqtAudioProcessorEditor::showReadout(juce::Component& target, const juce::String& text)
{
    if (text.isEmpty())
        return;

    const auto font = juce::Font(faceFont(16.5f, true));
    const auto width = static_cast<int>(std::ceil(getTextWidth(font, text) + 22.0f));
    constexpr int height = 31;
    const auto uiScale = static_cast<float>(getWidth()) / static_cast<float>(baseEditorWidth);
    auto editorTargetBounds = getLocalArea(&target, target.getLocalBounds()).toFloat();
    if (uiScale > 0.0f)
        editorTargetBounds = editorTargetBounds / uiScale;

    auto bounds = juce::Rectangle<int>(width, height)
                      .withCentre({ static_cast<int>(std::round(editorTargetBounds.getCentreX())),
                                    static_cast<int>(std::round(editorTargetBounds.getY() - 8.0f)) });

    if (bounds.getY() < 4)
        bounds.setY(static_cast<int>(std::round(editorTargetBounds.getBottom() + 8.0f)));

    readoutBubble.setText(text);
    readoutBubble.setBounds(bounds.constrainedWithin({ 0, 0, baseEditorWidth, baseEditorHeight }));
    readoutBubble.setVisible(true);
    readoutBubble.toFront(false);
}

void BqtAudioProcessorEditor::hideReadout()
{
    readoutBubble.setVisible(false);
}

void BqtAudioProcessorEditor::updateDynamicTooltips()
{
}

void BqtAudioProcessorEditor::updateLinkedControlStates()
{
    auto& right = sideControls[1];

    auto keepVisible = [](juce::Component& component)
    {
        component.setEnabled(true);
        component.setAlpha(1.0f);
    };

    keepVisible(right.lowGain);
    keepVisible(right.lowFreq);
    keepVisible(right.highGain);
    keepVisible(right.highFreq);
    keepVisible(right.lowGainLabel);
    keepVisible(right.lowFreqLabel);
    keepVisible(right.highGainLabel);
    keepVisible(right.highFreqLabel);
    keepVisible(right.drive);
    keepVisible(right.satType);
    keepVisible(right.satTypeButton);
    keepVisible(right.mix);
    keepVisible(right.outputTrim);
    keepVisible(right.driveLabel);
    keepVisible(right.satTypeLabel);
    keepVisible(right.mixLabel);
    keepVisible(right.outputTrimLabel);
}

void BqtAudioProcessorEditor::updateRackBypassVisualState()
{
    const auto bypassed = rackComponent.isBypassed();

    bypassOverlay.setVisible(bypassed);
    bypassOverlay.toFront(false);
    bypassOverlay.repaint();
}

void BqtAudioProcessorEditor::paint(juce::Graphics& g)
{
    const auto uiScale = static_cast<float>(getWidth()) / static_cast<float>(baseEditorWidth);
    juce::Graphics::ScopedSaveState savedState(g);
    g.addTransform(juce::AffineTransform::scale(uiScale));

    g.fillAll(juce::Colour(0xfff5f2ef));

    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(baseEditorWidth),
                                         static_cast<float>(baseEditorHeight)).reduced(static_cast<float>(outerEditorMargin));
    auto headerSlot = bounds.removeFromTop(104.0f);
    auto rackArea = bounds;
    rackArea.removeFromTop(10.0f);
    auto rack = getRackFaceBounds(rackArea);
    auto header = rack.withY(headerSlot.getY()).withHeight(headerSlot.getHeight());
    auto presetBar = header.removeFromTop(48.0f);
    auto top = header.removeFromTop(56.0f);

    g.setColour(juce::Colour(0xff1b1b1b));
    g.fillRoundedRectangle(presetBar.getUnion(top), 5.0f);
    g.setColour(juce::Colour(panelText));
    g.setFont(juce::Font(faceFont(27.0f, true)));
    g.drawText("bqst " JucePlugin_VersionString,
               presetBar.withTrimmedLeft(14.0f).withTrimmedTop(5.0f).withHeight(34.0f).toNearestInt(),
               juce::Justification::centredLeft);

}

void BqtAudioProcessorEditor::paintRack(juce::Graphics& g)
{
    auto rack = rackComponent.getLocalBounds().toFloat();
    auto eqPanel = rack.removeFromLeft(rack.getWidth() * 0.5f);
    auto satPanel = rack;

    auto drawPlate = [&g](juce::Rectangle<float> plate, const juce::String& title)
    {
        juce::ColourGradient plateGradient(juce::Colour(panelPink).brighter(0.04f), plate.getX(), plate.getY(),
                                           juce::Colour(panelPinkDark).brighter(0.02f), plate.getX(), plate.getBottom(), false);
        g.setGradientFill(plateGradient);
        g.fillRect(plate);

        juce::Random grainRandom(0xB071500);
        for (int i = 0; i < 1600; ++i)
        {
            const auto x = plate.getX() + grainRandom.nextFloat() * plate.getWidth();
            const auto y = plate.getY() + grainRandom.nextFloat() * plate.getHeight();
            const auto size = 0.25f + grainRandom.nextFloat() * 0.85f;
            const auto bright = grainRandom.nextBool();
            const auto alpha = 0.013f + grainRandom.nextFloat() * 0.034f;
            g.setColour(bright ? juce::Colours::white.withAlpha(alpha)
                               : juce::Colours::black.withAlpha(alpha * 0.72f));
            g.fillEllipse(x, y, size, size);
        }

        for (int i = 0; i < 230; ++i)
        {
            const auto y = plate.getY() + grainRandom.nextFloat() * plate.getHeight();
            const auto x = plate.getX() + grainRandom.nextFloat() * plate.getWidth() * 0.10f;
            const auto width = plate.getWidth() * (0.35f + grainRandom.nextFloat() * 0.72f);
            const auto alpha = 0.014f + grainRandom.nextFloat() * 0.024f;
            g.setColour((i % 3 == 0 ? juce::Colours::white : juce::Colours::black).withAlpha(alpha));
            g.drawLine(x, y, juce::jmin(plate.getRight(), x + width), y + grainRandom.nextFloat() * 0.25f, 0.45f);
        }

        juce::ColourGradient satin(juce::Colours::white.withAlpha(0.10f), plate.getX(), plate.getY(),
                                   juce::Colours::black.withAlpha(0.07f), plate.getRight(), plate.getBottom(), false);
        g.setGradientFill(satin);
        g.fillRect(plate);

        g.setColour(juce::Colours::black.withAlpha(0.08f));
        g.drawRect(plate.reduced(4.0f), 1.0f);

        g.setColour(juce::Colour(ink));
        g.drawRect(plate, 3.0f);

        const auto screwColour = juce::Colour(0xff545454);
        const auto screwSize = 14.0f;
        const auto screwInsetY = plate.getHeight() * (screwInsetYInches / panelHeightInches);
        const auto screwX1 = plate.getX() + plate.getWidth() * 0.25f;
        const auto screwX2 = plate.getX() + plate.getWidth() * 0.75f;
        const auto screwY1 = plate.getY() + screwInsetY;
        const auto screwY2 = plate.getBottom() - screwInsetY;

        for (auto screwCentre : { juce::Point<float>(screwX1, screwY1),
                                  juce::Point<float>(screwX2, screwY1),
                                  juce::Point<float>(screwX1, screwY2),
                                  juce::Point<float>(screwX2, screwY2) })
        {
            const auto screw = juce::Rectangle<float>(screwSize, screwSize).withCentre(screwCentre);
            g.setColour(juce::Colours::black.withAlpha(0.22f));
            g.fillEllipse(screw.translated(1.0f, 1.5f));
            g.setColour(screwColour);
            g.fillEllipse(screw);
        }

        const auto brand = plate.withTrimmedLeft(8.0f).withTrimmedTop(6.0f).withWidth(102.0f).withHeight(78.0f);
        drawPanelText(g, "ebr", brand.withY(brand.getY()).withHeight(30.0f), juce::Justification::topLeft, 27.0f);
        drawPanelText(g, "audio", brand.withY(brand.getY() + 19.0f).withHeight(30.0f), juce::Justification::topLeft, 27.0f);
        drawPanelText(g, "tech", brand.withY(brand.getY() + 38.0f).withHeight(30.0f), juce::Justification::topLeft, 27.0f);
        drawPanelText(g, title, plate.withTrimmedRight(8.0f).withTrimmedTop(6.0f).withHeight(32.0f),
                      juce::Justification::topRight, 27.0f);
    };

    drawPlate(eqPanel, "baxq");
    drawPlate(satPanel, "ttsat");

    auto drawScaleRing = [&g](const juce::Slider& slider, const juce::String& leftText,
                              const juce::String& rightText, bool showNumbers)
    {
        const auto sliderBounds = slider.getBounds().toFloat();
        if (sliderBounds.isEmpty())
            return;

        const auto centre = sliderBounds.getCentre();
        const auto radius = juce::jmin(sliderBounds.getWidth(), sliderBounds.getHeight()) * 0.47f;
        const auto start = juce::degreesToRadians(220.0f);
        const auto end = juce::degreesToRadians(500.0f);

        g.setColour(juce::Colour(panelText));
        g.drawEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre), 1.2f);

        for (int i = 0; i <= 20; ++i)
        {
            const auto p = static_cast<float>(i) / 20.0f;
            const auto angle = start + p * (end - start);
            const auto isMajor = i % 5 == 0;
            const auto inner = radius - (isMajor ? 16.0f : 10.0f);
            const auto outer = radius + 0.5f;
            const auto p1 = centre.getPointOnCircumference(inner, angle);
            const auto p2 = centre.getPointOnCircumference(outer, angle);
            g.drawLine({ p1, p2 }, isMajor ? 2.1f : 1.35f);
        }

        if (showNumbers)
        {
            const auto leftPoint = centre.getPointOnCircumference(radius + 12.0f, start);
            const auto rightPoint = centre.getPointOnCircumference(radius + 12.0f, end);
            drawPanelText(g, leftText,
                          juce::Rectangle<float>(48.0f, 24.0f).withCentre(leftPoint + juce::Point<float>(-8.0f, 3.0f)),
                          juce::Justification::centred, 19.0f);
            drawPanelText(g, rightText,
                          juce::Rectangle<float>(48.0f, 24.0f).withCentre(rightPoint + juce::Point<float>(8.0f, 3.0f)),
                          juce::Justification::centred, 19.0f);
        }
    };

    for (const auto& controls : sideControls)
    {
        drawScaleRing(controls.highGain, "-6", "+6", true);
        drawScaleRing(controls.lowGain, "-6", "+6", true);
        drawScaleRing(controls.drive, "", "", false);
    }

    auto drawChoiceLabels = [&g](const juce::Slider& slider, const juce::StringArray& labels)
    {
        const auto b = slider.getBounds().toFloat();
        if (b.isEmpty())
            return;

        const auto centre = b.getCentre();
        const auto count = juce::jmax(1, labels.size());
        const auto start = juce::degreesToRadians(220.0f);
        const auto end = juce::degreesToRadians(500.0f);
        const auto knobRadius = juce::jmin(b.getWidth(), b.getHeight()) * 0.36f;
        const auto labelGap = -5.0f;
        const auto font = juce::Font(faceFont(16.0f, true));
        g.setFont(font);
        g.setColour(juce::Colour(panelText));

        for (int i = 0; i < count; ++i)
        {
            const auto p = count == 1 ? 0.5f : static_cast<float>(i) / static_cast<float>(count - 1);
            const auto angle = start + p * (end - start);
            const auto textWidth = getTextWidth(font, labels[i]) + 4.0f;
            const auto textHeight = 20.0f;
            const auto radialHalfExtent = std::abs(std::sin(angle)) * textWidth * 0.5f
                                        + std::abs(std::cos(angle)) * textHeight * 0.5f;
            const auto point = centre.getPointOnCircumference(knobRadius + labelGap + radialHalfExtent, angle);
            g.drawText(labels[i], juce::Rectangle<float>(textWidth, textHeight).withCentre(point).toNearestInt(),
                       juce::Justification::centred);
        }
    };

    for (const auto& controls : sideControls)
    {
        drawChoiceLabels(controls.highFreq, highFreqLabels);
        drawChoiceLabels(controls.lowFreq, lowFreqLabels);
    }

    auto drawEqChannelText = [&g](const juce::Slider& slider, const juce::String& text)
    {
        const auto b = slider.getBounds().toFloat();
        drawPanelText(g, text, juce::Rectangle<float>(110.0f, 34.0f)
                                .withCentre({ b.getCentreX(), b.getY() - 20.0f }),
                      juce::Justification::centred, 27.0f);
    };

    drawEqChannelText(sideControls[0].highGain, "l/m");
    drawEqChannelText(sideControls[1].highGain, "r/s");

    auto drawEqCenterText = [&g](const juce::Slider& left, const juce::Slider& right, const juce::String& text, float yOffset)
    {
        const auto x = (left.getBounds().getCentreX() + right.getBounds().getCentreX()) * 0.5f;
        const auto y = (left.getY() + right.getY()) * 0.5f + yOffset;
        drawPanelText(g, text, juce::Rectangle<float>(120.0f, 42.0f).withCentre({ x, y }),
                      juce::Justification::centred, 34.0f);
    };

    drawEqCenterText(sideControls[0].highGain, sideControls[1].highGain, "hf", -18.0f);
    drawEqCenterText(sideControls[0].highFreq, sideControls[1].lowFreq, "freq", 52.0f);
    drawEqCenterText(sideControls[0].lowGain, sideControls[1].lowGain, "lf", -18.0f);

    auto drawSatChannelText = [&g](const juce::Component& meter, const juce::String& text)
    {
        const auto b = meter.getBounds().toFloat();
        drawPanelText(g, text, juce::Rectangle<float>(110.0f, 34.0f)
                                .withCentre({ b.getCentreX(), b.getY() + 7.0f }),
                      juce::Justification::centred, 27.0f);
    };

    drawSatChannelText(meterA, "l/m");
    drawSatChannelText(meterB, "r/s");

    auto drawSatKnobText = [&g](const juce::Slider& slider, const juce::String& text)
    {
        const auto b = slider.getBounds().toFloat();
        drawPanelText(g, text, juce::Rectangle<float>(132.0f, 42.0f)
                                .withCentre({ b.getCentreX(), b.getBottom() + (text == "drive" ? 13.0f : -2.0f) }),
                      juce::Justification::centred, text == "drive" ? 31.0f : 30.0f);
    };

    for (const auto& controls : sideControls)
    {
        drawSatKnobText(controls.drive, "drive");
        drawSatKnobText(controls.mix, "mix");
        drawSatKnobText(controls.outputTrim, "output");
    }
}

void BqtAudioProcessorEditor::RackComponent::setBypassed(bool shouldBeBypassed)
{
    if (bypassed == shouldBeBypassed)
        return;

    bypassed = shouldBeBypassed;
    editor.updateRackBypassVisualState();
}

void BqtAudioProcessorEditor::resized()
{
    const auto uiScale = static_cast<float>(getWidth()) / static_cast<float>(baseEditorWidth);

    auto applyUiScale = [uiScale](juce::Component& component)
    {
        component.setTransform(juce::AffineTransform::scale(uiScale));
    };

    for (auto* component : { static_cast<juce::Component*>(&rackComponent),
                             static_cast<juce::Component*>(&presetPrevious), static_cast<juce::Component*>(&presetMenuButton),
                             static_cast<juce::Component*>(&presetNext), static_cast<juce::Component*>(&presetSave),
                             static_cast<juce::Component*>(&inputTrimLabel), static_cast<juce::Component*>(&inputTrim),
                             static_cast<juce::Component*>(&eqMode), static_cast<juce::Component*>(&satMode),
                             static_cast<juce::Component*>(&osRealtime), static_cast<juce::Component*>(&osRender),
                             static_cast<juce::Component*>(&autoGain), static_cast<juce::Component*>(&eqBypass),
                             static_cast<juce::Component*>(&satBypass), static_cast<juce::Component*>(&eqLink),
                             static_cast<juce::Component*>(&satLink),
                             static_cast<juce::Component*>(&bypass), static_cast<juce::Component*>(&sizeSelect),
                             static_cast<juce::Component*>(&bypassOverlay), static_cast<juce::Component*>(&readoutBubble) })
        applyUiScale(*component);

    auto bounds = juce::Rectangle<int>(0, 0, baseEditorWidth, baseEditorHeight).reduced(outerEditorMargin);
    auto headerSlot = bounds.removeFromTop(104);
    auto rackArea = bounds;
    rackArea.removeFromTop(10);
    auto header = getRackFaceBounds(rackArea.toFloat()).toNearestInt()
                      .withY(headerSlot.getY())
                      .withHeight(headerSlot.getHeight());
    auto presetBar = header.removeFromTop(48).reduced(8, 7);
    auto top = header.removeFromTop(56).reduced(8, 9);
    constexpr int topControlHeight = 36;

    constexpr int topGap = 4;
    auto topSlot = [](juce::Rectangle<int> area) { return area.withSizeKeepingCentre(area.getWidth(), topControlHeight); };

    sizeSelect.setBounds(topSlot(presetBar.removeFromRight(82)));
    constexpr int presetPreviousWidth = 38;
    constexpr int presetMenuWidth = 320;
    constexpr int presetNextWidth = 38;
    constexpr int presetSaveWidth = 70;
    auto presetButtonBounds = juce::Rectangle<int>(presetMenuWidth, presetBar.getHeight())
                                  .withCentre({ baseEditorWidth / 2, presetBar.getCentreY() });
    presetMenuButton.setBounds(topSlot(presetButtonBounds));
    presetPrevious.setBounds(topSlot({ presetButtonBounds.getX() - topGap - presetPreviousWidth,
                                       presetBar.getY(),
                                       presetPreviousWidth,
                                       presetBar.getHeight() }));
    presetNext.setBounds(topSlot({ presetButtonBounds.getRight() + topGap,
                                   presetBar.getY(),
                                   presetNextWidth,
                                   presetBar.getHeight() }));
    presetSave.setBounds(topSlot({ presetNext.getRight() + topGap,
                                   presetBar.getY(),
                                   presetSaveWidth,
                                   presetBar.getHeight() }));

    constexpr int inputLabelWidth = 47;
    constexpr int inputControlWidth = 48;
    constexpr int numTopButtons = 8;
    int topButtonWidths[] = { 80, 70, 80, 70, 112, 112, 78, 70 };
    int preferredTopWidth = inputLabelWidth + inputControlWidth + (numTopButtons * topGap);
    for (const auto width : topButtonWidths)
        preferredTopWidth += width;

    auto extraTopWidth = juce::jmax(0, top.getWidth() - preferredTopWidth);
    for (auto i = 0; i < numTopButtons; ++i)
        topButtonWidths[i] += extraTopWidth / numTopButtons + (i < extraTopWidth % numTopButtons ? 1 : 0);

    inputTrimLabel.setBounds(topSlot(top.removeFromLeft(inputLabelWidth)));
    inputTrim.setBounds(topSlot(top.removeFromLeft(inputControlWidth)));
    top.removeFromLeft(topGap);
    eqMode.setBounds(topSlot(top.removeFromLeft(topButtonWidths[0])));
    top.removeFromLeft(topGap);
    eqLink.setBounds(topSlot(top.removeFromLeft(topButtonWidths[1])));
    top.removeFromLeft(topGap);
    satMode.setBounds(topSlot(top.removeFromLeft(topButtonWidths[2])));
    top.removeFromLeft(topGap);
    satLink.setBounds(topSlot(top.removeFromLeft(topButtonWidths[3])));
    top.removeFromLeft(topGap);
    osRealtime.setBounds(topSlot(top.removeFromLeft(topButtonWidths[4])));
    top.removeFromLeft(topGap);
    osRender.setBounds(topSlot(top.removeFromLeft(topButtonWidths[5])));
    top.removeFromLeft(topGap);
    autoGain.setBounds(topSlot(top.removeFromLeft(topButtonWidths[6])));
    top.removeFromLeft(topGap);
    bypass.setBounds(topSlot(top.removeFromLeft(topButtonWidths[7])));
    eqBypass.setBounds(-2000, -2000, 1, 1);
    satBypass.setBounds(-2000, -2000, 1, 1);

    bounds.removeFromTop(10);
    auto rackFloat = getRackFaceBounds(bounds.toFloat());
    auto rack = rackFloat.toNearestInt();
    rackComponent.setBounds(rack);
    bypassOverlay.setBounds(rack);
    auto rackLocal = juce::Rectangle<int>(0, 0, rack.getWidth(), rack.getHeight());
    auto eqPanel = rackLocal.removeFromLeft(rackLocal.getWidth() / 2).reduced(24, 26);
    auto satPanel = rackLocal.reduced(24, 26);
    const auto satPanelBounds = satPanel;

    auto& main = sideControls[0];
    auto& slave = sideControls[1];

    eqPanel.removeFromTop(46);
    const auto eqColW = (eqPanel.getWidth() - 24) / 2;
    auto eqLeft = eqPanel.removeFromLeft(eqColW);
    eqPanel.removeFromLeft(24);
    auto eqRight = eqPanel.removeFromLeft(eqColW);

    auto layoutEqColumn = [](SideControls& controls, juce::Rectangle<int> col)
    {
        controls.highGainLabel.setBounds(-2000, -2000, 1, 1);
        controls.highGain.setBounds(col.getCentreX() - 88, col.getY() + 0, 176, 176);
        controls.highFreqLabel.setBounds(-2000, -2000, 1, 1);
        controls.highFreq.setBounds(col.getCentreX() - 53, col.getY() + 193, 106, 106);

        controls.lowGain.setBounds(col.getCentreX() - 88, col.getY() + 436, 176, 176);
        controls.lowGainLabel.setBounds(-2000, -2000, 1, 1);
        controls.lowFreqLabel.setBounds(-2000, -2000, 1, 1);
        controls.lowFreq.setBounds(col.getCentreX() - 53, col.getY() + 313, 106, 106);
    };

    layoutEqColumn(main, eqLeft);
    layoutEqColumn(slave, eqRight);
    main.eqSectionLabel.setBounds(-2000, -2000, 1, 1);
    slave.eqSectionLabel.setBounds(-2000, -2000, 1, 1);
    main.eqSectionLabel.setJustificationType(juce::Justification::centred);
    slave.eqSectionLabel.setJustificationType(juce::Justification::centred);

    main.highGainLabel.setBounds(-2000, -2000, 1, 1);
    main.lowGainLabel.setBounds(-2000, -2000, 1, 1);
    main.eqSectionLabel.setFont(faceFont(27.0f));
    slave.eqSectionLabel.setFont(faceFont(27.0f));
    main.highGainLabel.setFont(faceFont(64.0f));
    main.lowGainLabel.setFont(faceFont(64.0f));

    main.highFreqLabel.setBounds(-2000, -2000, 1, 1);
    main.lowFreqLabel.setBounds(-2000, -2000, 1, 1);
    main.highFreqLabel.setText("freq", juce::dontSendNotification);
    main.lowFreqLabel.setText("freq", juce::dontSendNotification);
    main.highFreqLabel.setFont(faceFont(64.0f));
    main.lowFreqLabel.setFont(faceFont(64.0f));

    satPanel.removeFromTop(46);

    const auto satColW = (satPanel.getWidth() - 24) / 2;
    auto satLeft = satPanel.withTrimmedTop(278).removeFromLeft(satColW);
    auto satRight = satPanel.withTrimmedTop(278).withTrimmedLeft(satColW + 24);
    const auto leftDriveX = satLeft.getCentreX();
    const auto rightDriveX = satRight.getCentreX();

    main.satSectionLabel.setBounds(-2000, -2000, 1, 1);
    slave.satSectionLabel.setBounds(-2000, -2000, 1, 1);

    const auto meterTop = satPanel.getY() - 32;
    constexpr int vuSize = 286;
    const auto screwX1 = satPanel.getX() + satPanel.getWidth() * 0.25f;
    const auto screwX2 = satPanel.getX() + satPanel.getWidth() * 0.75f;
    const auto previousVuSize = 272.0f;
    const auto leftMeterX = screwX1 - previousVuSize * 0.5f - 13.0f;
    const auto rightMeterRight = screwX2 + previousVuSize * 0.5f + 13.0f;
    meterA.setBounds(static_cast<int>(std::round(leftMeterX)), meterTop, vuSize, vuSize);
    meterB.setBounds(static_cast<int>(std::round(rightMeterRight - vuSize)), meterTop, vuSize, vuSize);

    vintage.setBounds(leftDriveX - 72, satPanel.getY() + 201, 144, 44);
    main.satTypeLabel.setBounds(-2000, -2000, 1, 1);
    main.satType.setBounds(-2000, -2000, 1, 1);
    main.satTypeButton.setBounds(rightDriveX - 72, satPanel.getY() + 195, 144, 56);
    slave.satTypeLabel.setBounds(-2000, -2000, 1, 1);
    slave.satType.setBounds(-2000, -2000, 1, 1);
    slave.satTypeButton.setBounds(-2000, -2000, 1, 1);
    juce::ignoreUnused(satPanelBounds);

    auto layoutSatColumn = [](SideControls& controls, juce::Rectangle<int> col)
    {
        controls.drive.setBounds(col.getCentreX() - 88, col.getY() - 18, 176, 176);
        controls.driveLabel.setBounds(-2000, -2000, 1, 1);
        controls.mix.setBounds(col.getCentreX() - 96, col.getY() + 180, 106, 106);
        controls.mixLabel.setBounds(-2000, -2000, 1, 1);
        controls.outputTrim.setBounds(col.getCentreX() - 10, col.getY() + 180, 106, 106);
        controls.outputTrimLabel.setBounds(-2000, -2000, 1, 1);
    };

    layoutSatColumn(main, satLeft);
    layoutSatColumn(slave, satRight);

    vintage.toFront(false);
    main.satTypeButton.toFront(false);
    bypassOverlay.toFront(false);
    readoutBubble.toFront(false);
}
