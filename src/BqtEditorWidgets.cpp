#include "BqtEditorWidgets.h"

#include "BinaryData.h"
#include "BqtEditorStyle.h"

#include <array>
#include <cmath>

using namespace bqst::ui;

namespace
{
// VU scale position (0 = left stop, 1 = right stop) of a VU reading: -20..0 VU takes the first 82%
// of the arc, 0..+3 VU the red remainder.
float vuDbToScaleFraction(float db)
{
    const auto clamped = juce::jlimit(-20.0f, 3.0f, db);
    if (clamped <= 0.0f)
        return ((clamped + 20.0f) / 20.0f) * 0.82f;
    return 0.82f + (clamped / 3.0f) * 0.18f;
}

// Backlit VU face. The frame artwork bakes in a flat face; this repaints it as paper lit by a
// soft-white bulb hidden behind the bottom bar: the face is a little darker overall (unlit), the
// bulb's reflection brings the lower part back up and a bloom lifts it slightly past the paper.
// Geometry is in the artwork's 2000 px space, measured from vu-frame.png.
namespace vuface
{
constexpr float artSize = 2000.0f, centreX = 1000.0f, centreY = 865.0f, radius = 569.0f, cutY = 1189.0f;
constexpr float paper[3] { 244.0f, 243.0f, 239.0f };  // neutral off-white #f4f3ef
constexpr float bulb[3] { 255.0f, 232.0f, 208.0f };   // soft white, about 4000 K
constexpr float unlitDarkening = 0.16f;
constexpr float bloom = 0.9f;

struct Stop { float position, alpha; };

template <size_t N>
float alphaAt(const Stop (&stops)[N], float t)
{
    if (t <= stops[0].position)
        return stops[0].alpha;
    for (size_t i = 1; i < N; ++i)
        if (t <= stops[i].position)
            return juce::jmap(t, stops[i - 1].position, stops[i].position, stops[i - 1].alpha, stops[i].alpha);
    return stops[N - 1].alpha;
}

// 0..1 along a radial gradient with inner radius r0 and outer radius r1.
float radialT(float dx, float dy, float r0, float r1)
{
    return juce::jlimit(0.0f, 1.0f, (std::hypot(dx, dy) - r0) / (r1 - r0));
}

// Paints the face over `layer`, whose pixels are `scale` per logical unit; `frame` is where the
// artwork was drawn, in logical units.
void paint(juce::Image& layer, juce::Rectangle<float> frame, float scale)
{
    const auto toX = [frame](float artX) { return frame.getX() + artX / artSize * frame.getWidth(); };
    const auto toY = [frame](float artY) { return frame.getY() + artY / artSize * frame.getHeight(); };
    const auto cx = toX(centreX), cy = toY(centreY), cut = toY(cutY);
    const auto r = radius / artSize * frame.getWidth();
    const auto clipR = r + 0.8f, clipBottom = cut + 0.8f;

    static constexpr Stop reflection[] { { 0.0f, 1.0f }, { 0.35f, 0.85f }, { 0.7f, 0.35f }, { 1.0f, 0.0f } };
    static constexpr Stop bloomFalloff[] { { 0.0f, 1.0f }, { 0.5f, 0.4f }, { 1.0f, 0.0f } };

    float reflected[3], hot[3], base[3];
    for (int c = 0; c < 3; ++c)
    {
        base[c] = paper[c] * (1.0f - unlitDarkening);
        reflected[c] = paper[c] * bulb[c] / 255.0f;
        hot[c] = std::round(255.0f - (255.0f - bulb[c]) * 0.5f);
    }

    const auto x0 = juce::jmax(0, static_cast<int>(std::floor((cx - clipR) * scale)));
    const auto x1 = juce::jmin(layer.getWidth(), static_cast<int>(std::ceil((cx + clipR) * scale)));
    const auto y0 = juce::jmax(0, static_cast<int>(std::floor((cy - clipR) * scale)));
    const auto y1 = juce::jmin(layer.getHeight(), static_cast<int>(std::ceil(clipBottom * scale)));

    juce::Image::BitmapData pixels(layer, juce::Image::BitmapData::readWrite);
    for (int py = y0; py < y1; ++py)
    {
        const auto y = (static_cast<float>(py) + 0.5f) / scale;
        for (int px = x0; px < x1; ++px)
        {
            const auto x = (static_cast<float>(px) + 0.5f) / scale;
            const auto coverage = juce::jlimit(0.0f, 1.0f, (clipR - std::hypot(x - cx, y - cy)) * scale + 0.5f)
                                * juce::jlimit(0.0f, 1.0f, (clipBottom - y) * scale + 0.5f);
            if (coverage <= 0.0f)
                continue;

            const auto reflectionAlpha = alphaAt(reflection, radialT(x - cx, y - (cut + 22.0f), 6.0f, r * 1.55f));
            const auto hotAlpha = 0.55f * (1.0f - radialT(x - cx, y - (cut + 4.0f), 1.0f, r * 0.45f));
            const auto bloomAlpha = bloom * alphaAt(bloomFalloff, radialT(x - cx, y - (cut + 10.0f), 4.0f, r * 1.05f));

            juce::uint8 rgb[3];
            for (int c = 0; c < 3; ++c)
            {
                auto v = base[c] + (reflected[c] - base[c]) * reflectionAlpha;
                v += hotAlpha * hot[c] * (1.0f - v / 255.0f);    // screen
                v += bloomAlpha * hot[c] * (1.0f - v / 255.0f);  // screen
                rgb[c] = static_cast<juce::uint8>(juce::jlimit(0.0f, 255.0f, std::round(v)));
            }

            const auto face = juce::Colour(rgb[0], rgb[1], rgb[2]);
            pixels.setPixelColour(px, py, pixels.getPixelColour(px, py).interpolatedWith(face, coverage));
        }
    }
}
} // namespace vuface
} // namespace

void BqtReadoutBubble::setText(juce::String newText)
{
    if (text == newText)
        return;

    text = std::move(newText);
    repaint();
}

void BqtReadoutBubble::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff111111));
    g.fillRoundedRectangle(bounds.reduced(1.0f), 3.0f);
    g.setColour(juce::Colour(panelText).withAlpha(0.72f));
    g.drawRoundedRectangle(bounds.reduced(1.0f), 3.0f, 1.0f);
    g.setColour(juce::Colour(panelText));
    g.setFont(juce::Font(faceFont(16.5f, true)));
    g.drawText(text, getLocalBounds().reduced(8, 2), juce::Justification::centred);
}

BqtHardwareLookAndFeel::BqtHardwareLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, juce::Colour(ink));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::textColourId, juce::Colour(ink));
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(cream));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(ink));
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(cream));
    setColour(juce::PopupMenu::textColourId, juce::Colour(ink));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(panelPink));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(ink));
    setColour(juce::PopupMenu::headerTextColourId, juce::Colour(ink).withAlpha(0.6f));
}

// Drop-down and preset menus use the panel's face font, matching the buttons that open them.
juce::Font BqtHardwareLookAndFeel::getPopupMenuFont()
{
    return juce::Font(faceFont(15.8f));
}

// The current choice gets a lit lamp, like the panel's cream/grit indicators, instead of a tick.
void BqtHardwareLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                               bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                               const juce::String& text, const juce::String& shortcutKeyText,
                                               const juce::Drawable* icon, const juce::Colour* textColour)
{
    juce::LookAndFeel_V4::drawPopupMenuItem(g, area, isSeparator, isActive, isHighlighted, false, hasSubMenu, text,
                                            shortcutKeyText, icon, textColour);
    if (! isTicked || isSeparator)
        return;

    // Same left column LookAndFeel_V4 reserves for the tick.
    const auto row = area.reduced(1).toFloat();
    const auto column = row.withWidth(row.getHeight() / 1.3f);
    const auto lamp = juce::Rectangle<float>(9.0f, 9.0f).withCentre(column.getCentre().translated(2.0f, 0.0f));
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.fillEllipse(lamp.translated(1.0f, 1.0f));
    g.setColour(juce::Colour(lampOn));
    g.fillEllipse(lamp);
    g.setColour(juce::Colour(ink));
    g.drawEllipse(lamp, 1.0f);
}

void BqtHardwareLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                              juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                               static_cast<float>(width), static_cast<float>(height)).reduced(8.0f);
    const auto textBox = slider.getTextBoxHeight() > 0 ? 22.0f : 0.0f;
    auto knobArea = bounds;
    knobArea.removeFromBottom(textBox);
    const auto radius = juce::jmin(knobArea.getWidth(), knobArea.getHeight()) * 0.46f;
    const auto centre = knobArea.getCentre();
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto knob = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);

    if (slider.getProperties().contains("bqtLargeCreamKnob") && static_cast<bool>(slider.getProperties()["bqtLargeCreamKnob"]))
    {
        static auto image = []
        {
            const auto source = juce::ImageCache::getFromMemory(BinaryData::knoblargeskirted_png,
                                                                BinaryData::knoblargeskirted_pngSize);
            return source.rescaled(768, 768, juce::Graphics::highResamplingQuality);
        }();
        drawRotatedImageKnob(g, image, knobArea, angle, 1.18f);
        return;
    }

    if (slider.getProperties().contains("bqtSmallCreamKnob") && static_cast<bool>(slider.getProperties()["bqtSmallCreamKnob"]))
    {
        static auto image = []
        {
            const auto source = juce::ImageCache::getFromMemory(BinaryData::knobsmallpointer_png,
                                                                BinaryData::knobsmallpointer_pngSize);
            return source.rescaled(512, 512, juce::Graphics::highResamplingQuality);
        }();
        drawRotatedImageKnob(g, image, knobArea, angle, 1.08f);
        return;
    }

    if (slider.getProperties().contains("bqtKnobCombo") && static_cast<bool>(slider.getProperties()["bqtKnobCombo"]))
    {
        static auto image = []
        {
            const auto source = juce::ImageCache::getFromMemory(BinaryData::knobsmallpointer_png,
                                                                BinaryData::knobsmallpointer_pngSize);
            return source.rescaled(512, 512, juce::Graphics::highResamplingQuality);
        }();
        drawRotatedImageKnob(g, image, knobArea, angle, 1.10f);
        return;
    }

    if (slider.getProperties().contains("bqtTopInputKnob") && static_cast<bool>(slider.getProperties()["bqtTopInputKnob"]))
    {
        const auto fullBounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                       static_cast<float>(width), static_cast<float>(height)).reduced(2.0f);
        const auto side = juce::jmin(fullBounds.getWidth(), fullBounds.getHeight()) * 0.96f;
        const auto mini = juce::Rectangle<float>(side, side).withCentre(fullBounds.getCentre());
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillEllipse(mini.translated(1.0f, 2.0f));

        juce::ColourGradient body(juce::Colour(0xfff7eee6), mini.getCentreX(), mini.getY(),
                                  juce::Colour(0xffcfc2b9), mini.getCentreX(), mini.getBottom(), false);
        g.setGradientFill(body);
        g.fillEllipse(mini);
        g.setColour(juce::Colour(ink).withAlpha(0.55f));
        g.drawEllipse(mini, 1.1f);

        const auto pointerEnd = mini.getCentre().getPointOnCircumference(mini.getWidth() * 0.34f, angle);
        g.setColour(juce::Colour(ink));
        g.drawLine({ mini.getCentre(), pointerEnd }, 2.2f);
        return;
    }

    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillEllipse(knob.translated(3.0f, 5.0f));

    juce::ColourGradient body(juce::Colour(0xff3b3b3b), knob.getX(), knob.getY(),
                              juce::Colour(0xff020202), knob.getRight(), knob.getBottom(), false);
    body.addColour(0.45, juce::Colour(0xff151515));
    g.setGradientFill(body);
    g.fillEllipse(knob);

    g.setColour(juce::Colour(0xff050505));
    g.drawEllipse(knob, 2.0f);
    g.setColour(juce::Colour(0xff5f5f5f).withAlpha(0.55f));
    g.drawEllipse(knob.reduced(4.0f), 1.2f);

    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.fillEllipse(knob.reduced(radius * 0.18f).withTrimmedRight(radius * 0.52f).withTrimmedBottom(radius * 0.52f));

    const auto pointerLength = radius * 0.72f;
    const auto pointerThickness = juce::jmax(2.0f, radius * 0.055f);
    juce::Path pointer;
    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -pointerLength, pointerThickness, pointerLength * 0.78f,
                                pointerThickness * 0.45f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
    g.setColour(juce::Colour(0xfff4f0ec));
    g.fillPath(pointer);
}

void BqtHardwareLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                          int, int, int, int, juce::ComboBox& box)
{
    if (box.getProperties().contains("bqtKnobCombo") && static_cast<bool>(box.getProperties()["bqtKnobCombo"]))
    {
        const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(5.0f);
        const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.46f;
        const auto centre = bounds.getCentre();
        const auto choiceCount = juce::jmax(1, box.getNumItems());
        const auto selected = juce::jlimit(0, choiceCount - 1, box.getSelectedItemIndex());
        const auto pos = choiceCount <= 1 ? 0.5f : static_cast<float>(selected) / static_cast<float>(choiceCount - 1);
        const auto start = juce::MathConstants<float>::pi * 1.22f;
        const auto end = juce::MathConstants<float>::pi * 2.78f;
        const auto angle = start + pos * (end - start);

        static auto image = []
        {
            const auto source = juce::ImageCache::getFromMemory(BinaryData::knobsmallpointer_png,
                                                                BinaryData::knobsmallpointer_pngSize);
            return source.rescaled(512, 512, juce::Graphics::highResamplingQuality);
        }();
        drawRotatedImageKnob(g, image, bounds.withCentre(centre).withSizeKeepingCentre(radius * 2.18f, radius * 2.18f),
                             angle, 1.10f);
        return;
    }

    if (box.getProperties().contains("bqtSatTypeSelector") && static_cast<bool>(box.getProperties()["bqtSatTypeSelector"]))
    {
        auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        const auto selected = box.getSelectedItemIndex();
        const auto rowH = bounds.getHeight() * 0.5f;

        auto drawRow = [&g](juce::String text, int row, bool on)
        {
            const auto y = 8.0f + static_cast<float>(row) * 30.0f;
            const auto led = juce::Rectangle<float>(13.0f, 13.0f).withCentre({ 18.0f, y + 8.0f });
            const auto rail = juce::Rectangle<float>(82.0f, 3.0f).withCentre({ 72.0f, y + 8.0f });
            const auto button = juce::Rectangle<float>(25.0f, 25.0f).withCentre({ 112.0f, y + 8.0f });

            g.setColour(juce::Colour(ink));
            g.setFont(juce::Font(faceFont(17.0f)));
            g.drawText(text, juce::Rectangle<int>(30, static_cast<int>(y - 4.0f), 64, 24), juce::Justification::centredLeft);

            g.setColour(juce::Colour(ink));
            g.fillRoundedRectangle(rail, 1.5f);

            g.setColour(juce::Colours::black.withAlpha(0.28f));
            g.fillEllipse(button.translated(1.0f, 2.0f));
            g.setColour(on ? juce::Colour(ink) : juce::Colour(0xff9a9a9a));
            g.fillEllipse(button);

            g.setColour(juce::Colours::black.withAlpha(0.25f));
            g.fillEllipse(led.translated(1.0f, 1.0f));
            g.setColour(on ? juce::Colour(lampOn) : juce::Colour(lampOff));
            g.fillEllipse(led);
            g.setColour(juce::Colour(ink));
            g.drawEllipse(led, 1.0f);
        };

        drawRow("cream", 0, selected <= 0);
        drawRow("grit", 1, selected > 0);
        juce::ignoreUnused(rowH);
        return;
    }

    const auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f)
                            .reduced(1.0f, 2.0f);
    g.setColour(juce::Colours::black.withAlpha(0.42f));
    g.fillRoundedRectangle(bounds.translated(0.0f, 3.0f), 4.0f);

    juce::ColourGradient buttonGradient(isButtonDown ? juce::Colour(0xffeee4dc) : juce::Colour(cream),
                                        bounds.getCentreX(), bounds.getY(),
                                        isButtonDown ? juce::Colour(0xffcfc4bb) : juce::Colour(0xffded3ca),
                                        bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(buttonGradient);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(juce::Colours::white.withAlpha(0.45f));
    g.drawRoundedRectangle(bounds.reduced(1.0f), 3.0f, 1.0f);
    g.setColour(juce::Colour(panelText));
    g.drawRoundedRectangle(bounds, 4.0f, 1.2f);

    juce::Path arrow;
    const auto cx = bounds.getRight() - 13.0f;
    const auto cy = bounds.getCentreY();
    arrow.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour(juce::Colour(ink));
    g.fillPath(arrow);
    box.setColour(juce::Label::textColourId, juce::Colour(ink));
}

void BqtHardwareLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    if (box.getProperties().contains("bqtKnobCombo") && static_cast<bool>(box.getProperties()["bqtKnobCombo"]))
    {
        label.setBounds(0, 0, 0, 0);
        return;
    }

    if (box.getProperties().contains("bqtSatTypeSelector") && static_cast<bool>(box.getProperties()["bqtSatTypeSelector"]))
    {
        label.setBounds(0, 0, 0, 0);
        return;
    }

    label.setBounds(7, 1, box.getWidth() - 21, box.getHeight() - 2);
    label.setFont(juce::Font(faceFont(15.8f)));
    label.setJustificationType(juce::Justification::centred);
}

void BqtHardwareLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    if (button.getProperties().contains("bqtPushButton") && static_cast<bool>(button.getProperties()["bqtPushButton"]))
    {
        const auto active = button.getToggleState();
        const auto pressed = shouldDrawButtonAsDown || active;
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f, 2.0f);

        g.setColour(juce::Colours::black.withAlpha(pressed ? 0.24f : 0.42f));
        g.fillRoundedRectangle(bounds.translated(0.0f, pressed ? 1.0f : 3.0f), 4.0f);

        if (pressed)
            bounds = bounds.reduced(0.8f).translated(0.0f, 0.7f);

        // Latched on: panel pink, still seated a little lower. A momentary press stays cream.
        const auto topColour = active ? juce::Colour(0xffffc3d9) : pressed ? juce::Colour(0xffeee4dc) : juce::Colour(cream);
        const auto bottomColour = active ? juce::Colour(panelPinkDark) : pressed ? juce::Colour(0xffcfc4bb) : juce::Colour(0xffded3ca);
        juce::ColourGradient buttonGradient(topColour, bounds.getCentreX(), bounds.getY(),
                                            bottomColour, bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(buttonGradient);
        g.fillRoundedRectangle(bounds, 4.0f);

        g.setColour(pressed ? juce::Colours::black.withAlpha(0.26f) : juce::Colours::white.withAlpha(0.45f));
        g.drawRoundedRectangle(bounds.reduced(1.0f), 3.0f, 1.0f);
        g.setColour(pressed ? juce::Colour(ink).withAlpha(0.56f) : juce::Colour(panelText));
        g.drawRoundedRectangle(bounds, 4.0f, pressed ? 1.7f : 1.2f);

        if (shouldDrawButtonAsHighlighted && ! pressed)
        {
            g.setColour(juce::Colours::white.withAlpha(0.18f));
            g.fillRoundedRectangle(bounds.reduced(2.0f), 3.0f);
        }

        g.setColour(juce::Colour(ink).withAlpha(button.isEnabled() ? 1.0f : 0.38f));
        g.setFont(juce::Font(faceFont(15.8f)));
        g.drawText(button.getButtonText().toLowerCase(), bounds.toNearestInt(), juce::Justification::centred);
        return;
    }

    auto bounds = button.getLocalBounds().toFloat();
    auto labels = bounds.removeFromLeft(58.0f);
    auto controls = bounds;
    const auto led = juce::Rectangle<float>(13.0f, 13.0f).withCentre({ controls.getX() + 8.0f, controls.getCentreY() });
    const auto rail = juce::Rectangle<float>(35.0f, 3.0f).withCentre({ controls.getX() + 25.5f, controls.getCentreY() });
    auto cap = juce::Rectangle<float>(22.0f, 22.0f).withCentre({ controls.getX() + 43.0f, controls.getCentreY() });
    const auto on = button.getToggleState();
    const auto down = shouldDrawButtonAsDown;

    g.setColour(juce::Colour(panelText));
    g.setFont(juce::Font(faceFont(20.0f, true)));
    g.drawText(button.getButtonText().toLowerCase(), labels.toNearestInt(), juce::Justification::centredRight);

    g.setColour(juce::Colour(ink));
    g.fillRoundedRectangle(rail, 1.5f);

    if (down)
        cap = cap.reduced(1.0f).translated(0.0f, 0.5f);

    g.setColour(juce::Colours::black.withAlpha(0.30f));
    g.fillEllipse(cap.translated(1.0f, 2.0f));
    g.setColour(juce::Colour(cream));
    g.fillEllipse(cap);
    g.setColour(juce::Colour(ink).withAlpha(0.45f));
    g.drawEllipse(cap, 1.0f);

    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.fillEllipse(led.translated(1.0f, 1.0f));
    auto lampColour = on ? juce::Colour(lampOn) : juce::Colour(lampOff);
    if (shouldDrawButtonAsDown)
        lampColour = lampColour.brighter(0.08f);
    if (on)
    {
        g.setColour(lampColour.withAlpha(0.11f));
        g.fillEllipse(led.expanded(7.0f));
        g.setColour(lampColour.withAlpha(0.18f));
        g.fillEllipse(led.expanded(3.0f));
    }
    g.setColour(lampColour);
    g.fillEllipse(led);
    if (on)
    {
        g.setColour(juce::Colours::white.withAlpha(0.40f));
        g.fillEllipse(led.reduced(4.0f).translated(-1.5f, -1.5f));
    }
    g.setColour(juce::Colour(panelText));
    g.drawEllipse(led, 1.0f);
}

void BqtHardwareLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                  const juce::Colour&, bool, bool shouldDrawButtonAsDown)
{
    if (button.getProperties().contains("bqtPushButton") && static_cast<bool>(button.getProperties()["bqtPushButton"]))
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f, 2.0f);

        g.setColour(juce::Colours::black.withAlpha(shouldDrawButtonAsDown ? 0.24f : 0.42f));
        g.fillRoundedRectangle(bounds.translated(0.0f, shouldDrawButtonAsDown ? 1.0f : 3.0f), 4.0f);

        if (shouldDrawButtonAsDown)
            bounds = bounds.reduced(0.8f).translated(0.0f, 0.7f);

        juce::ColourGradient buttonGradient(shouldDrawButtonAsDown ? juce::Colour(0xffeee4dc) : juce::Colour(cream),
                                            bounds.getCentreX(), bounds.getY(),
                                            shouldDrawButtonAsDown ? juce::Colour(0xffcfc4bb) : juce::Colour(0xffded3ca),
                                            bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(buttonGradient);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.drawRoundedRectangle(bounds.reduced(1.0f), 3.0f, 1.0f);
        g.setColour(juce::Colour(panelText));
        g.drawRoundedRectangle(bounds, 4.0f, 1.2f);

        if (auto* textButton = dynamic_cast<juce::TextButton*>(&button))
        {
            g.setColour(juce::Colour(ink).withAlpha(button.isEnabled() ? 1.0f : 0.38f));
            g.setFont(juce::Font(faceFont(15.8f)));
            g.drawText(textButton->getButtonText(), bounds.toNearestInt().reduced(6, 0), juce::Justification::centred);
        }

        return;
    }

    if (! (button.getProperties().contains("bqtSatTypeSelector")
           && static_cast<bool>(button.getProperties()["bqtSatTypeSelector"])))
    {
        LookAndFeel_V4::drawButtonBackground(g, button, juce::Colour(cream), false, false);
        return;
    }

    const auto grit = button.getToggleState();
    auto bounds = button.getLocalBounds().toFloat();
    auto labels = bounds.removeFromLeft(60.0f);
    auto controls = bounds;

    auto drawLedRow = [&g, labels, controls](const juce::String& text, int row, bool on)
    {
        const auto y = 8.0f + static_cast<float>(row) * 24.0f;
        const auto led = juce::Rectangle<float>(13.0f, 13.0f).withCentre({ controls.getX() + 8.0f, y + 8.0f });

        g.setColour(juce::Colour(panelText));
        g.setFont(juce::Font(faceFont(20.0f, true)));
        g.drawText(text, labels.withY(y - 4.0f).withHeight(24.0f).toNearestInt(), juce::Justification::centredRight);

        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.fillEllipse(led.translated(1.0f, 1.0f));
        g.setColour(on ? juce::Colour(lampOn) : juce::Colour(lampOff));
        if (on)
        {
            g.setColour(juce::Colour(lampOn).withAlpha(0.11f));
            g.fillEllipse(led.expanded(7.0f));
            g.setColour(juce::Colour(lampOn).withAlpha(0.18f));
            g.fillEllipse(led.expanded(3.0f));
            g.setColour(juce::Colour(lampOn));
        }
        g.fillEllipse(led);
        if (on)
        {
            g.setColour(juce::Colours::white.withAlpha(0.40f));
            g.fillEllipse(led.reduced(4.0f).translated(-1.5f, -1.5f));
        }
        g.setColour(juce::Colour(panelText));
        g.drawEllipse(led, 1.0f);
    };

    const auto rail = juce::Rectangle<float>(35.0f, 3.0f).withCentre({ controls.getX() + 25.5f, controls.getCentreY() });
    const auto ledTop = juce::Rectangle<float>(13.0f, 13.0f).withCentre({ controls.getX() + 8.0f, 16.0f });
    const auto ledBottom = juce::Rectangle<float>(13.0f, 13.0f).withCentre({ controls.getX() + 8.0f, 40.0f });
    auto cap = juce::Rectangle<float>(22.0f, 22.0f).withCentre({ controls.getX() + 43.0f, controls.getCentreY() });
    if (shouldDrawButtonAsDown)
        cap = cap.reduced(1.0f).translated(0.0f, 0.5f);

    g.setColour(juce::Colour(ink));
    g.fillRoundedRectangle(rail, 1.5f);
    g.drawLine({ ledTop.getCentreX(), ledTop.getCentreY(), ledBottom.getCentreX(), ledBottom.getCentreY() }, 3.0f);
    g.setColour(juce::Colours::black.withAlpha(0.28f));
    g.fillEllipse(cap.translated(1.0f, 2.0f));
    g.setColour(juce::Colour(cream));
    g.fillEllipse(cap);
    g.setColour(juce::Colour(ink).withAlpha(0.45f));
    g.drawEllipse(cap, 1.0f);

    drawLedRow("cream", 0, ! grit);
    drawLedRow("grit", 1, grit);
}

void BqtHardwareLookAndFeel::drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool)
{
}

juce::Rectangle<int> BqtHardwareLookAndFeel::getTooltipBounds(const juce::String& tipText,
                                                              juce::Point<int> screenPos,
                                                              juce::Rectangle<int> parentArea)
{
    const auto font = juce::Font(faceFont(16.5f, true));
    const auto width = static_cast<int>(std::ceil(getTextWidth(font, tipText) + 22.0f));
    const auto height = 31;
    auto bounds = juce::Rectangle<int>(width, height).withCentre({ screenPos.x, screenPos.y - height / 2 - 3 });

    if (bounds.getY() < parentArea.getY())
        bounds.setY(screenPos.y + 8);

    return bounds.constrainedWithin(parentArea);
}

void BqtHardwareLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    g.fillAll(juce::Colours::transparentBlack);
    g.setColour(juce::Colour(0xff111111));
    g.fillRoundedRectangle(bounds.reduced(1.0f), 3.0f);
    g.setColour(juce::Colour(panelText).withAlpha(0.72f));
    g.drawRoundedRectangle(bounds.reduced(1.0f), 3.0f, 1.0f);
    g.setColour(juce::Colour(panelText));
    g.setFont(juce::Font(faceFont(16.5f, true)));
    g.drawText(text, juce::Rectangle<int>(width, height).reduced(8, 2), juce::Justification::centred);
}

BqtVuMeter::BqtVuMeter(BqtAudioProcessor& p, int sideIndex)
    : audioProcessor(p), side(sideIndex)
{
}

void BqtVuMeter::paint(juce::Graphics& g)
{
    const auto layerScale = juce::jlimit(1.0f, 4.0f,
                                         juce::jmax(renderScale * 2.0f,
                                                    juce::Component::getApproximateScaleFactorForComponent(this)));
    const auto desiredLayerWidth = static_cast<int>(std::ceil(static_cast<float>(getWidth()) * layerScale));
    const auto desiredLayerHeight = static_cast<int>(std::ceil(static_cast<float>(getHeight()) * layerScale));

    if (staticLayerWidth != desiredLayerWidth || staticLayerHeight != desiredLayerHeight
        || ! juce::approximatelyEqual(staticLayerScale, layerScale) || ! staticLayer.isValid())
        rebuildStaticLayer();

    if (staticLayer.isValid())
    {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(staticLayer, 0, 0, getWidth(), getHeight(),
                    0, 0, staticLayer.getWidth(), staticLayer.getHeight());
    }

    const auto bounds = getLocalBounds().toFloat();
    auto frameBounds = bounds.reduced(1.0f);

    const auto inner = frameBounds.withTrimmedLeft(frameBounds.getWidth() * 0.27f)
                                 .withTrimmedRight(frameBounds.getWidth() * 0.27f)
                                 .withTrimmedTop(frameBounds.getHeight() * 0.22f)
                                 .withTrimmedBottom(frameBounds.getHeight() * 0.35f);

    auto polar = [](juce::Point<float> centre, float radius, float degrees)
    {
        const auto radians = juce::degreesToRadians(degrees);
        return juce::Point<float>(centre.x + std::cos(radians) * radius,
                                  centre.y - std::sin(radians) * radius);
    };

    const auto centre = juce::Point<float>(inner.getCentreX(), inner.getY() + inner.getHeight() * 0.87f);
    const auto needlePivot = juce::Point<float>(inner.getCentreX(), inner.getY() + inner.getHeight() * 0.88f);
    const auto radius = inner.getWidth() * 0.62f;
    const auto start = 137.0f;
    const auto end = 43.0f;

    const auto meterBlack = juce::Colour(0xff1f1a17);

    const auto needleAngle = start + juce::jlimit(0.0f, 1.0f, displayedLevel) * (end - start);
    const auto needleEnd = polar(centre, radius + 7.0f, needleAngle);
    g.setColour(meterBlack);
    g.drawLine({ needlePivot, needleEnd }, 2.6f);
    g.setColour(meterBlack.darker(0.35f));
    g.fillEllipse(juce::Rectangle<float>(6.5f, 6.5f).withCentre(needlePivot));
}

void BqtVuMeter::setRenderScale(float newScale)
{
    newScale = juce::jlimit(1.0f, 3.0f, newScale);

    if (juce::approximatelyEqual(renderScale, newScale))
        return;

    renderScale = newScale;
    staticLayer = {};
    repaint();
}

void BqtVuMeter::rebuildStaticLayer()
{
    const auto logicalWidth = getWidth();
    const auto logicalHeight = getHeight();
    const auto layerScale = juce::jlimit(1.0f, 4.0f,
                                         juce::jmax(renderScale * 2.0f,
                                                    juce::Component::getApproximateScaleFactorForComponent(this)));
    staticLayerWidth = static_cast<int>(std::ceil(static_cast<float>(logicalWidth) * layerScale));
    staticLayerHeight = static_cast<int>(std::ceil(static_cast<float>(logicalHeight) * layerScale));
    staticLayerScale = layerScale;

    if (staticLayerWidth <= 0 || staticLayerHeight <= 0 || logicalWidth <= 0 || logicalHeight <= 0)
    {
        staticLayer = {};
        return;
    }

    staticLayer = juce::Image(juce::Image::ARGB, staticLayerWidth, staticLayerHeight, true);

    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(logicalWidth),
                                               static_cast<float>(logicalHeight));
    auto frameBounds = bounds.reduced(1.0f);

    const auto inner = frameBounds.withTrimmedLeft(frameBounds.getWidth() * 0.27f)
                                 .withTrimmedRight(frameBounds.getWidth() * 0.27f)
                                 .withTrimmedTop(frameBounds.getHeight() * 0.22f)
                                 .withTrimmedBottom(frameBounds.getHeight() * 0.35f);

    static auto frame = []
    {
        const auto source = juce::ImageCache::getFromMemory(BinaryData::vuframe_png, BinaryData::vuframe_pngSize);
        return source.rescaled(1000, 1000, juce::Graphics::highResamplingQuality);
    }();
    const auto frameArea = frameBounds.toNearestInt();
    if (frame.isValid())
    {
        juce::Graphics frameGraphics(staticLayer);
        frameGraphics.addTransform(juce::AffineTransform::scale(layerScale));
        frameGraphics.drawImageWithin(frame, frameArea.getX(), frameArea.getY(), frameArea.getWidth(), frameArea.getHeight(),
                                      juce::RectanglePlacement::stretchToFit, false);
    }
    vuface::paint(staticLayer, frameArea.toFloat(), layerScale);

    juce::Graphics g(staticLayer);
    g.addTransform(juce::AffineTransform::scale(layerScale));

    auto polar = [](juce::Point<float> centre, float radius, float degrees)
    {
        const auto radians = juce::degreesToRadians(degrees);
        return juce::Point<float>(centre.x + std::cos(radians) * radius,
                                  centre.y - std::sin(radians) * radius);
    };

    const auto centre = juce::Point<float>(inner.getCentreX(), inner.getY() + inner.getHeight() * 0.87f);
    const auto radius = inner.getWidth() * 0.62f;
    const auto start = 137.0f;
    const auto end = 43.0f;

    const auto meterBlack = juce::Colour(0xff1f1a17);
    const auto meterRed = juce::Colour(0xffd22b2b);

    g.setColour(meterBlack.withAlpha(0.78f));
    juce::Path arc;
    for (int i = 0; i <= 40; ++i)
    {
        const auto p = static_cast<float>(i) / 40.0f;
        const auto point = polar(centre, radius, start + (end - start) * p);
        i == 0 ? arc.startNewSubPath(point) : arc.lineTo(point);
    }
    g.strokePath(arc, juce::PathStrokeType(2.2f));

    const auto redStart = vuDbToScaleFraction(0.0f);
    juce::Path redArc;
    for (int i = 0; i <= 18; ++i)
    {
        const auto p = redStart + (1.0f - redStart) * (static_cast<float>(i) / 18.0f);
        const auto point = polar(centre, radius - 3.0f, start + (end - start) * p);
        i == 0 ? redArc.startNewSubPath(point) : redArc.lineTo(point);
    }
    g.setColour(meterRed.withAlpha(0.92f));
    g.strokePath(redArc, juce::PathStrokeType(3.4f));

    const std::array<float, 7> majorDbs { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, 0.0f, 3.0f };
    // Plain numbers; the scale carries one minus sign at its left end and one plus at its right.
    const std::array<const char*, 7> majorLabels { "20", "10", "7", "5", "3", "0", "3" };
    const auto labelFont = juce::Font(vuFont(11.07f, false));
    for (size_t i = 0; i < majorDbs.size(); ++i)
    {
        const auto frac = vuDbToScaleFraction(majorDbs[i]);
        const auto angle = start + (end - start) * frac;
        const auto red = majorDbs[i] >= 0.0f;
        g.setColour(red ? meterRed.withAlpha(0.98f)
                        : meterBlack.withAlpha(0.95f));
        g.drawLine({ polar(centre, radius - 7.0f, angle), polar(centre, radius + 3.0f, angle) },
                   majorDbs[i] == 0.0f ? 2.6f : 1.9f);
        // Centre the number on its tick, with the near edge of its ink box a fixed gap past the
        // tick end; the box's extent along the tick direction depends on the angle.
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(labelFont, majorLabels[i], 0.0f, 0.0f);
        const auto inkBox = glyphs.getBoundingBox(0, -1, true);
        const auto radians = juce::degreesToRadians(angle);
        const auto extent = std::abs(inkBox.getWidth() * 0.5f * std::cos(radians))
                          + std::abs(inkBox.getHeight() * 0.5f * std::sin(radians));
        const auto labelCentre = polar(centre, radius + 3.0f + 2.5f + extent, angle);
        glyphs.draw(g, juce::AffineTransform::translation(labelCentre - inkBox.getCentre()));
    }

    const auto drawEndSign = [&](const juce::String& sign, juce::Colour colour, float angle)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(juce::Font(vuFont(11.07f * 1.25f, true)), sign, 0.0f, 0.0f);
        g.setColour(colour);
        glyphs.draw(g, juce::AffineTransform::translation(polar(centre, radius - 2.0f, angle)
                                                         - glyphs.getBoundingBox(0, -1, true).getCentre()));
    };
    drawEndSign(juce::String::charToString(static_cast<juce::juce_wchar>(0x2212)), meterBlack.withAlpha(0.95f), start + 7.0f);
    drawEndSign("+", meterRed.withAlpha(0.98f), end - 7.0f);

    for (int db = -20; db <= 3; ++db)
    {
        if (db == -20 || db == -10 || db == -7 || db == -5 || db == -3 || db == 0 || db == 3)
            continue;
        if (db < -10 && db % 5 != 0)
            continue;

        const auto angle = start + (end - start) * vuDbToScaleFraction(static_cast<float>(db));
        const auto red = db >= 0;
        g.setColour(red ? meterRed.withAlpha(0.72f)
                        : meterBlack.withAlpha(0.58f));
        g.drawLine({ polar(centre, radius - 4.0f, angle), polar(centre, radius + 2.5f, angle) }, 1.2f);
    }

    g.setColour(meterBlack.withAlpha(0.98f));
    g.setFont(juce::Font(vuFont(19.0f * 0.98f, true)));
    g.drawText("vu", inner.withTrimmedTop(inner.getHeight() * 0.38f).withHeight(22.0f).toNearestInt(),
               juce::Justification::centred);
}

bool BqtVuMeter::updateLevel(double secondsElapsed)
{
    const auto raw = audioProcessor.getMeterLevel(side);
    const auto db = juce::Decibels::gainToDecibels(raw, -60.0f);
    targetLevel = vuDbToScaleFraction(db + 18.0f);
    // Time-based smoothing. This was a fixed 0.18 per tick, which meant the needle's speed was a
    // function of how punctually the timer fired rather than of elapsed time: a late or dropped
    // frame still advanced only 18% of the remaining distance, so jitter turned into visibly
    // uneven, steppy motion instead of degrading gracefully. Now a late frame covers exactly the
    // distance it should have.
    constexpr auto needleTimeConstantSeconds = 0.09;
    const auto clampedElapsed = juce::jlimit(0.0, 0.25, secondsElapsed);
    const auto alpha = static_cast<float>(1.0 - std::exp(-clampedElapsed / needleTimeConstantSeconds));

    const auto previousLevel = displayedLevel;
    displayedLevel += (targetLevel - displayedLevel) * alpha;

    if (std::abs(targetLevel - displayedLevel) < 0.0005f)
        displayedLevel = targetLevel;

    return std::abs(displayedLevel - previousLevel) >= 0.0001f;
}
