#include "BqtEditorWidgets.h"

#include "BinaryData.h"
#include "BqtEditorStyle.h"

#include <array>
#include <cmath>

using namespace bqst::ui;

namespace
{
bool hasFlag(const juce::Component& component, const char* flag)
{
    return static_cast<bool>(component.getProperties().getWithDefault(flag, false));
}

bool isFlatBar(const juce::Component& component) { return hasFlag(component, "bqtFlatBar"); }

// Offscreen pictures of the hover and pressed looks set this, since no pointer is present there.
bool previewHot(const juce::Component& component) { return hasFlag(component, "bqtPreviewHot"); }

juce::Colour inkAlpha(float alpha) { return juce::Colour(ink).withAlpha(alpha); }

// Keyboard focus shows as a thin outline, only after Tab; on a solid ink fill it is drawn in cream.
void drawFocusRing(juce::Graphics& g, bool ringsVisible, const juce::Component& component,
                   juce::Rectangle<float> area, bool onFill)
{
    if (! ringsVisible || ! component.hasKeyboardFocus(false))
        return;
    g.setColour(onFill ? juce::Colour(barField) : inkAlpha(0.85f));
    g.drawRoundedRectangle(area.reduced(onFill ? 2.5f : 0.75f), fieldCorner, 1.5f);
}

// The bar's one field shape: a flat rounded rectangle with no outline, cream when off, solid ink
// when on; under the pointer or pressed, cream turns white and ink lifts a little. Returns the
// field's bounds.
juce::Rectangle<float> drawFlatBarField(juce::Graphics& g, bool ringsVisible, juce::Component& component, bool on,
                                        bool highlighted, bool down)
{
    const auto bounds = component.getLocalBounds().toFloat().reduced(1.0f);
    const auto enabled = component.isEnabled();
    auto fill = on ? juce::Colour(ink) : juce::Colour(barField);
    if (enabled && (highlighted || down))
        fill = on ? fill.brighter(0.25f) : juce::Colours::white;
    g.setColour(fill.withMultipliedAlpha(enabled ? 1.0f : 0.5f));
    g.fillRoundedRectangle(bounds, fieldCorner);
    drawFocusRing(g, ringsVisible, component, bounds, on);
    return bounds;
}

// A dropdown's chevron, in the right end of its field. Returns what is left for the text, which
// stays centred and clear of the chevron on both sides.
juce::Rectangle<float> drawDropdownChevron(juce::Graphics& g, juce::Rectangle<float> field)
{
    auto textArea = field.reduced(6.0f, 0.0f);
    const auto c = textArea.removeFromRight(18.0f).getCentre();
    textArea.removeFromLeft(18.0f);
    juce::Path chevron;
    chevron.startNewSubPath(c.x - 4.0f, c.y - 2.0f);
    chevron.lineTo(c.x, c.y + 2.0f);
    chevron.lineTo(c.x + 4.0f, c.y - 2.0f);
    g.setColour(inkAlpha(0.75f));
    g.strokePath(chevron, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    return textArea;
}

void drawFlatBarButton(juce::Graphics& g, bool ringsVisible, juce::Button& button, bool on, bool highlighted, bool down)
{
    const auto bounds = drawFlatBarField(g, ringsVisible, button, on, highlighted, down);
    auto textArea = bounds.reduced(6.0f, 0.0f);
    if (hasFlag(button, "bqtDropdown"))
        textArea = drawDropdownChevron(g, bounds);

    g.setColour(on ? juce::Colour(barField) : inkAlpha(button.isEnabled() ? 0.85f : 0.3f));
    g.setFont(juce::Font(faceFont(15.0f)));
    g.drawFittedText(button.getButtonText(), textArea.toNearestInt(), juce::Justification::centred, 1, 0.8f);
}
} // namespace

namespace
{
// Both accessibility actions select through the click callback. JUCE's default toggle
// action flips a ToggleButton independently of its owner's parameter attachment.
class SwitchHalf final : public juce::ToggleButton
{
public:
    using juce::ToggleButton::ToggleButton;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        class Handler final : public juce::AccessibilityHandler
        {
        public:
            explicit Handler(SwitchHalf& half)
                : juce::AccessibilityHandler(half, juce::AccessibilityRole::radioButton,
                    juce::AccessibilityActions()
                        .addAction(juce::AccessibilityActionType::press, [&half] { if (half.onClick) half.onClick(); })
                        .addAction(juce::AccessibilityActionType::toggle, [&half] { if (half.onClick) half.onClick(); })),
                  button(half) {}

            juce::AccessibleState getCurrentState() const override
            {
                auto state = juce::AccessibilityHandler::getCurrentState().withCheckable();
                return button.getToggleState() ? state.withChecked() : state;
            }
        private:
            SwitchHalf& button;
        };
        return std::make_unique<Handler>(*this);
    }
};
} // namespace

//==============================================================================
BqtSwitch::BqtSwitch(const juce::String& leftText, const juce::String& rightText)
{
    // Radio groups match among siblings, and the halves are this switch's only children.
    constexpr int radioGroup = 1;
    const juce::String texts[] { leftText, rightText };
    for (size_t index = 0; index < halves.size(); ++index)
    {
        auto half = std::make_unique<SwitchHalf>(texts[index]);
        half->getProperties().set("bqtSwitchHalf", true);
        half->setRadioGroupId(radioGroup, juce::dontSendNotification);
        half->setClickingTogglesState(false);
        half->addMouseListener(this, false);
        half->onClick = [this, index]
        {
            if (static_cast<int>(index) != selected && onSelect)
                onSelect(static_cast<int>(index));
            setSelected(selected);
        };
        addAndMakeVisible(*half);
        halves[index] = std::move(half);
    }
    halves[0]->setToggleState(true, juce::dontSendNotification);
}

BqtSwitch::~BqtSwitch()
{
    frameClock = {};
}

void BqtSwitch::setSelected(int index)
{
    index = juce::jlimit(0, static_cast<int>(halves.size()) - 1, index);
    for (size_t i = 0; i < halves.size(); ++i)
        halves[i]->setToggleState(static_cast<int>(i) == index, juce::dontSendNotification);

    if (selected == index)
        return;
    selected = index;

    // Slides only where someone can see it; a hidden or offscreen switch jumps straight there.
    if (isShowing() && ! getLocalBounds().isEmpty())
    {
        slideStart = pill;
        slideStartedAt = juce::Time::getMillisecondCounterHiRes();
        if (frameClock.isEmpty())
            frameClock = juce::VBlankAttachment(this, [this] { animationFrame(); });
    }
    else
    {
        snapPill();
    }
    repaint();
}

void BqtSwitch::snapPill()
{
    frameClock = {};
    pill = halves[static_cast<size_t>(selected)]->getBounds().toFloat();
}

// Ease-out: quick to leave, settling into place, 120 ms end to end.
void BqtSwitch::animationFrame()
{
    if (! isShowing())
    {
        snapPill();
        repaint();
        return;
    }

    const auto t = static_cast<float>(juce::jlimit(0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - slideStartedAt) / slideDurationMs));
    const auto target = halves[static_cast<size_t>(selected)]->getBounds().toFloat();
    if (t >= 1.0f)
    {
        snapPill();
    }
    else
    {
        const auto remaining = 1.0f - t;
        const auto eased = 1.0f - remaining * remaining * remaining;
        pill = { slideStart.getX() + (target.getX() - slideStart.getX()) * eased, target.getY(),
                 slideStart.getWidth() + (target.getWidth() - slideStart.getWidth()) * eased, target.getHeight() };
    }
    repaint();
}

void BqtSwitch::resized()
{
    const auto area = getLocalBounds().reduced(1);
    const auto width = area.getWidth();
    for (size_t i = 0; i < halves.size(); ++i)
    {
        const auto left = static_cast<int>(i) * width / static_cast<int>(halves.size());
        const auto right = static_cast<int>(i + 1) * width / static_cast<int>(halves.size());
        halves[i]->setBounds(area.getX() + left, area.getY(), right - left, area.getHeight());
    }
    snapPill();
}

void BqtSwitch::paint(juce::Graphics& g)
{
    g.setColour(isMouseOver(true) || previewHot(*this) ? juce::Colours::white : juce::Colour(barField));
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), fieldCorner);
    g.setColour(juce::Colour(ink));
    g.fillRoundedRectangle(pill, fieldCorner);
}

//==============================================================================
void BqtHardwareLookAndFeel::showMenuUnder(juce::PopupMenu& menu, juce::Component& target, std::function<void(int)> onResult)
{
    // A PopupMenu does not take its target's look-and-feel by itself.
    menu.setLookAndFeel(&target.getLookAndFeel());
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(&target)
                           .withMinimumWidth(target.getWidth())
                           .withStandardItemHeight(menuRowHeight),
                       [safe = juce::Component::SafePointer<juce::Component>(&target), callback = std::move(onResult)](int result)
                       {
                           if (safe != nullptr && callback)
                               callback(result);
                       });
}

juce::PopupMenu::Options BqtHardwareLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label)
{
    return LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label).withStandardItemHeight(menuRowHeight);
}

void BqtReadoutBubble::setText(juce::String newText)
{
    if (text == newText)
        return;

    text = std::move(newText);
    repaint();
}

bqst::vu::OpaqueCoverShape BqtReadoutBubble::opaqueShape() const
{
    return { getLocalBounds().toFloat().reduced(1.0f), fieldCorner };
}

void BqtReadoutBubble::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff111111));
    g.fillPath(opaqueShape().toPath()); // flat, like a switched-on bar button
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
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(menuPanel));
    setColour(juce::PopupMenu::textColourId, juce::Colour(ink));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(panelPink));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(ink));
    setColour(juce::PopupMenu::headerTextColourId, juce::Colour(ink).withAlpha(0.6f));
}

// Menus: a plain cream panel with a hairline edge, a rounded pink highlight under the pointer, and
// the current choice shown by weight and a quiet tint, with no marker beside it. Only the drawing
// changes; keyboard and mouse handling stay JUCE's own.
juce::Font BqtHardwareLookAndFeel::getPopupMenuFont()
{
    return juce::Font(faceFont(16.0f, false));
}

void BqtHardwareLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(juce::Colour(menuPanel));
    g.setColour(inkAlpha(0.25f));
    g.drawRect(0, 0, width, height, 1);
}

void BqtHardwareLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                               bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                               const juce::String& text, const juce::String&,
                                               const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour(inkAlpha(0.15f));
        g.fillRect(area.reduced(8, 0).withHeight(1).withY(area.getCentreY()));
        return;
    }

    auto row = area.reduced(4, 1).toFloat();
    if (isHighlighted && isActive)
    {
        g.setColour(juce::Colour(panelPink));
        g.fillRoundedRectangle(row, 4.0f);
    }
    else if (isTicked)
    {
        g.setColour(inkAlpha(0.06f));
        g.fillRoundedRectangle(row, 4.0f);
    }
    row.removeFromLeft(14.0f);

    if (hasSubMenu)
    {
        const auto arrow = row.removeFromRight(18.0f);
        juce::Path chevron;
        chevron.startNewSubPath(arrow.getCentreX() - 2.0f, arrow.getCentreY() - 4.0f);
        chevron.lineTo(arrow.getCentreX() + 2.0f, arrow.getCentreY());
        chevron.lineTo(arrow.getCentreX() - 2.0f, arrow.getCentreY() + 4.0f);
        g.setColour(inkAlpha(0.7f));
        g.strokePath(chevron, juce::PathStrokeType(1.5f));
    }

    g.setColour(inkAlpha(isActive ? 1.0f : 0.4f));
    g.setFont(juce::Font(faceFont(16.0f, isTicked)));
    g.drawText(text, row, juce::Justification::centredLeft);
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
        // Flat, like the bar's fields: a cream disc that turns white under the pointer or while
        // dragged, an ink pointer, and a thin ring once Tab gives it focus.
        g.setColour(slider.isMouseOverOrDragging() || previewHot(slider) ? juce::Colours::white : juce::Colour(barField));
        g.fillEllipse(mini);
        if (focusRingsVisible && slider.hasKeyboardFocus(false))
        {
            g.setColour(inkAlpha(0.85f));
            g.drawEllipse(mini.reduced(0.75f), 1.5f);
        }

        const auto pointerStart = mini.getCentre().getPointOnCircumference(mini.getWidth() * 0.12f, angle);
        const auto pointerEnd = mini.getCentre().getPointOnCircumference(mini.getWidth() * 0.38f, angle);
        g.setColour(juce::Colour(ink));
        g.drawLine({ pointerStart, pointerEnd }, 2.2f);
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
    if (isFlatBar(box))
    {
        const auto hot = box.isMouseOver(true) || previewHot(box);
        drawDropdownChevron(g, drawFlatBarField(g, focusRingsVisible, box, false, hot, isButtonDown));
        return;
    }

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
            if (! on) // a lit lamp needs no rim
            {
                g.setColour(juce::Colour(ink));
                g.drawEllipse(led, 1.0f);
            }
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

    if (isFlatBar(box))
    {
        // Text centred in the field and kept clear of the chevron on both sides, as on every
        // dropdown in the bar (see drawDropdownChevron).
        label.setBounds(25, 1, juce::jmax(0, box.getWidth() - 50), box.getHeight() - 2);
        label.setBorderSize({});
        label.setFont(juce::Font(faceFont(15.0f)));
        label.setColour(juce::Label::textColourId, inkAlpha(box.isEnabled() ? 0.85f : 0.3f));
        label.setMinimumHorizontalScale(0.8f);
        label.setJustificationType(juce::Justification::centred);
        return;
    }

    label.setBounds(7, 1, box.getWidth() - 21, box.getHeight() - 2);
    label.setFont(juce::Font(faceFont(15.8f)));
    label.setJustificationType(juce::Justification::centred);
}

void BqtHardwareLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    if (isFlatBar(button))
    {
        drawFlatBarButton(g, focusRingsVisible, button, button.getToggleState(), shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        return;
    }

    if (hasFlag(button, "bqtSwitchHalf"))
    {
        // The switch paints the field and the sliding pill; a half draws its word in ink, then
        // again in cream clipped to the pill, so the word changes colour as the pill passes.
        const auto area = button.getLocalBounds().toFloat();
        drawFocusRing(g, focusRingsVisible, button, area, button.getToggleState());
        g.setFont(juce::Font(faceFont(15.0f)));
        g.setColour(inkAlpha(0.75f));
        g.drawText(button.getButtonText(), area, juce::Justification::centred);
        if (auto* owner = dynamic_cast<BqtSwitch*>(button.getParentComponent()))
        {
            const auto fill = owner->pillBounds().translated(-static_cast<float>(button.getX()), -static_cast<float>(button.getY()));
            if (fill.intersects(area))
            {
                juce::Graphics::ScopedSaveState save(g);
                juce::Path clip;
                clip.addRoundedRectangle(fill, fieldCorner);
                g.reduceClipRegion(clip);
                g.setColour(juce::Colour(barField));
                g.drawText(button.getButtonText(), area, juce::Justification::centred);
            }
        }
        return;
    }

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

    g.setColour(juce::Colour(plateInk));
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
    if (! on) // a lit lamp needs no rim
    {
        g.setColour(juce::Colour(plateInk));
        g.drawEllipse(led, 1.0f);
    }
}

void BqtHardwareLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                  const juce::Colour&, bool shouldDrawButtonAsHighlighted,
                                                  bool shouldDrawButtonAsDown)
{
    if (isFlatBar(button))
    {
        drawFlatBarButton(g, focusRingsVisible, button, button.getToggleState(), shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        return;
    }

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

        g.setColour(juce::Colour(plateInk));
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
        if (! on) // a lit lamp needs no rim
        {
            g.setColour(juce::Colour(plateInk));
            g.drawEllipse(led, 1.0f);
        }
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
    g.fillRoundedRectangle(bounds.reduced(1.0f), fieldCorner); // flat, like a switched-on bar button
    g.setColour(juce::Colour(panelText));
    g.setFont(juce::Font(faceFont(16.5f, true)));
    g.drawText(text, juce::Rectangle<int>(width, height).reduced(8, 2), juce::Justification::centred);
}
