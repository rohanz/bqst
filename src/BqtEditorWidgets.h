#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

class BqtHardwareLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    BqtHardwareLookAndFeel();
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) override;
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                      int buttonW, int buttonH, juce::ComboBox& box) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label) override;
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSize() override { return 4; }
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                           bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* icon,
                           const juce::Colour* textColour) override;

    // Focus rings are for keyboard users: shown once Tab is used, hidden again by a mouse press.
    void setFocusRingsVisible(bool shouldShow) { focusRingsVisible = shouldShow; }
    bool areFocusRingsVisible() const { return focusRingsVisible; }

    // Every menu in the editor opens the same way: under its control, at least as wide, 26 px rows.
    static constexpr int menuRowHeight = 26;
    static void showMenuUnder(juce::PopupMenu& menu, juce::Component& target, std::function<void(int)> onResult);

private:
    bool focusRingsVisible = false;
};

// A flat two-way choice (l/r or m/s). Each half is a real toggle button, so it takes keyboard
// focus; a solid ink pill slides under the chosen half. The switch only shows a state and reports
// a choice: the owner writes the parameter, and the parameter sets the switch.
class BqtSwitch final : public juce::Component
{
public:
    BqtSwitch(const juce::String& leftText, const juce::String& rightText);
    ~BqtSwitch() override;

    std::function<void(int)> onSelect;
    void setSelected(int index);
    int getSelected() const { return selected; }
    // The sliding fill, in this component's coordinates.
    juce::Rectangle<float> pillBounds() const { return pill; }
    juce::Button& getHalf(int index) { return *halves[static_cast<size_t>(index)]; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override { snapPill(); }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    void snapPill();
    void animationFrame();

    static constexpr double slideDurationMs = 120.0;
    std::array<std::unique_ptr<juce::ToggleButton>, 2> halves;
    int selected = 0;
    juce::Rectangle<float> pill, slideStart;
    double slideStartedAt = 0.0;
    juce::VBlankAttachment frameClock;
};

class BqtReadoutBubble final : public juce::Component
{
public:
    void setText(juce::String newText);
    void paint(juce::Graphics& g) override;

private:
    juce::String text;
};
