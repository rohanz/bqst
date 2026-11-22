// Test-only tool: renders the real BQST editor to a PNG with no window on screen.
//
//   BqstEditorSnapshot <width> <out.png> [pixelScale]
//   BqstEditorSnapshot menu <out.png> [pixelScale]
//   BqstEditorSnapshot tips <out.png> [pixelScale]
//   BqstEditorSnapshot bar <out.png> [pixelScale]     (each bar control: resting, hover, pressed, on)
//   BqstEditorSnapshot <width> <out.png> <pixelScale> about
//
// "menu" draws two open dropdown menus instead (the realtime oversampling choices with a row
// under the pointer, and the preset menu's top level) through the editor's own look-and-feel,
// laid out the way JUCE lays out a popup. A live popup needs a window on screen, so this paints
// the same calls into an image instead.
//
// The editor is created from a fresh processor (default state), sized to <width> logical pixels
// (height follows the editor's own aspect ratio), and painted offscreen via
// createComponentSnapshot. Nothing is added to the desktop: no focus, no cursor, no host.
// pixelScale (default 1) renders more pixels per logical pixel, like a Retina display.

#include "../src/BqtEditorStyle.h"
#include "../src/BqtVuMeter.h"
#include "../src/PluginEditor.h"
#include "../src/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <functional>
#include <iostream>

namespace
{
struct MenuRow
{
    juce::String text;
    bool active = true, ticked = false, subMenu = false, highlighted = false;
};

// Paints one menu the way PopupMenu's window does: background, then each item in a column inset
// by the border, items sized by getIdealPopupMenuItemSize, at least minWidth wide.
juce::Image paintMenu(juce::LookAndFeel& lf, const std::vector<MenuRow>& rows, int minWidth, float pixelScale)
{
    const auto border = lf.getPopupMenuBorderSize();
    int width = minWidth - 2 * border, height = 0;
    std::vector<int> heights;
    for (const auto& row : rows)
    {
        int w = 0, h = 0;
        lf.getIdealPopupMenuItemSize(row.text, false, BqtHardwareLookAndFeel::menuRowHeight, w, h);
        width = juce::jmax(width, w);
        heights.push_back(h);
        height += h;
    }

    const auto menuWidth = width + 2 * border, menuHeight = height + 2 * border;
    juce::Image image(juce::Image::ARGB, juce::roundToInt(static_cast<float>(menuWidth) * pixelScale),
                      juce::roundToInt(static_cast<float>(menuHeight) * pixelScale), true);
    juce::Graphics g(image);
    g.addTransform(juce::AffineTransform::scale(pixelScale));
    lf.drawPopupMenuBackground(g, menuWidth, menuHeight);
    auto y = border;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& row = rows[i];
        lf.drawPopupMenuItem(g, { border, y, width, heights[i] }, false, row.active, row.highlighted, row.ticked,
                             row.subMenu, row.text, {}, nullptr, nullptr);
        y += heights[i];
    }
    return image;
}

juce::Image renderMenus(juce::AudioProcessorEditor& editor, float pixelScale)
{
    auto& lf = editor.getLookAndFeel();

    // The realtime oversampling dropdown, with its real items and current choice.
    std::vector<MenuRow> oversampling;
    int comboWidth = 120;
    for (auto* child : editor.getChildren())
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child); combo != nullptr && combo->getItemText(0).startsWith("realtime"))
        {
            comboWidth = combo->getWidth();
            for (int i = 0; i < combo->getNumItems(); ++i)
                oversampling.push_back({ combo->getItemText(i), true, i == combo->getSelectedItemIndex(), false, false });
        }
    if (oversampling.size() > 3)
        oversampling[3].highlighted = true;

    // The preset menu's top level: factory presets, categories as submenus, one under the pointer.
    std::vector<MenuRow> presets;
    int presetWidth = 200;
    {
        BqtPresetManager manager(dynamic_cast<BqtAudioProcessor&>(*editor.getAudioProcessor()).state());
        manager.refresh();
        juce::StringArray categories;
        for (const auto& preset : manager.getPresets())
        {
            if (! preset.factory)
                continue;
            if (preset.category.isEmpty())
                presets.push_back({ preset.name, true, presets.empty(), false, false });
            else if (! categories.contains(preset.category))
                categories.add(preset.category);
        }
        for (const auto& category : categories)
            presets.push_back({ category, true, false, true, false });
        if (presets.size() > 1)
            presets[1].highlighted = true;
        for (auto* child : editor.getChildren())
            if (child->getProperties().contains("bqtDropdown"))
                presetWidth = child->getWidth();
    }

    const auto a = paintMenu(lf, oversampling, comboWidth, pixelScale);
    const auto b = paintMenu(lf, presets, presetWidth, pixelScale);
    const auto gap = juce::roundToInt(24.0f * pixelScale);
    juce::Image sheet(juce::Image::ARGB, a.getWidth() + b.getWidth() + 3 * gap,
                      juce::jmax(a.getHeight(), b.getHeight()) + 2 * gap, true);
    juce::Graphics g(sheet);
    g.fillAll(juce::Colour(0xffffadcb)); // shown over the plate pink, as in use
    g.drawImageAt(a, gap, gap);
    g.drawImageAt(b, 2 * gap + a.getWidth(), gap);
    return sheet;
}
// Tooltips as the look-and-feel draws them, over the bar tint and over the plate pink.
juce::Image renderTips(juce::AudioProcessorEditor& editor, float pixelScale)
{
    auto& lf = editor.getLookAndFeel();
    const juce::StringArray tips { "Adjusts level before the EQ and saturation.", "+3.5 dB" };
    juce::Image sheet(juce::Image::ARGB, juce::roundToInt(460.0f * pixelScale), juce::roundToInt(150.0f * pixelScale), true);
    juce::Graphics g(sheet);
    g.addTransform(juce::AffineTransform::scale(pixelScale));
    g.fillAll(juce::Colour(0xffffadcb));
    int y = 24;
    for (const auto& tip : tips)
    {
        const auto bounds = lf.getTooltipBounds(tip, { 230, y + 40 }, { 0, 0, 460, 150 });
        juce::Graphics::ScopedSaveState state(g);
        g.setOrigin(bounds.getPosition());
        lf.drawTooltip(g, tip, bounds.getWidth(), bounds.getHeight());
        y += 56;
    }
    return sheet;
}
// Every bar control in its resting, hover, pressed and on looks, one row per control in bar order,
// drawn by the controls' own paint code. Buttons are put in each state with Button::setState; the
// fields that read the pointer directly (dropdowns, mode switches, the input knob) are shown hot
// through the "bqtPreviewHot" property, since there is no pointer offscreen. Cells a control does
// not have (action buttons never latch on; a dropdown or knob has no on state) are left empty.
juce::Image renderBarStates(juce::AudioProcessorEditor& editor, float pixelScale)
{
    std::vector<juce::Component*> controls;
    for (auto* child : editor.getChildren())
        if (child->isVisible() && child->getBounds().getBottom() < 130
            && (dynamic_cast<juce::Button*>(child) != nullptr || dynamic_cast<juce::ComboBox*>(child) != nullptr
                || dynamic_cast<juce::Slider*>(child) != nullptr || dynamic_cast<BqtSwitch*>(child) != nullptr))
            controls.push_back(child);
    std::sort(controls.begin(), controls.end(), [](auto* a, auto* b)
    {
        return a->getY() != b->getY() ? a->getY() < b->getY() : a->getX() < b->getX();
    });

    auto nameOf = [](juce::Component& c)
    {
        if (auto* combo = dynamic_cast<juce::ComboBox*>(&c)) return combo->getText();
        if (dynamic_cast<juce::Slider*>(&c) != nullptr) return juce::String("input knob");
        if (c.getTitle().isNotEmpty()) return c.getTitle();
        if (auto* button = dynamic_cast<juce::Button*>(&c)) return button->getButtonText();
        return juce::String("?");
    };

    const juce::StringArray columns { "resting", "hover", "pressed", "on" };
    constexpr int labelWidth = 120, pad = 10, headerHeight = 30;
    int cellWidth = 0, rowHeight = 0;
    for (auto* c : controls)
    {
        cellWidth = juce::jmax(cellWidth, c->getWidth() + 2 * pad);
        rowHeight = juce::jmax(rowHeight, c->getHeight() + pad);
    }
    const auto sheetWidth = labelWidth + cellWidth * columns.size();
    const auto sheetHeight = headerHeight + rowHeight * static_cast<int>(controls.size()) + pad;
    juce::Image sheet(juce::Image::ARGB, juce::roundToInt(static_cast<float>(sheetWidth) * pixelScale),
                      juce::roundToInt(static_cast<float>(sheetHeight) * pixelScale), true);
    juce::Graphics g(sheet);
    g.addTransform(juce::AffineTransform::scale(pixelScale));
    g.fillAll(juce::Colour(bqst::ui::barTint)); // as they sit in the bar
    g.setColour(juce::Colour(bqst::ui::ink));
    g.setFont(juce::Font(bqst::ui::faceFont(15.0f)));
    for (int col = 0; col < columns.size(); ++col)
        g.drawText(columns[col], labelWidth + col * cellWidth, 0, cellWidth, headerHeight, juce::Justification::centred);

    auto y = headerHeight;
    for (auto* c : controls)
    {
        g.setColour(juce::Colour(bqst::ui::ink).withAlpha(0.7f));
        g.drawText(nameOf(*c), pad, y, labelWidth - pad, rowHeight, juce::Justification::centredLeft);

        auto* button = dynamic_cast<juce::Button*>(c);
        auto* modeSwitch = dynamic_cast<BqtSwitch*>(c);
        const auto latches = button != nullptr && button->getClickingTogglesState();
        for (int col = 0; col < columns.size(); ++col)
        {
            const auto hot = col == 1 || col == 2;
            const auto on = col == 3;
            if (on && ! latches && modeSwitch == nullptr)
                continue;

            const auto wasOn = button != nullptr && button->getToggleState();
            const auto wasSelected = modeSwitch != nullptr ? modeSwitch->getSelected() : 0;
            if (button != nullptr)
            {
                button->setToggleState(on, juce::dontSendNotification);
                button->setState(col == 1 ? juce::Button::buttonOver : col == 2 ? juce::Button::buttonDown : juce::Button::buttonNormal);
            }
            else
            {
                c->getProperties().set("bqtPreviewHot", hot);
            }
            if (modeSwitch != nullptr)
                modeSwitch->setSelected(on ? 1 : 0);

            const auto picture = c->createComponentSnapshot(c->getLocalBounds(), true, pixelScale);
            const auto cell = juce::Rectangle<int>(labelWidth + col * cellWidth, y, cellWidth, rowHeight);
            const auto at = juce::Rectangle<int>(c->getWidth(), c->getHeight()).withCentre(cell.getCentre());
            g.setOpacity(1.0f); // the label colour above would otherwise fade the picture
            g.drawImage(picture, at.toFloat());

            if (button != nullptr)
            {
                button->setState(juce::Button::buttonNormal);
                button->setToggleState(wasOn, juce::dontSendNotification);
            }
            c->getProperties().remove("bqtPreviewHot");
            if (modeSwitch != nullptr)
                modeSwitch->setSelected(wasSelected);
        }
        y += rowHeight;
    }
    return sheet;
}
// The first VU meter painted alone: its housing, face and needle with real alpha around them.
juce::Image renderMeter(juce::Component& root, float pixelScale)
{
    std::function<BqtVuMeter*(juce::Component&)> find = [&find](juce::Component& c) -> BqtVuMeter*
    {
        if (auto* meter = dynamic_cast<BqtVuMeter*>(&c))
            return meter;
        for (auto* child : c.getChildren())
            if (auto* meter = find(*child))
                return meter;
        return nullptr;
    };
    auto* meter = find(root);
    return meter != nullptr ? meter->createComponentSnapshot(meter->getLocalBounds(), true, pixelScale) : juce::Image();
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cerr << "usage: BqstEditorSnapshot <width> <out.png> [pixelScale]\n";
        return 2;
    }

    const auto menuMode = juce::String(argv[1]) == "menu";
    const auto tipsMode = juce::String(argv[1]) == "tips";
    const auto barMode = juce::String(argv[1]) == "bar";
    const auto vuMode = juce::String(argv[1]) == "vu"; // one meter alone, on a transparent background
    const auto showAbout = argc > 4 && juce::String(argv[4]) == "about";
    const auto width = (menuMode || tipsMode || barMode || vuMode) ? 865 : juce::String(argv[1]).getIntValue();
    const auto pixelScale = argc > 3 ? juce::String(argv[3]).getFloatValue() : 1.0f;
    if (width < 100 || width > 8000 || pixelScale <= 0.0f || pixelScale > 16.0f)
    {
        std::cerr << "bad width or pixelScale\n";
        return 2;
    }

    const auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[2]));

    juce::ScopedJuceInitialiser_GUI gui;
    int exitCode = 1;
    {
        BqtAudioProcessor processor;
        processor.setPlayConfigDetails(2, 2, 48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        if (editor == nullptr)
        {
            std::cerr << "no editor\n";
            return 1;
        }

        const auto aspect = static_cast<double>(editor->getHeight()) / static_cast<double>(editor->getWidth());
        editor->setSize(width, juce::roundToInt(width * aspect));

        if (showAbout)
            for (auto* child : editor->getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*>(child); button != nullptr && button->getButtonText() == "about" && button->onClick)
                    button->onClick();

        const auto image = menuMode ? renderMenus(*editor, pixelScale)
                         : tipsMode ? renderTips(*editor, pixelScale)
                         : barMode ? renderBarStates(*editor, pixelScale)
                         : vuMode ? renderMeter(*editor, pixelScale)
                                    : editor->createComponentSnapshot(editor->getLocalBounds(), true, pixelScale);
        outFile.getParentDirectory().createDirectory();
        outFile.deleteFile();
        juce::FileOutputStream out(outFile);
        juce::PNGImageFormat png;
        if (out.openedOk() && png.writeImageToStream(image, out))
        {
            std::cout << outFile.getFullPathName() << " " << image.getWidth() << "x" << image.getHeight() << "\n";
            exitCode = 0;
        }
        else
        {
            std::cerr << "could not write " << outFile.getFullPathName() << "\n";
        }

        editor.reset();
    }
    return exitCode;
}
