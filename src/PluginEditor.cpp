#include "PluginEditor.h"

namespace
{
juce::String sidePrefix(int sideIndex)
{
    return sideIndex == 0 ? "a" : "b";
}
} // namespace

BqtAudioProcessorEditor::BqtAudioProcessorEditor(BqtAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(760, 520);

    configureCombo(mode);
    configureCombo(osRealtime);
    configureCombo(osRender);
    addAndMakeVisible(autoGain);
    addAndMakeVisible(bypass);

    autoGain.setButtonText("Auto Gain");
    bypass.setButtonText("Bypass");

    modeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "mode", mode);
    osRealtimeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "osRealtime", osRealtime);
    osRenderAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), "osRender", osRender);
    autoGainAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "autoGain", autoGain);
    bypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.state(), "bypass", bypass);

    for (int side = 0; side < 2; ++side)
        configureSide(sideControls[static_cast<size_t>(side)], side);
}

void BqtAudioProcessorEditor::configureSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 20);
    addAndMakeVisible(slider);
}

void BqtAudioProcessorEditor::configureCombo(juce::ComboBox& combo)
{
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
    configureSlider(controls.outputTrim);

    const auto prefix = sidePrefix(sideIndex);
    controls.lowGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "LowGain", controls.lowGain);
    controls.lowFreqAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), prefix + "LowFreq", controls.lowFreq);
    controls.highGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "HighGain", controls.highGain);
    controls.highFreqAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), prefix + "HighFreq", controls.highFreq);
    controls.driveAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "Drive", controls.drive);
    controls.satTypeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.state(), prefix + "SatType", controls.satType);
    controls.outputTrimAttachment = std::make_unique<SliderAttachment>(audioProcessor.state(), prefix + "OutputTrim", controls.outputTrim);
}

void BqtAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff151515));

    auto bounds = getLocalBounds().toFloat().reduced(18.0f);
    auto top = bounds.removeFromTop(62.0f);
    auto panel = bounds.reduced(0.0f, 10.0f);

    g.setColour(juce::Colour(0xff242424));
    g.fillRoundedRectangle(top, 6.0f);

    g.setColour(juce::Colour(0xffb9b4a8));
    g.fillRoundedRectangle(panel, 6.0f);

    g.setColour(juce::Colour(0xff202020));
    g.drawRoundedRectangle(panel, 6.0f, 2.0f);
    g.drawLine(panel.getCentreX(), panel.getY() + 12.0f, panel.getCentreX(), panel.getBottom() - 12.0f, 2.0f);

    g.setColour(juce::Colours::white);
    g.setFont(22.0f);
    g.drawText("BQT", panel.removeFromTop(34.0f).toNearestInt(), juce::Justification::centred);
}

void BqtAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(18);
    auto top = bounds.removeFromTop(62).reduced(12);

    mode.setBounds(top.removeFromLeft(100));
    top.removeFromLeft(10);
    osRealtime.setBounds(top.removeFromLeft(118));
    top.removeFromLeft(10);
    osRender.setBounds(top.removeFromLeft(118));
    top.removeFromLeft(16);
    autoGain.setBounds(top.removeFromLeft(110));
    bypass.setBounds(top.removeFromLeft(90));

    auto panel = bounds.reduced(0, 18);
    panel.removeFromTop(40);

    for (int side = 0; side < 2; ++side)
    {
        auto area = side == 0 ? panel.removeFromLeft(panel.getWidth() / 2) : panel;
        area = area.reduced(22, 8);

        auto row1 = area.removeFromTop(112);
        sideControls[static_cast<size_t>(side)].highGain.setBounds(row1.removeFromLeft(132));
        row1.removeFromLeft(12);
        sideControls[static_cast<size_t>(side)].highFreq.setBounds(row1.removeFromLeft(120).reduced(0, 36));

        auto row2 = area.removeFromTop(112);
        sideControls[static_cast<size_t>(side)].lowGain.setBounds(row2.removeFromLeft(132));
        row2.removeFromLeft(12);
        sideControls[static_cast<size_t>(side)].lowFreq.setBounds(row2.removeFromLeft(120).reduced(0, 36));

        auto row3 = area.removeFromTop(132);
        sideControls[static_cast<size_t>(side)].drive.setBounds(row3.removeFromLeft(112));
        row3.removeFromLeft(8);
        sideControls[static_cast<size_t>(side)].satType.setBounds(row3.removeFromLeft(112).reduced(0, 44));
        row3.removeFromLeft(8);
        sideControls[static_cast<size_t>(side)].outputTrim.setBounds(row3.removeFromLeft(112));
    }
}
