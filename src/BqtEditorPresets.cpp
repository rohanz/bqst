#include "PluginEditor.h"

void BqtAudioProcessorEditor::refreshPresetMenu()
{
    presetManager.refresh();

    // Re-resolve the selection by its stable key so it follows the preset even when the list
    // was rebuilt (e.g. another user preset was added/removed). Fall back to a clamped index
    // only if the selected preset no longer exists.
    const auto resolved = presetManager.findPreset(selectedPresetKey);
    selectedPresetIndex = resolved >= 0
                              ? resolved
                              : juce::jlimit(0, juce::jmax(0, presetManager.getPresets().size() - 1), selectedPresetIndex);
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
        selectedPresetKey = presetManager.getPresetKey(index);
        inputTrimCompensationStart = inputTrim.getValue();
        for (size_t side = 0; side < sideControls.size(); ++side)
            outputTrimCompensationStart[side] = sideControls[side].outputTrim.getValue();
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
                                                   selectedPresetKey = presetManager.getPresetKey(i);
                                                   updatePresetButtonText();
                                                   break;
                                               }
                                           }
                                       }
                                       else
                                       {
                                           juce::NativeMessageBox::showMessageBoxAsync(
                                               juce::MessageBoxIconType::WarningIcon,
                                               "Preset Not Saved",
                                               "BQST could not write the preset to:\n"
                                                   + file.withFileExtension(".bqstpreset").getFullPathName(),
                                               this);
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
