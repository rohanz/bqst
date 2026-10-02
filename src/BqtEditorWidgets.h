#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

class BqtHardwareLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    BqtHardwareLookAndFeel();
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
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                           bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
};

class BqtReadoutBubble final : public juce::Component
{
public:
    void setText(juce::String newText);
    void paint(juce::Graphics& g) override;

private:
    juce::String text;
};

class BqtVuMeter final : public juce::Component
{
public:
    BqtVuMeter(BqtAudioProcessor& processor, int sideIndex);
    void paint(juce::Graphics& g) override;
    void setRenderScale(float newScale);
    // secondsElapsed is the real time since the previous call. The needle smoothing used to use a
    // fixed per-tick coefficient, which made its speed depend on how punctually the timer fired.
    bool updateLevel(double secondsElapsed);

private:
    void rebuildStaticLayer();

    BqtAudioProcessor& audioProcessor;
    int side = 0;
    float targetLevel = 0.0f;
    float displayedLevel = 0.0f;
    juce::Image staticLayer;
    int staticLayerWidth = 0;
    int staticLayerHeight = 0;
    float staticLayerScale = 0.0f;
    float renderScale = 1.0f;
};
