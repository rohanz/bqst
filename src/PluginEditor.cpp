#include "PluginEditor.h"

namespace
{
constexpr auto panelPink = 0xfff28ab8;
constexpr auto panelPinkDark = 0xffdf6f9f;
constexpr auto ink = 0xff111111;
constexpr auto cream = 0xfffff4ed;
constexpr auto lampOn = 0xfffaf7f2;
constexpr auto lampOff = 0xff6d6d6d;

juce::String sidePrefix(int sideIndex)
{
    return sideIndex == 0 ? "a" : "b";
}

juce::FontOptions faceFont(float height, bool bold = true)
{
    return juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain);
}
} // namespace

BqtAudioProcessorEditor::BqtAudioProcessorEditor(BqtAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), meterA(p, 0), meterB(p, 1)
{
    setLookAndFeel(&hardwareLookAndFeel);
    setSize(1160, 560);

    configureCombo(eqMode);
    configureCombo(satMode);
    configureCombo(osRealtime);
    configureCombo(osRender);
    configureLabel(inputTrimLabel, "Input", juce::Justification::centredLeft);
    configureSlider(inputTrim);
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

    autoGain.setButtonText("auto");
    eqBypass.setButtonText("eq off");
    satBypass.setButtonText("sat off");
    eqLink.setButtonText("eq link");
    satLink.setButtonText("sat link");
    vintage.setButtonText("vint.");
    bypass.setButtonText("bypass");
    eqMode.addItemList(juce::StringArray { "eq l/r", "eq m/s" }, 1);
    satMode.addItemList(juce::StringArray { "sat l/r", "sat m/s" }, 1);
    osRealtime.addItemList(juce::StringArray { "rt off", "rt 2x", "rt 4x", "rt 8x" }, 1);
    osRender.addItemList(juce::StringArray { "rn off", "rn 2x", "rn 4x", "rn 8x" }, 1);

    eqModeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "eqMode", eqMode);
    satModeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "satMode", satMode);
    osRealtimeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "osRealtime", osRealtime);
    osRenderAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "osRender", osRender);
    inputTrimAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), "inputTrim", inputTrim);
    autoGainAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "autoGain", autoGain);
    eqBypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "eqBypass", eqBypass);
    satBypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "satBypass", satBypass);
    eqLinkAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "eqLink", eqLink);
    satLinkAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "satLink", satLink);
    vintageAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "vintage", vintage);
    bypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "bypass", bypass);

    for (int side = 0; side < 2; ++side)
        configureSide(sideControls[static_cast<size_t>(side)], side);

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
    addAndMakeVisible(combo);
}

void BqtAudioProcessorEditor::configureLabel(juce::Label& label, const juce::String& text, juce::Justification justification)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(justification);
    label.setColour(juce::Label::textColourId, juce::Colour(ink));
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
    configureLabel(controls.highFreqLabel, "freq");
    configureCombo(controls.highFreq);
    configureLabel(controls.lowGainLabel, "lf");
    configureSlider(controls.lowGain);
    configureLabel(controls.lowFreqLabel, "freq");
    configureCombo(controls.lowFreq);
    configureLabel(controls.driveLabel, "drive");
    configureSlider(controls.drive);
    configureLabel(controls.satTypeLabel, "sat. type");
    configureCombo(controls.satType);
    configureLabel(controls.mixLabel, "mix");
    configureSlider(controls.mix);
    configureLabel(controls.outputTrimLabel, "output");
    configureSlider(controls.outputTrim);
    controls.lowGain.setDoubleClickReturnValue(true, 0.0);
    controls.highGain.setDoubleClickReturnValue(true, 0.0);
    controls.drive.setDoubleClickReturnValue(true, 0.0);
    controls.mix.setDoubleClickReturnValue(true, 100.0);
    controls.outputTrim.setDoubleClickReturnValue(true, 0.0);
    controls.lowFreq.addItemList(juce::StringArray { "74", "84", "98", "116", "131", "166", "230", "361" }, 1);
    controls.highFreq.addItemList(juce::StringArray { "1.6k", "1.8k", "2.1k", "2.5k", "3.4k", "4.8k", "7.1k", "18k" }, 1);
    controls.satType.addItemList(juce::StringArray { "cream", "grit" }, 1);

    const auto prefix = sidePrefix(sideIndex);
    controls.lowGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "LowGain", controls.lowGain);
    controls.lowFreqAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), prefix + "LowFreq", controls.lowFreq);
    controls.highGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "HighGain", controls.highGain);
    controls.highFreqAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), prefix + "HighFreq", controls.highFreq);
    controls.driveAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "Drive", controls.drive);
    controls.satTypeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), prefix + "SatType", controls.satType);
    controls.mixAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "Mix", controls.mix);
    controls.outputTrimAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "OutputTrim", controls.outputTrim);
}

void BqtAudioProcessorEditor::timerCallback()
{
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
        controls.highFreq.setTooltip("hf freq " + controls.highFreq.getText());
        controls.lowFreq.setTooltip("lf freq " + controls.lowFreq.getText());
        controls.satType.setTooltip("sat type " + controls.satType.getText());
    }
}

void BqtAudioProcessorEditor::updateLinkedControlStates()
{
    const auto eqLinked = audioProcessor.state().getRawParameterValue("eqLink")->load() > 0.5f;
    const auto satLinked = audioProcessor.state().getRawParameterValue("satLink")->load() > 0.5f;
    auto& right = sideControls[1];

    auto setEqSlave = [eqLinked](juce::Component& component)
    {
        component.setEnabled(! eqLinked);
        component.setAlpha(eqLinked ? 0.38f : 1.0f);
    };

    auto setSatSlave = [satLinked](juce::Component& component)
    {
        component.setEnabled(! satLinked);
        component.setAlpha(satLinked ? 0.38f : 1.0f);
    };

    setEqSlave(right.lowGain);
    setEqSlave(right.lowFreq);
    setEqSlave(right.highGain);
    setEqSlave(right.highFreq);
    setEqSlave(right.lowGainLabel);
    setEqSlave(right.lowFreqLabel);
    setEqSlave(right.highGainLabel);
    setEqSlave(right.highFreqLabel);

    setSatSlave(right.drive);
    setSatSlave(right.satType);
    setSatSlave(right.mix);
    setSatSlave(right.outputTrim);
    setSatSlave(right.driveLabel);
    setSatSlave(right.satTypeLabel);
    setSatSlave(right.mixLabel);
    setSatSlave(right.outputTrimLabel);
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
    const auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f);
    g.setColour(isButtonDown ? juce::Colour(0xffefe1d8) : juce::Colour(cream));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(juce::Colour(ink));
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
    label.setBounds(8, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont(juce::Font(faceFont(13.0f)));
    label.setJustificationType(juce::Justification::centredLeft);
}

void BqtAudioProcessorEditor::HardwareLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                                                    bool shouldDrawButtonAsHighlighted,
                                                                    bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    const auto lamp = bounds.removeFromLeft(18.0f).reduced(3.0f);
    const auto text = bounds.reduced(2.0f, 0.0f);
    const auto on = button.getToggleState();

    g.setColour(juce::Colour(ink));
    g.setFont(juce::Font(faceFont(13.5f)));
    g.drawText(button.getButtonText().toLowerCase(), text.toNearestInt(), juce::Justification::centredLeft);

    g.setColour(juce::Colours::black.withAlpha(0.30f));
    g.fillEllipse(lamp.translated(1.5f, 2.0f));
    auto lampColour = on ? juce::Colour(lampOn) : juce::Colour(lampOff);
    if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
        lampColour = lampColour.brighter(0.08f);
    g.setColour(lampColour);
    g.fillEllipse(lamp);
    g.setColour(juce::Colour(ink));
    g.drawEllipse(lamp, 1.0f);
}

BqtAudioProcessorEditor::VuMeter::VuMeter(BqtAudioProcessor& p, int sideIndex)
    : audioProcessor(p), side(sideIndex)
{
    startTimerHz(24);
}

void BqtAudioProcessorEditor::VuMeter::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    auto meter = bounds.reduced(1.0f);

    g.setColour(juce::Colours::black.withAlpha(0.28f));
    g.fillRoundedRectangle(meter.translated(2.0f, 3.0f), 8.0f);

    g.setColour(juce::Colour(cream));
    g.fillRoundedRectangle(meter, 8.0f);
    g.setColour(juce::Colour(ink));
    g.drawRoundedRectangle(meter, 8.0f, 1.4f);

    g.setFont(juce::Font(faceFont(12.5f)));
    g.drawText(side == 0 ? "l/m" : "r/s", meter.removeFromBottom(18.0f).toNearestInt(), juce::Justification::centred);

    const auto arc = bounds.reduced(12.0f, 8.0f);
    const auto centre = juce::Point<float>(arc.getCentreX(), arc.getBottom() + 18.0f);
    const auto radius = arc.getWidth() * 0.48f;
    const auto start = juce::MathConstants<float>::pi * 1.18f;
    const auto end = juce::MathConstants<float>::pi * 1.82f;

    for (int i = 0; i <= 6; ++i)
    {
        const auto proportion = static_cast<float>(i) / 6.0f;
        const auto angle = start + proportion * (end - start);
        const auto p1 = centre.getPointOnCircumference(radius * 0.78f, angle);
        const auto p2 = centre.getPointOnCircumference(radius * 0.90f, angle);
        g.drawLine({ p1, p2 }, 1.0f);
    }

    const auto needleAngle = start + juce::jlimit(0.0f, 1.0f, level) * (end - start);
    const auto needleEnd = centre.getPointOnCircumference(radius * 0.82f, needleAngle);
    g.setColour(juce::Colour(0xff555555));
    g.drawLine({ centre, needleEnd }, 2.0f);
    g.setColour(juce::Colour(ink));
    g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(centre));
}

void BqtAudioProcessorEditor::VuMeter::timerCallback()
{
    const auto raw = audioProcessor.getMeterLevel(side);
    const auto db = juce::Decibels::gainToDecibels(raw, -60.0f);
    const auto vu = db + 18.0f;
    level = juce::jmap(juce::jlimit(-20.0f, 3.0f, vu), -20.0f, 3.0f, 0.0f, 1.0f);
    repaint();
}

void BqtAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xfff5f2ef));

    auto bounds = getLocalBounds().toFloat().reduced(18.0f);
    auto top = bounds.removeFromTop(62.0f);
    auto panel = bounds.reduced(0.0f, 10.0f);
    auto eqPanel = panel.removeFromLeft(panel.getWidth() * 0.5f).reduced(0.0f, 0.0f);
    auto satPanel = panel.reduced(0.0f, 0.0f);

    g.setColour(juce::Colour(0xff1b1b1b));
    g.fillRoundedRectangle(top, 5.0f);

    auto drawPlate = [&g](juce::Rectangle<float> plate, const juce::String& title)
    {
        juce::ColourGradient plateGradient(juce::Colour(panelPink).brighter(0.05f), plate.getX(), plate.getY(),
                                           juce::Colour(panelPinkDark), plate.getRight(), plate.getBottom(), false);
        g.setGradientFill(plateGradient);
        g.fillRect(plate);

        g.setColour(juce::Colour(ink));
        g.drawRect(plate, 3.0f);

        const auto screwColour = juce::Colour(0xff545454);
        const auto screwSize = 14.0f;
        for (auto screwCentre : { plate.getTopLeft() + juce::Point<float>(142.0f, 20.0f),
                                  plate.getTopRight() + juce::Point<float>(-142.0f, 20.0f),
                                  plate.getBottomLeft() + juce::Point<float>(142.0f, -20.0f),
                                  plate.getBottomRight() + juce::Point<float>(-142.0f, -20.0f) })
        {
            const auto screw = juce::Rectangle<float>(screwSize, screwSize).withCentre(screwCentre);
            g.setColour(juce::Colours::black.withAlpha(0.22f));
            g.fillEllipse(screw.translated(1.0f, 1.5f));
            g.setColour(screwColour);
            g.fillEllipse(screw);
        }

        g.setColour(juce::Colour(ink));
        g.setFont(juce::Font(faceFont(23.0f)));
        g.drawText("ebr\naudio\ntech", plate.removeFromLeft(92.0f).removeFromTop(104.0f).toNearestInt(),
                   juce::Justification::topLeft);
        g.drawText(title, plate.removeFromTop(30.0f).reduced(10.0f, 0.0f).toNearestInt(), juce::Justification::topRight);
    };

    drawPlate(eqPanel, "baxq");
    drawPlate(satPanel.reduced(10.0f, 0.0f), "ttsat");

    auto drawGainRange = [&g](const juce::Slider& slider)
    {
        const auto sliderBounds = slider.getBounds().toFloat();
        if (sliderBounds.isEmpty())
            return;

        const auto centre = sliderBounds.getCentre();
        const auto radius = juce::jmin(sliderBounds.getWidth(), sliderBounds.getHeight()) * 0.34f;
        g.setColour(juce::Colour(ink));
        g.setFont(juce::Font(faceFont(13.0f)));
        g.drawText("-6", juce::Rectangle<float>(38.0f, 18.0f).withCentre(centre + juce::Point<float>(-radius - 8.0f, radius * 0.70f)).toNearestInt(),
                   juce::Justification::centred);
        g.drawText("+6", juce::Rectangle<float>(38.0f, 18.0f).withCentre(centre + juce::Point<float>(radius + 8.0f, radius * 0.70f)).toNearestInt(),
                   juce::Justification::centred);
    };

    for (const auto& controls : sideControls)
    {
        drawGainRange(controls.highGain);
        drawGainRange(controls.lowGain);
    }
}

void BqtAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(18);
    auto top = bounds.removeFromTop(62).reduced(12);

    eqMode.setBounds(top.removeFromLeft(100));
    top.removeFromLeft(8);
    satMode.setBounds(top.removeFromLeft(106));
    top.removeFromLeft(10);
    osRealtime.setBounds(top.removeFromLeft(118));
    top.removeFromLeft(10);
    osRender.setBounds(top.removeFromLeft(118));
    top.removeFromLeft(8);
    inputTrimLabel.setBounds(top.removeFromLeft(42));
    inputTrim.setBounds(top.removeFromLeft(96));
    top.removeFromLeft(8);
    autoGain.setBounds(top.removeFromLeft(90));
    eqBypass.setBounds(top.removeFromLeft(76));
    satBypass.setBounds(top.removeFromLeft(78));
    eqLink.setBounds(top.removeFromLeft(78));
    satLink.setBounds(top.removeFromLeft(82));
    bypass.setBounds(top.removeFromLeft(76));

    auto panel = bounds.reduced(0, 18);
    panel.removeFromTop(40);
    auto eqPanel = panel.removeFromLeft(panel.getWidth() / 2).reduced(22, 8);
    auto satPanel = panel.reduced(32, 8);

    auto satShared = satPanel.removeFromTop(48);
    vintage.setBounds(satShared.removeFromLeft(118).reduced(8, 8));
    satPanel.removeFromTop(4);

    for (int side = 0; side < 2; ++side)
    {
        auto eqArea = side == 0 ? eqPanel.removeFromTop(eqPanel.getHeight() / 2) : eqPanel;
        eqArea = eqArea.reduced(0, 4);
        sideControls[static_cast<size_t>(side)].eqSectionLabel.setBounds(eqArea.removeFromTop(22));

        auto row1 = eqArea.removeFromTop(eqArea.getHeight() / 2);
        auto highGainCell = row1.removeFromLeft(132);
        sideControls[static_cast<size_t>(side)].highGainLabel.setBounds(highGainCell.removeFromTop(18));
        sideControls[static_cast<size_t>(side)].highGain.setBounds(highGainCell);
        row1.removeFromLeft(12);
        auto highFreqCell = row1.removeFromLeft(120);
        sideControls[static_cast<size_t>(side)].highFreqLabel.setBounds(highFreqCell.removeFromTop(18));
        sideControls[static_cast<size_t>(side)].highFreq.setBounds(highFreqCell.removeFromTop(32).reduced(0, 3));

        auto row2 = eqArea;
        auto lowGainCell = row2.removeFromLeft(132);
        sideControls[static_cast<size_t>(side)].lowGainLabel.setBounds(lowGainCell.removeFromTop(18));
        sideControls[static_cast<size_t>(side)].lowGain.setBounds(lowGainCell);
        row2.removeFromLeft(12);
        auto lowFreqCell = row2.removeFromLeft(120);
        sideControls[static_cast<size_t>(side)].lowFreqLabel.setBounds(lowFreqCell.removeFromTop(18));
        sideControls[static_cast<size_t>(side)].lowFreq.setBounds(lowFreqCell.removeFromTop(32).reduced(0, 3));

        auto satArea = side == 0 ? satPanel.removeFromTop(satPanel.getHeight() / 2) : satPanel;
        satArea = satArea.reduced(0, 4);
        sideControls[static_cast<size_t>(side)].satSectionLabel.setBounds(satArea.removeFromTop(22));

        auto satRow = satArea.removeFromTop(110);
        auto driveCell = satRow.removeFromLeft(104);
        sideControls[static_cast<size_t>(side)].driveLabel.setBounds(driveCell.removeFromTop(18));
        sideControls[static_cast<size_t>(side)].drive.setBounds(driveCell);
        satRow.removeFromLeft(8);
        auto mixCell = satRow.removeFromLeft(104);
        sideControls[static_cast<size_t>(side)].mixLabel.setBounds(mixCell.removeFromTop(18));
        sideControls[static_cast<size_t>(side)].mix.setBounds(mixCell);
        satRow.removeFromLeft(8);
        auto outputCell = satRow.removeFromLeft(104);
        sideControls[static_cast<size_t>(side)].outputTrimLabel.setBounds(outputCell.removeFromTop(18));
        sideControls[static_cast<size_t>(side)].outputTrim.setBounds(outputCell);
        satRow.removeFromLeft(8);
        auto typeCell = satRow.removeFromLeft(116);
        sideControls[static_cast<size_t>(side)].satTypeLabel.setBounds(typeCell.removeFromTop(18));
        sideControls[static_cast<size_t>(side)].satType.setBounds(typeCell.removeFromTop(32).reduced(0, 3));

        auto meterBounds = satArea.removeFromTop(58);
        if (side == 0)
            meterA.setBounds(meterBounds.removeFromLeft(190).reduced(16, 4));
        else
            meterB.setBounds(meterBounds.removeFromLeft(190).reduced(16, 4));
    }
}
