#include "PluginEditor.h"

#include "BinaryData.h"

#include <array>
#include <cmath>

namespace
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
const juce::StringArray lowFreqLabels { "74", "84", "98", "116", "131", "166", "230", "361" };
const juce::StringArray highFreqLabels { "1.6k", "1.8k", "2.1k", "2.5k", "3.4k", "4.8k", "7.1k", "18k" };

juce::String indexedLabel(const juce::StringArray& labels, double value)
{
    return labels[juce::jlimit(0, labels.size() - 1, static_cast<int>(std::round(value)))];
}

juce::String sidePrefix(int sideIndex)
{
    return sideIndex == 0 ? "a" : "b";
}

juce::FontOptions faceFont(float height, bool bold = true)
{
    static auto regular = juce::Typeface::createSystemTypefaceFor(BinaryData::ChillaxRegular_otf,
                                                                  BinaryData::ChillaxRegular_otfSize);
    static auto semibold = juce::Typeface::createSystemTypefaceFor(BinaryData::ChillaxSemibold_otf,
                                                                   BinaryData::ChillaxSemibold_otfSize);
    return juce::FontOptions(bold && semibold != nullptr ? semibold : regular).withHeight(height);
}

juce::Rectangle<float> getRackFaceBounds(juce::Rectangle<float> available)
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

void drawPanelText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> bounds,
                   juce::Justification justification, float height, bool bold = true)
{
    g.setFont(juce::Font(faceFont(height, bold)));
    g.setColour(juce::Colour(panelText));
    g.drawText(text, bounds.toNearestInt(), justification);
}

float getTextWidth(const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox(0, -1, true).getWidth();
}

void drawRotatedImageKnob(juce::Graphics& g, const juce::Image& image, juce::Rectangle<float> bounds, float angle,
                          float scale = 1.0f)
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
} // namespace

BqtAudioProcessorEditor::BqtAudioProcessorEditor(BqtAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), meterA(p, 0), meterB(p, 1)
{
    setLookAndFeel(&hardwareLookAndFeel);
    setSize(900, 820);

    configureCombo(eqMode);
    configureCombo(satMode);
    configureCombo(osRealtime);
    configureCombo(osRender);
    configureLabel(inputTrimLabel, "input", juce::Justification::centredLeft);
    inputTrimLabel.setColour(juce::Label::textColourId, juce::Colour(cream).withAlpha(0.84f));
    inputTrimLabel.setFont(faceFont(19.5f));
    configureSlider(inputTrim);
    inputTrim.getProperties().set("bqtTopInputKnob", true);
    inputTrim.setDoubleClickReturnValue(true, 0.0);
    addAndMakeVisible(autoGain);
    addAndMakeVisible(eqBypass);
    addAndMakeVisible(satBypass);
    addAndMakeVisible(eqLink);
    addAndMakeVisible(satLink);
    addAndMakeVisible(vintage);
    addAndMakeVisible(bypass);
    addAndMakeVisible(meterA);
    addAndMakeVisible(meterB);
    meterA.setInterceptsMouseClicks(false, false);
    meterB.setInterceptsMouseClicks(false, false);

    autoGain.setButtonText("autogain");
    eqBypass.setButtonText("eq in");
    satBypass.setButtonText("sat in");
    eqLink.setButtonText("eq link");
    satLink.setButtonText("sat link");
    vintage.setButtonText("vint.");
    bypass.setButtonText("bypass");
    eqMode.addItemList(juce::StringArray { "eq l/r", "eq m/s" }, 1);
    satMode.addItemList(juce::StringArray { "sat l/r", "sat m/s" }, 1);
    osRealtime.addItemList(juce::StringArray { "rt off", "rt 2x", "rt 4x", "rt 8x" }, 1);
    osRender.addItemList(juce::StringArray { "rn off", "rn 2x", "rn 4x", "rn 8x" }, 1);

    for (auto* button : { &autoGain, &eqBypass, &satBypass, &eqLink, &satLink, &bypass })
        button->getProperties().set("bqtPushButton", true);

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

    auto mirrorSlider = [](juce::Slider& source, juce::Slider& dest)
    {
        dest.setValue(source.getValue(), juce::sendNotificationSync);
    };

    auto& left = sideControls[0];
    auto& right = sideControls[1];
    left.lowGain.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (audioProcessor.state().getRawParameterValue("eqLink")->load() > 0.5f)
            mirrorSlider(left.lowGain, right.lowGain);
    };
    left.lowFreq.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (audioProcessor.state().getRawParameterValue("eqLink")->load() > 0.5f)
            mirrorSlider(left.lowFreq, right.lowFreq);
    };
    left.highGain.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (audioProcessor.state().getRawParameterValue("eqLink")->load() > 0.5f)
            mirrorSlider(left.highGain, right.highGain);
    };
    left.highFreq.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (audioProcessor.state().getRawParameterValue("eqLink")->load() > 0.5f)
            mirrorSlider(left.highFreq, right.highFreq);
    };

    left.drive.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (audioProcessor.state().getRawParameterValue("satLink")->load() > 0.5f)
            mirrorSlider(left.drive, right.drive);
    };
    left.mix.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (audioProcessor.state().getRawParameterValue("satLink")->load() > 0.5f)
            mirrorSlider(left.mix, right.mix);
    };
    left.outputTrim.onValueChange = [this, &left, &right, mirrorSlider]
    {
        if (audioProcessor.state().getRawParameterValue("satLink")->load() > 0.5f)
            mirrorSlider(left.outputTrim, right.outputTrim);
    };
    left.satType.onChange = [this, &left, &right]
    {
        if (audioProcessor.state().getRawParameterValue("satLink")->load() > 0.5f)
            right.satType.setSelectedItemIndex(left.satType.getSelectedItemIndex(), juce::sendNotificationSync);
    };

    startTimerHz(12);
    updateLinkedControlStates();
}

BqtAudioProcessorEditor::~BqtAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void BqtAudioProcessorEditor::configureSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setVelocityBasedMode(false);
    slider.setVelocityModeParameters(0.35, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    slider.setTooltip("Drag to adjust. Hold Shift for fine movement. Double-click the value to type.");
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
    configureLabel(controls.highFreqLabel, "freq");
    configureSlider(controls.highFreq);
    controls.highFreq.getProperties().set("bqtKnobCombo", true);
    controls.highFreq.setRange(0.0, 7.0, 1.0);
    configureLabel(controls.lowGainLabel, "lf");
    configureSlider(controls.lowGain);
    controls.lowGain.getProperties().set("bqtLargeCreamKnob", true);
    configureLabel(controls.lowFreqLabel, "freq");
    configureSlider(controls.lowFreq);
    controls.lowFreq.getProperties().set("bqtKnobCombo", true);
    controls.lowFreq.setRange(0.0, 7.0, 1.0);
    configureLabel(controls.driveLabel, "drive");
    configureSlider(controls.drive);
    controls.drive.getProperties().set("bqtLargeCreamKnob", true);
    configureLabel(controls.satTypeLabel, "sat. type");
    configureCombo(controls.satType);
    controls.satType.setVisible(false);
    controls.satTypeButton.getProperties().set("bqtSatTypeSelector", true);
    controls.satTypeButton.setButtonText("sat type");
    addAndMakeVisible(controls.satTypeButton);
    configureLabel(controls.mixLabel, "mix");
    configureSlider(controls.mix);
    controls.mix.getProperties().set("bqtSmallCreamKnob", true);
    configureLabel(controls.outputTrimLabel, "output");
    configureSlider(controls.outputTrim);
    controls.outputTrim.getProperties().set("bqtSmallCreamKnob", true);
    controls.lowGain.setDoubleClickReturnValue(true, 0.0);
    controls.highGain.setDoubleClickReturnValue(true, 0.0);
    controls.drive.setDoubleClickReturnValue(true, 0.0);
    controls.mix.setDoubleClickReturnValue(true, 100.0);
    controls.outputTrim.setDoubleClickReturnValue(true, 0.0);
    controls.satType.addItemList(juce::StringArray { "cream", "grit" }, 1);
    controls.satTypeButton.onClick = [&combo = controls.satType, &button = controls.satTypeButton]
    {
        const auto next = combo.getSelectedItemIndex() == 0 ? 1 : 0;
        combo.setSelectedItemIndex(next, juce::sendNotificationSync);
        button.setToggleState(next == 1, juce::dontSendNotification);
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
}

void BqtAudioProcessorEditor::timerCallback()
{
    const auto eqIsIn = audioProcessor.state().getRawParameterValue("eqBypass")->load() < 0.5f;
    const auto satIsIn = audioProcessor.state().getRawParameterValue("satBypass")->load() < 0.5f;
    eqBypass.setToggleState(eqIsIn, juce::dontSendNotification);
    satBypass.setToggleState(satIsIn, juce::dontSendNotification);

    for (auto& controls : sideControls)
        controls.satTypeButton.setToggleState(controls.satType.getSelectedItemIndex() == 1, juce::dontSendNotification);

    updateLinkedControlStates();
    updateDynamicTooltips();
}

void BqtAudioProcessorEditor::updateDynamicTooltips()
{
    inputTrim.setTooltip("input " + juce::String(inputTrim.getValue(), 1) + " dB");

    for (auto& controls : sideControls)
    {
        controls.highGain.setTooltip("hf " + juce::String(controls.highGain.getValue(), 1) + " dB");
        controls.lowGain.setTooltip("lf " + juce::String(controls.lowGain.getValue(), 1) + " dB");
        controls.drive.setTooltip("drive " + juce::String(controls.drive.getValue(), 1) + " dB");
        controls.mix.setTooltip("mix " + juce::String(controls.mix.getValue(), 1) + "%");
        controls.outputTrim.setTooltip("output " + juce::String(controls.outputTrim.getValue(), 1) + " dB");
        controls.highFreq.setTooltip("hf freq " + indexedLabel(highFreqLabels, controls.highFreq.getValue()));
        controls.lowFreq.setTooltip("lf freq " + indexedLabel(lowFreqLabels, controls.lowFreq.getValue()));
        controls.satTypeButton.setTooltip("sat type " + controls.satType.getText());
    }
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

BqtAudioProcessorEditor::HardwareLookAndFeel::HardwareLookAndFeel()
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

void BqtAudioProcessorEditor::HardwareLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                                                    float sliderPos, float rotaryStartAngle,
                                                                    float rotaryEndAngle, juce::Slider& slider)
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
        static auto image = juce::ImageCache::getFromMemory(BinaryData::knoblargeskirted_png,
                                                            BinaryData::knoblargeskirted_pngSize);
        drawRotatedImageKnob(g, image, knobArea, angle, 1.18f);
        return;
    }

    if (slider.getProperties().contains("bqtSmallCreamKnob") && static_cast<bool>(slider.getProperties()["bqtSmallCreamKnob"]))
    {
        static auto image = juce::ImageCache::getFromMemory(BinaryData::knobsmallpointer_png,
                                                            BinaryData::knobsmallpointer_pngSize);
        drawRotatedImageKnob(g, image, knobArea, angle, 1.08f);
        return;
    }

    if (slider.getProperties().contains("bqtKnobCombo") && static_cast<bool>(slider.getProperties()["bqtKnobCombo"]))
    {
        static auto image = juce::ImageCache::getFromMemory(BinaryData::knobsmallpointer_png,
                                                            BinaryData::knobsmallpointer_pngSize);
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

void BqtAudioProcessorEditor::HardwareLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
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

        static auto image = juce::ImageCache::getFromMemory(BinaryData::knobsmallpointer_png,
                                                            BinaryData::knobsmallpointer_pngSize);
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

    const auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f);
    g.setColour(isButtonDown ? juce::Colour(0xffefe1d8) : juce::Colour(cream));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(juce::Colour(panelText));
    g.drawRoundedRectangle(bounds, 4.0f, 1.5f);

    juce::Path arrow;
    const auto cx = bounds.getRight() - 13.0f;
    const auto cy = bounds.getCentreY();
    arrow.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.fillPath(arrow);
    box.setColour(juce::Label::textColourId, juce::Colour(ink));
}

void BqtAudioProcessorEditor::HardwareLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
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

    label.setBounds(10, 1, box.getWidth() - 27, box.getHeight() - 2);
    label.setFont(juce::Font(faceFont(18.5f)));
    label.setJustificationType(juce::Justification::centredLeft);
}

void BqtAudioProcessorEditor::HardwareLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                                                    bool shouldDrawButtonAsHighlighted,
                                                                    bool shouldDrawButtonAsDown)
{
    if (button.getProperties().contains("bqtPushButton") && static_cast<bool>(button.getProperties()["bqtPushButton"]))
    {
        const auto toggled = button.getToggleState();
        const auto pressed = shouldDrawButtonAsDown || toggled;
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f, 2.0f);

        g.setColour(juce::Colours::black.withAlpha(pressed ? 0.18f : 0.42f));
        g.fillRoundedRectangle(bounds.translated(0.0f, pressed ? 1.0f : 3.0f), 4.0f);

        if (pressed)
            bounds = bounds.reduced(2.0f).translated(0.0f, 1.0f);

        const auto topColour = pressed ? juce::Colour(0xffd6cdc5) : juce::Colour(cream);
        const auto bottomColour = pressed ? juce::Colour(0xffb7ada5) : juce::Colour(0xffded3ca);
        juce::ColourGradient buttonGradient(topColour, bounds.getCentreX(), bounds.getY(),
                                            bottomColour, bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(buttonGradient);
        g.fillRoundedRectangle(bounds, 4.0f);

        g.setColour(pressed ? juce::Colours::black.withAlpha(0.22f) : juce::Colours::white.withAlpha(0.45f));
        g.drawRoundedRectangle(bounds.reduced(1.0f), 3.0f, 1.0f);
        g.setColour(juce::Colour(panelText));
        g.drawRoundedRectangle(bounds, 4.0f, 1.2f);

        if (shouldDrawButtonAsHighlighted && ! pressed)
        {
            g.setColour(juce::Colours::white.withAlpha(0.18f));
            g.fillRoundedRectangle(bounds.reduced(2.0f), 3.0f);
        }

        g.setColour(juce::Colour(ink).withAlpha(button.isEnabled() ? 1.0f : 0.38f));
        g.setFont(juce::Font(faceFont(18.0f)));
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

void BqtAudioProcessorEditor::HardwareLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                                        const juce::Colour&, bool,
                                                                        bool shouldDrawButtonAsDown)
{
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

void BqtAudioProcessorEditor::HardwareLookAndFeel::drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool)
{
}

BqtAudioProcessorEditor::VuMeter::VuMeter(BqtAudioProcessor& p, int sideIndex)
    : audioProcessor(p), side(sideIndex)
{
    startTimerHz(24);
}

void BqtAudioProcessorEditor::VuMeter::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    auto frameBounds = bounds.reduced(1.0f);

    const auto inner = frameBounds.withTrimmedLeft(frameBounds.getWidth() * 0.27f)
                                 .withTrimmedRight(frameBounds.getWidth() * 0.27f)
                                 .withTrimmedTop(frameBounds.getHeight() * 0.22f)
                                 .withTrimmedBottom(frameBounds.getHeight() * 0.35f);

    static auto frame = juce::ImageCache::getFromMemory(BinaryData::vuframe_png, BinaryData::vuframe_pngSize);
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
    const auto needlePivot = juce::Point<float>(inner.getCentreX(), inner.getY() + inner.getHeight() * 0.88f);
    const auto radius = inner.getWidth() * 0.62f;
    const auto start = 137.0f;
    const auto end = 43.0f;

    g.setColour(juce::Colour(0xff6f5e52).withAlpha(0.74f));
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
    g.setColour(juce::Colour(0xffa13f44).withAlpha(0.82f));
    g.strokePath(redArc, juce::PathStrokeType(3.4f));

    const std::array<float, 7> majorDbs { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, 0.0f, 3.0f };
    const std::array<const char*, 7> majorLabels { "-20", "-10", "-7", "-5", "-3", "0", "+3" };
    g.setFont(juce::Font(faceFont(11.3f, false)));
    for (size_t i = 0; i < majorDbs.size(); ++i)
    {
        const auto frac = dbToFrac(majorDbs[i]);
        const auto angle = start + (end - start) * frac;
        const auto red = majorDbs[i] >= 0.0f;
        g.setColour(red ? juce::Colour(0xff99353b).withAlpha(0.96f)
                        : juce::Colour(0xff6b5a4f).withAlpha(0.94f));
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
        g.setColour(red ? juce::Colour(0xff99353b).withAlpha(0.62f)
                        : juce::Colour(0xff6b5a4f).withAlpha(0.52f));
        g.drawLine({ polar(centre, radius - 4.0f, angle), polar(centre, radius + 2.5f, angle) }, 1.2f);
    }

    g.setColour(juce::Colour(0xff594a42).withAlpha(0.92f));
    g.setFont(juce::Font(faceFont(15.0f)));
    g.drawText("VU", inner.withTrimmedTop(inner.getHeight() * 0.38f).withHeight(22.0f).toNearestInt(),
               juce::Justification::centred);

    const auto needleAngle = start + juce::jlimit(0.0f, 1.0f, level) * (end - start);
    const auto needleEnd = polar(centre, radius + 7.0f, needleAngle);
    g.setColour(juce::Colour(0xff8d6e63));
    g.drawLine({ needlePivot, needleEnd }, 2.6f);
    g.setColour(juce::Colour(0xff3e2723));
    g.fillEllipse(juce::Rectangle<float>(6.5f, 6.5f).withCentre(needlePivot));
}

void BqtAudioProcessorEditor::VuMeter::timerCallback()
{
    const auto raw = audioProcessor.getMeterLevel(side);
    const auto db = juce::Decibels::gainToDecibels(raw, -60.0f);
    const auto vu = db + 18.0f;
    const auto clampedVu = juce::jlimit(-20.0f, 3.0f, vu);
    level = clampedVu <= 0.0f ? ((clampedVu + 20.0f) / 20.0f) * 0.82f
                              : 0.82f + (clampedVu / 3.0f) * 0.18f;
    repaint();
}

void BqtAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xfff5f2ef));

    auto bounds = getLocalBounds().toFloat().reduced(18.0f);
    auto top = bounds.removeFromTop(62.0f);
    bounds.removeFromTop(10.0f);
    auto rack = getRackFaceBounds(bounds);
    auto eqPanel = rack.removeFromLeft(rack.getWidth() * 0.5f);
    auto satPanel = rack;

    g.setColour(juce::Colour(0xff1b1b1b));
    g.fillRoundedRectangle(top, 5.0f);

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
        drawPanelText(g, "ebr", brand.withY(brand.getY()).withHeight(28.0f), juce::Justification::topLeft, 24.0f);
        drawPanelText(g, "audio", brand.withY(brand.getY() + 21.0f).withHeight(28.0f), juce::Justification::topLeft, 24.0f);
        drawPanelText(g, "tech", brand.withY(brand.getY() + 42.0f).withHeight(28.0f), juce::Justification::topLeft, 24.0f);
        drawPanelText(g, title, plate.withTrimmedRight(8.0f).withTrimmedTop(6.0f).withHeight(30.0f),
                      juce::Justification::topRight, 24.0f);
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
                                .withCentre({ b.getCentreX(), b.getBottom() + (text == "drive" ? 13.0f : 8.0f) }),
                      juce::Justification::centred, 31.0f);
    };

    for (const auto& controls : sideControls)
    {
        drawSatKnobText(controls.drive, "drive");
        drawSatKnobText(controls.mix, "mix");
        drawSatKnobText(controls.outputTrim, "output");
    }
}

void BqtAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(18);
    auto top = bounds.removeFromTop(62).reduced(18, 14);
    constexpr int topControlHeight = 36;

    constexpr int topControlWidth = 80;
    constexpr int topGap = 8;
    auto topSlot = [](juce::Rectangle<int> area) { return area.withSizeKeepingCentre(area.getWidth(), topControlHeight); };

    inputTrimLabel.setBounds(topSlot(top.removeFromLeft(62)));
    inputTrim.setBounds(topSlot(top.removeFromLeft(62)));
    top.removeFromLeft(topGap);
    eqMode.setBounds(topSlot(top.removeFromLeft(topControlWidth)));
    top.removeFromLeft(topGap);
    eqLink.setBounds(topSlot(top.removeFromLeft(topControlWidth)));
    top.removeFromLeft(topGap);
    satMode.setBounds(topSlot(top.removeFromLeft(topControlWidth)));
    top.removeFromLeft(topGap);
    satLink.setBounds(topSlot(top.removeFromLeft(topControlWidth)));
    top.removeFromLeft(topGap);
    osRealtime.setBounds(topSlot(top.removeFromLeft(topControlWidth)));
    top.removeFromLeft(topGap);
    osRender.setBounds(topSlot(top.removeFromLeft(topControlWidth)));
    top.removeFromLeft(topGap);
    autoGain.setBounds(topSlot(top.removeFromLeft(topControlWidth)));
    top.removeFromLeft(topGap);
    bypass.setBounds(topSlot(top.removeFromLeft(topControlWidth)));
    eqBypass.setBounds(-2000, -2000, 1, 1);
    satBypass.setBounds(-2000, -2000, 1, 1);

    bounds.removeFromTop(10);
    auto rackFloat = getRackFaceBounds(bounds.toFloat());
    auto rack = rackFloat.toNearestInt();
    auto eqPanel = rack.removeFromLeft(rack.getWidth() / 2).reduced(24, 26);
    auto satPanel = rack.reduced(24, 26);
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

    vintage.setBounds(leftDriveX - 72, satPanel.getY() + 196, 144, 44);
    main.satTypeLabel.setBounds(-2000, -2000, 1, 1);
    main.satType.setBounds(-2000, -2000, 1, 1);
    main.satTypeButton.setBounds(rightDriveX - 72, satPanel.getY() + 190, 144, 56);
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
}
