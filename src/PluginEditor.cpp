#include "PluginEditor.h"

namespace
{
juce::String sidePrefix(int sideIndex)
{
    return sideIndex == 0 ? "a" : "b";
}
} // namespace

BqtAudioProcessorEditor::BqtAudioProcessorEditor(BqtAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), meterA(p, 0), meterB(p, 1)
{
    setSize(1160, 560);

    configureCombo(eqMode);
    configureCombo(satMode);
    configureCombo(osRealtime);
    configureCombo(osRender);
    configureSlider(inputTrim);
    inputTrim.setDoubleClickReturnValue(true, 0.0);
    configureCombo(boom);
    addAndMakeVisible(autoGain);
    addAndMakeVisible(eqBypass);
    addAndMakeVisible(satBypass);
    addAndMakeVisible(eqLink);
    addAndMakeVisible(satLink);
    addAndMakeVisible(vintage);
    addAndMakeVisible(bypass);
    addAndMakeVisible(meterA);
    addAndMakeVisible(meterB);

    autoGain.setButtonText("Auto Gain");
    eqBypass.setButtonText("EQ Byp");
    satBypass.setButtonText("Sat Byp");
    eqLink.setButtonText("EQ Link");
    satLink.setButtonText("Sat Link");
    vintage.setButtonText("Vintage");
    bypass.setButtonText("Bypass");
    eqMode.addItemList(juce::StringArray { "EQ L/R", "EQ M/S" }, 1);
    satMode.addItemList(juce::StringArray { "Sat L/R", "Sat M/S" }, 1);
    osRealtime.addItemList(juce::StringArray { "RT Off", "RT 2x", "RT 4x", "RT 8x" }, 1);
    osRender.addItemList(juce::StringArray { "RN Off", "RN 2x", "RN 4x", "RN 8x" }, 1);
    boom.addItemList(juce::StringArray { "Boom Off", "Boom A", "Boom B" }, 1);

    eqModeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "eqMode", eqMode);
    satModeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "satMode", satMode);
    osRealtimeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "osRealtime", osRealtime);
    osRenderAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "osRender", osRender);
    inputTrimAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), "inputTrim", inputTrim);
    boomAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "boom", boom);
    autoGainAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "autoGain", autoGain);
    eqBypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "eqBypass", eqBypass);
    satBypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "satBypass", satBypass);
    eqLinkAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "eqLink", eqLink);
    satLinkAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "satLink", satLink);
    vintageAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "vintage", vintage);
    bypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "bypass", bypass);

    for (int side = 0; side < 2; ++side)
        configureSide(sideControls[static_cast<size_t>(side)], side);
}

void BqtAudioProcessorEditor::configureSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 20);
    slider.setVelocityBasedMode(true);
    slider.setVelocityModeParameters(0.75, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    slider.setTooltip("Drag to adjust. Hold Shift for fine movement. Double-click the value to type.");
    addAndMakeVisible(slider);
}

void BqtAudioProcessorEditor::configureCombo(juce::ComboBox& combo)
{
    combo.setTooltip("Click to choose a stepped setting.");
    addAndMakeVisible(combo);
}

void BqtAudioProcessorEditor::configureSide(SideControls& controls, int sideIndex)
{
    configureSlider(controls.lowGain);
    configureCombo(controls.lowFreq);
    configureSlider(controls.highGain);
    configureCombo(controls.highFreq);
    configureSlider(controls.drive);
    configureCombo(controls.satType);
    configureSlider(controls.mix);
    configureSlider(controls.outputTrim);
    controls.lowGain.setDoubleClickReturnValue(true, 0.0);
    controls.highGain.setDoubleClickReturnValue(true, 0.0);
    controls.drive.setDoubleClickReturnValue(true, 0.0);
    controls.mix.setDoubleClickReturnValue(true, 100.0);
    controls.outputTrim.setDoubleClickReturnValue(true, 0.0);
    controls.lowFreq.addItemList(juce::StringArray { "74", "84", "98", "116", "131", "166", "230", "361" }, 1);
    controls.highFreq.addItemList(juce::StringArray { "1.6k", "1.8k", "2.1k", "2.5k", "3.4k", "4.8k", "7.1k", "18k" }, 1);
    controls.satType.addItemList(juce::StringArray { "Density", "Transformer" }, 1);

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

BqtAudioProcessorEditor::VuMeter::VuMeter(BqtAudioProcessor& p, int sideIndex)
    : audioProcessor(p), side(sideIndex)
{
    startTimerHz(24);
}

void BqtAudioProcessorEditor::VuMeter::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff111111));
    g.fillRoundedRectangle(bounds, 4.0f);

    const auto clamped = juce::jlimit(0.0f, 1.0f, level);
    auto fill = bounds.reduced(4.0f);
    fill.removeFromTop(fill.getHeight() * (1.0f - clamped));

    g.setColour(clamped > 0.8f ? juce::Colour(0xffd15c45) : juce::Colour(0xffd6b15f));
    g.fillRoundedRectangle(fill, 3.0f);

    g.setColour(juce::Colour(0xff363636));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
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
    g.fillAll(juce::Colour(0xff151515));

    auto bounds = getLocalBounds().toFloat().reduced(18.0f);
    auto top = bounds.removeFromTop(62.0f);
    auto panel = bounds.reduced(0.0f, 10.0f);
    auto eqPanel = panel.removeFromLeft(panel.getWidth() * 0.5f).reduced(0.0f, 0.0f);
    auto satPanel = panel.reduced(0.0f, 0.0f);

    g.setColour(juce::Colour(0xff242424));
    g.fillRoundedRectangle(top, 6.0f);

    g.setColour(juce::Colour(0xffb9b4a8));
    g.fillRoundedRectangle(eqPanel.reduced(0.0f, 0.0f), 6.0f);

    g.setColour(juce::Colour(0xff8f9692));
    g.fillRoundedRectangle(satPanel.reduced(10.0f, 0.0f), 6.0f);

    g.setColour(juce::Colour(0xff202020));
    g.drawRoundedRectangle(eqPanel, 6.0f, 2.0f);
    g.drawRoundedRectangle(satPanel.reduced(10.0f, 0.0f), 6.0f, 2.0f);

    g.setColour(juce::Colours::white);
    g.setFont(22.0f);
    g.drawText("BQT EQ", eqPanel.removeFromTop(38.0f).toNearestInt(), juce::Justification::centred);
    g.drawText("BQT SAT", satPanel.removeFromTop(38.0f).toNearestInt(), juce::Justification::centred);
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
    boom.setBounds(satShared.removeFromLeft(132).reduced(8, 8));
    vintage.setBounds(satShared.removeFromLeft(118).reduced(8, 8));
    satPanel.removeFromTop(4);

    for (int side = 0; side < 2; ++side)
    {
        auto eqArea = side == 0 ? eqPanel.removeFromTop(eqPanel.getHeight() / 2) : eqPanel;
        eqArea = eqArea.reduced(0, 4);

        auto row1 = eqArea.removeFromTop(92);
        sideControls[static_cast<size_t>(side)].highGain.setBounds(row1.removeFromLeft(132));
        row1.removeFromLeft(12);
        sideControls[static_cast<size_t>(side)].highFreq.setBounds(row1.removeFromLeft(120).reduced(0, 36));

        auto row2 = eqArea.removeFromTop(92);
        sideControls[static_cast<size_t>(side)].lowGain.setBounds(row2.removeFromLeft(132));
        row2.removeFromLeft(12);
        sideControls[static_cast<size_t>(side)].lowFreq.setBounds(row2.removeFromLeft(120).reduced(0, 36));

        auto satArea = side == 0 ? satPanel.removeFromTop(satPanel.getHeight() / 2) : satPanel;
        satArea = satArea.reduced(0, 4);

        auto satRow = satArea.removeFromTop(132);
        sideControls[static_cast<size_t>(side)].drive.setBounds(satRow.removeFromLeft(104));
        satRow.removeFromLeft(8);
        sideControls[static_cast<size_t>(side)].mix.setBounds(satRow.removeFromLeft(104));
        satRow.removeFromLeft(8);
        sideControls[static_cast<size_t>(side)].outputTrim.setBounds(satRow.removeFromLeft(104));
        satRow.removeFromLeft(8);
        sideControls[static_cast<size_t>(side)].satType.setBounds(satRow.removeFromLeft(116).reduced(0, 44));

        auto meterBounds = satArea.removeFromTop(58);
        if (side == 0)
            meterA.setBounds(meterBounds.removeFromLeft(190).reduced(16, 4));
        else
            meterB.setBounds(meterBounds.removeFromLeft(190).reduced(16, 4));
    }
}
