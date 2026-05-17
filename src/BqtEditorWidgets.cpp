#include "BqtEditorWidgets.h"

#include "BinaryData.h"
#include "BqtEditorStyle.h"

#include <array>
#include <cmath>

using namespace bqst::ui;

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

        const auto topColour = pressed ? juce::Colour(0xffeee4dc) : juce::Colour(cream);
        const auto bottomColour = pressed ? juce::Colour(0xffcfc4bb) : juce::Colour(0xffded3ca);
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
    juce::Graphics g(staticLayer);
    g.addTransform(juce::AffineTransform::scale(layerScale));

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
    if (frame.isValid())
    {
        const auto frameArea = frameBounds.toNearestInt();
        g.drawImageWithin(frame, frameArea.getX(), frameArea.getY(), frameArea.getWidth(), frameArea.getHeight(),
                          juce::RectanglePlacement::stretchToFit, false);
    }

    auto polar = [](juce::Point<float> centre, float radius, float degrees)
    {
        const auto radians = juce::degreesToRadians(degrees);
        return juce::Point<float>(centre.x + std::cos(radians) * radius,
                                  centre.y - std::sin(radians) * radius);
    };

    auto dbToFrac = [](float db)
    {
        const auto clamped = juce::jlimit(-20.0f, 3.0f, db);
        if (clamped <= 0.0f)
            return ((clamped + 20.0f) / 20.0f) * 0.82f;
        return 0.82f + (clamped / 3.0f) * 0.18f;
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

    const auto redStart = dbToFrac(0.0f);
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
    const std::array<const char*, 7> majorLabels { "-20", "-10", "-7", "-5", "-3", "0", "+3" };
    g.setFont(juce::Font(faceFont(11.3f, false)));
    for (size_t i = 0; i < majorDbs.size(); ++i)
    {
        const auto frac = dbToFrac(majorDbs[i]);
        const auto angle = start + (end - start) * frac;
        const auto red = majorDbs[i] >= 0.0f;
        g.setColour(red ? meterRed.withAlpha(0.98f)
                        : meterBlack.withAlpha(0.95f));
        g.drawLine({ polar(centre, radius - 7.0f, angle), polar(centre, radius + 3.0f, angle) },
                   majorDbs[i] == 0.0f ? 2.6f : 1.9f);
        const auto labelPoint = polar(centre, radius + 9.0f, angle);
        g.drawText(majorLabels[i], juce::Rectangle<float>(34.0f, 15.0f).withCentre(labelPoint).toNearestInt(),
                   juce::Justification::centred);
    }

    for (int db = -20; db <= 3; ++db)
    {
        if (db == -20 || db == -10 || db == -7 || db == -5 || db == -3 || db == 0 || db == 3)
            continue;
        if (db < -10 && db % 5 != 0)
            continue;

        const auto angle = start + (end - start) * dbToFrac(static_cast<float>(db));
        const auto red = db >= 0;
        g.setColour(red ? meterRed.withAlpha(0.72f)
                        : meterBlack.withAlpha(0.58f));
        g.drawLine({ polar(centre, radius - 4.0f, angle), polar(centre, radius + 2.5f, angle) }, 1.2f);
    }

    g.setColour(meterBlack.withAlpha(0.98f));
    g.setFont(juce::Font(faceFont(19.0f, true)));
    g.drawText("vu", inner.withTrimmedTop(inner.getHeight() * 0.38f).withHeight(22.0f).toNearestInt(),
               juce::Justification::centred);
}

bool BqtVuMeter::updateLevel()
{
    const auto raw = audioProcessor.getMeterLevel(side);
    const auto db = juce::Decibels::gainToDecibels(raw, -60.0f);
    const auto vu = db + 18.0f;
    const auto clampedVu = juce::jlimit(-20.0f, 3.0f, vu);
    targetLevel = clampedVu <= 0.0f ? ((clampedVu + 20.0f) / 20.0f) * 0.82f
                                    : 0.82f + (clampedVu / 3.0f) * 0.18f;
    const auto previousLevel = displayedLevel;
    displayedLevel += (targetLevel - displayedLevel) * 0.18f;

    if (std::abs(targetLevel - displayedLevel) < 0.0005f)
        displayedLevel = targetLevel;

    return std::abs(displayedLevel - previousLevel) >= 0.0001f;
}
