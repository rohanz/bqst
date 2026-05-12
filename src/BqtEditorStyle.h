#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BinaryData.h"

#include <cmath>

namespace bqst::ui
{
constexpr auto panelPink = 0xffffadcb;
constexpr auto panelPinkDark = 0xfff095bd;
constexpr auto ink = 0xff111111;
constexpr auto cream = 0xfffff4ed;
constexpr auto panelText = 0xfffff1df;
constexpr auto lampOn = 0xfffaf7f2;
constexpr auto lampOff = 0xff6d6d6d;
constexpr auto totalSlots = 4.0f;
constexpr auto slotWidthInches = 1.5f;
constexpr auto panelHeightInches = 5.25f;
constexpr auto screwInsetYInches = (5.25f - 4.938f) * 0.5f;
constexpr auto baseEditorWidth = 900;
constexpr auto baseEditorHeight = 874;
constexpr auto outerEditorMargin = 14;
constexpr auto fixedEditorScale = 1.25f;

inline const juce::StringArray lowFreqLabels { "74", "84", "98", "116", "131", "166", "230", "361" };
inline const juce::StringArray highFreqLabels { "1.6k", "1.8k", "2.1k", "2.5k", "3.4k", "4.8k", "7.1k", "18k" };

inline juce::String indexedLabel(const juce::StringArray& labels, double value)
{
    return labels[juce::jlimit(0, labels.size() - 1, static_cast<int>(std::round(value)))];
}

inline juce::String sidePrefix(int sideIndex)
{
    return sideIndex == 0 ? "a" : "b";
}

inline juce::FontOptions faceFont(float height, bool bold = true)
{
    static auto regular = juce::Typeface::createSystemTypefaceFor(BinaryData::ChillaxRegular_otf,
                                                                  BinaryData::ChillaxRegular_otfSize);
    static auto semibold = juce::Typeface::createSystemTypefaceFor(BinaryData::ChillaxSemibold_otf,
                                                                   BinaryData::ChillaxSemibold_otfSize);
    return juce::FontOptions(bold && semibold != nullptr ? semibold : regular).withHeight(height);
}

inline juce::Rectangle<float> getRackFaceBounds(juce::Rectangle<float> available)
{
    available = available.reduced(4.0f, 0.0f);
    const auto targetRatio = (totalSlots * slotWidthInches) / panelHeightInches;
    auto width = available.getWidth();
    auto height = width / targetRatio;

    if (height > available.getHeight())
    {
        height = available.getHeight();
        width = height * targetRatio;
    }

    return juce::Rectangle<float>(width, height).withCentre(available.getCentre());
}

inline void drawPanelText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> bounds,
                          juce::Justification justification, float height, bool bold = true)
{
    g.setFont(juce::Font(faceFont(height, bold)));
    g.setColour(juce::Colour(panelText));
    g.drawText(text, bounds.toNearestInt(), justification);
}

inline float getTextWidth(const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox(0, -1, true).getWidth();
}

inline void drawRotatedImageKnob(juce::Graphics& g, const juce::Image& image, juce::Rectangle<float> bounds,
                                 float angle, float scale = 1.0f)
{
    if (! image.isValid())
        return;

    auto knob = bounds;
    const auto side = juce::jmin(knob.getWidth(), knob.getHeight()) * scale;
    knob = juce::Rectangle<float>(side, side).withCentre(knob.getCentre());

    const auto centre = knob.getCentre();
    const auto transform = juce::AffineTransform::translation(static_cast<float>(-image.getWidth()) * 0.5f,
                                                              static_cast<float>(-image.getHeight()) * 0.5f)
                               .scaled(knob.getWidth() / static_cast<float>(image.getWidth()),
                                       knob.getHeight() / static_cast<float>(image.getHeight()))
                               .rotated(angle)
                               .translated(centre.x, centre.y);

    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImageTransformed(image, transform, false);
}
} // namespace bqst::ui
