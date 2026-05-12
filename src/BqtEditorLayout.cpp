#include "PluginEditor.h"

#include "BqtEditorStyle.h"

#include <cmath>

namespace
{
using namespace bqst::ui;
} // namespace

void BqtAudioProcessorEditor::paint(juce::Graphics& g)
{
    const auto uiScale = static_cast<float>(getWidth()) / static_cast<float>(baseEditorWidth);
    juce::Graphics::ScopedSaveState savedState(g);
    g.addTransform(juce::AffineTransform::scale(uiScale));

    g.fillAll(juce::Colour(0xfff5f2ef));

    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(baseEditorWidth),
                                         static_cast<float>(baseEditorHeight)).reduced(static_cast<float>(outerEditorMargin));
    auto headerSlot = bounds.removeFromTop(104.0f);
    auto rackArea = bounds;
    rackArea.removeFromTop(10.0f);
    auto rack = getRackFaceBounds(rackArea);
    auto header = rack.withY(headerSlot.getY()).withHeight(headerSlot.getHeight());
    auto presetBar = header.removeFromTop(48.0f);
    auto top = header.removeFromTop(56.0f);

    g.setColour(juce::Colour(0xff1b1b1b));
    g.fillRoundedRectangle(presetBar.getUnion(top), 5.0f);
    g.setColour(juce::Colour(panelText));
    g.setFont(juce::Font(faceFont(27.0f, true)));
    g.drawText("bqst " JucePlugin_VersionString,
               presetBar.withTrimmedLeft(14.0f).withTrimmedTop(5.0f).withHeight(34.0f).toNearestInt(),
               juce::Justification::centredLeft);
}

void BqtAudioProcessorEditor::paintRack(juce::Graphics& g)
{
    auto rack = rackComponent.getLocalBounds().toFloat();
    auto eqPanel = rack.removeFromLeft(rack.getWidth() * 0.5f);
    auto satPanel = rack;

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
        drawPanelText(g, "ebr", brand.withY(brand.getY()).withHeight(30.0f), juce::Justification::topLeft, 27.0f);
        drawPanelText(g, "audio", brand.withY(brand.getY() + 19.0f).withHeight(30.0f), juce::Justification::topLeft, 27.0f);
        drawPanelText(g, "tech", brand.withY(brand.getY() + 38.0f).withHeight(30.0f), juce::Justification::topLeft, 27.0f);
        drawPanelText(g, title, plate.withTrimmedRight(8.0f).withTrimmedTop(6.0f).withHeight(32.0f),
                      juce::Justification::topRight, 27.0f);
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
                                .withCentre({ b.getCentreX(), b.getBottom() + (text == "drive" ? 13.0f : -2.0f) }),
                      juce::Justification::centred, text == "drive" ? 31.0f : 30.0f);
    };

    for (const auto& controls : sideControls)
    {
        drawSatKnobText(controls.drive, "drive");
        drawSatKnobText(controls.mix, "mix");
        drawSatKnobText(controls.outputTrim, "output");
    }
}

void BqtAudioProcessorEditor::RackComponent::paintOverChildren(juce::Graphics& g)
{
    if (! bypassed)
        return;

    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.28f));
    g.fillRect(bounds);
    g.setColour(juce::Colour(0xff8a8a8a).withAlpha(0.16f));
    g.fillRect(bounds);
}

void BqtAudioProcessorEditor::requestRackBypassVisualState(bool shouldBeBypassed)
{
    rackComponent.setBypassed(shouldBeBypassed);
}

void BqtAudioProcessorEditor::RackComponent::setBypassed(bool shouldBeBypassed)
{
    if (bypassed == shouldBeBypassed)
        return;

    bypassed = shouldBeBypassed;
    repaint();
}

void BqtAudioProcessorEditor::resized()
{
    const auto uiScale = static_cast<float>(getWidth()) / static_cast<float>(baseEditorWidth);

    auto applyUiScale = [uiScale](juce::Component& component)
    {
        component.setTransform(juce::AffineTransform::scale(uiScale));
    };

    for (auto* component : { static_cast<juce::Component*>(&rackComponent),
                             static_cast<juce::Component*>(&presetPrevious), static_cast<juce::Component*>(&presetMenuButton),
                             static_cast<juce::Component*>(&presetNext), static_cast<juce::Component*>(&presetSave),
                             static_cast<juce::Component*>(&inputTrimLabel), static_cast<juce::Component*>(&inputTrim),
                             static_cast<juce::Component*>(&eqMode), static_cast<juce::Component*>(&satMode),
                             static_cast<juce::Component*>(&osRealtime), static_cast<juce::Component*>(&osRender),
                             static_cast<juce::Component*>(&autoGain), static_cast<juce::Component*>(&eqBypass),
                             static_cast<juce::Component*>(&satBypass), static_cast<juce::Component*>(&eqLink),
                             static_cast<juce::Component*>(&satLink),
                             static_cast<juce::Component*>(&bypass), static_cast<juce::Component*>(&sizeSelect),
                             static_cast<juce::Component*>(&readoutBubble) })
        applyUiScale(*component);

    auto bounds = juce::Rectangle<int>(0, 0, baseEditorWidth, baseEditorHeight).reduced(outerEditorMargin);
    auto headerSlot = bounds.removeFromTop(104);
    auto rackArea = bounds;
    rackArea.removeFromTop(10);
    auto header = getRackFaceBounds(rackArea.toFloat()).toNearestInt()
                      .withY(headerSlot.getY())
                      .withHeight(headerSlot.getHeight());
    auto presetBar = header.removeFromTop(48).reduced(8, 7);
    auto top = header.removeFromTop(56).reduced(8, 9);
    constexpr int topControlHeight = 36;

    constexpr int topGap = 4;
    auto topSlot = [](juce::Rectangle<int> area) { return area.withSizeKeepingCentre(area.getWidth(), topControlHeight); };

    sizeSelect.setBounds(topSlot(presetBar.removeFromRight(82)));
    constexpr int presetPreviousWidth = 38;
    constexpr int presetMenuWidth = 320;
    constexpr int presetNextWidth = 38;
    constexpr int presetSaveWidth = 70;
    auto presetButtonBounds = juce::Rectangle<int>(presetMenuWidth, presetBar.getHeight())
                                  .withCentre({ baseEditorWidth / 2, presetBar.getCentreY() });
    presetMenuButton.setBounds(topSlot(presetButtonBounds));
    presetPrevious.setBounds(topSlot({ presetButtonBounds.getX() - topGap - presetPreviousWidth,
                                       presetBar.getY(),
                                       presetPreviousWidth,
                                       presetBar.getHeight() }));
    presetNext.setBounds(topSlot({ presetButtonBounds.getRight() + topGap,
                                   presetBar.getY(),
                                   presetNextWidth,
                                   presetBar.getHeight() }));
    presetSave.setBounds(topSlot({ presetNext.getRight() + topGap,
                                   presetBar.getY(),
                                   presetSaveWidth,
                                   presetBar.getHeight() }));

    constexpr int inputLabelWidth = 47;
    constexpr int inputControlWidth = 48;
    constexpr int numTopButtons = 8;
    int topButtonWidths[] = { 80, 70, 80, 70, 112, 112, 78, 70 };
    int preferredTopWidth = inputLabelWidth + inputControlWidth + (numTopButtons * topGap);
    for (const auto width : topButtonWidths)
        preferredTopWidth += width;

    auto extraTopWidth = juce::jmax(0, top.getWidth() - preferredTopWidth);
    for (auto i = 0; i < numTopButtons; ++i)
        topButtonWidths[i] += extraTopWidth / numTopButtons + (i < extraTopWidth % numTopButtons ? 1 : 0);

    inputTrimLabel.setBounds(topSlot(top.removeFromLeft(inputLabelWidth)));
    inputTrim.setBounds(topSlot(top.removeFromLeft(inputControlWidth)));
    top.removeFromLeft(topGap);
    eqMode.setBounds(topSlot(top.removeFromLeft(topButtonWidths[0])));
    top.removeFromLeft(topGap);
    eqLink.setBounds(topSlot(top.removeFromLeft(topButtonWidths[1])));
    top.removeFromLeft(topGap);
    satMode.setBounds(topSlot(top.removeFromLeft(topButtonWidths[2])));
    top.removeFromLeft(topGap);
    satLink.setBounds(topSlot(top.removeFromLeft(topButtonWidths[3])));
    top.removeFromLeft(topGap);
    osRealtime.setBounds(topSlot(top.removeFromLeft(topButtonWidths[4])));
    top.removeFromLeft(topGap);
    osRender.setBounds(topSlot(top.removeFromLeft(topButtonWidths[5])));
    top.removeFromLeft(topGap);
    autoGain.setBounds(topSlot(top.removeFromLeft(topButtonWidths[6])));
    top.removeFromLeft(topGap);
    bypass.setBounds(topSlot(top.removeFromLeft(topButtonWidths[7])));
    eqBypass.setBounds(-2000, -2000, 1, 1);
    satBypass.setBounds(-2000, -2000, 1, 1);

    bounds.removeFromTop(10);
    auto rackFloat = getRackFaceBounds(bounds.toFloat());
    auto rack = rackFloat.toNearestInt();
    rackComponent.setBounds(rack);
    auto rackLocal = juce::Rectangle<int>(0, 0, rack.getWidth(), rack.getHeight());
    auto eqPanel = rackLocal.removeFromLeft(rackLocal.getWidth() / 2).reduced(24, 26);
    auto satPanel = rackLocal.reduced(24, 26);
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

    vintage.setBounds(leftDriveX - 72, satPanel.getY() + 201, 144, 44);
    main.satTypeLabel.setBounds(-2000, -2000, 1, 1);
    main.satType.setBounds(-2000, -2000, 1, 1);
    main.satTypeButton.setBounds(rightDriveX - 72, satPanel.getY() + 195, 144, 56);
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
    readoutBubble.toFront(false);
}
