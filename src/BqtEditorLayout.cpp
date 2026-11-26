#include "PluginEditor.h"

#include "BqtEditorStyle.h"

#include <cmath>

namespace
{
using namespace bqst::ui;

// The 104 px header holds two rounded strips: presets/bypass/about (48 px, 32 px controls), a
// 12 px gap, then the global controls (44 px, 30 px controls).
constexpr int presetBarHeight = 48;
constexpr int headerRowGap = 12;
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
    auto presetBar = header.removeFromTop(static_cast<float>(presetBarHeight));
    header.removeFromTop(static_cast<float>(headerRowGap));
    auto top = header;

    // Two soft tinted strips, one per row, each a step deeper than the plate below.
    g.setColour(juce::Colour(barTint));
    g.fillRoundedRectangle(presetBar, barCorner);
    g.fillRoundedRectangle(top, barCorner);
    g.setColour(juce::Colour(ink));
    g.setFont(juce::Font(faceFont(26.0f, true)));
    g.drawText("bqst", presetBar.withTrimmedLeft(18.0f).toNearestInt(), juce::Justification::centredLeft);
}

void BqtAudioProcessorEditor::RackComponent::paint(juce::Graphics& g)
{
    // Cache at the target's real pixel density (editor scale x display or snapshot scale) so the
    // plate stays sharp at any editor size.
    const auto layerScale = juce::jlimit(1.0f, 4.0f,
                                         juce::jmax(juce::Component::getApproximateScaleFactorForComponent(this),
                                                    g.getInternalContext().getPhysicalPixelScaleFactor()));
    const auto desiredWidth = static_cast<int>(std::ceil(static_cast<float>(getWidth()) * layerScale));
    const auto desiredHeight = static_cast<int>(std::ceil(static_cast<float>(getHeight()) * layerScale));

    if ((! faceplateCache.isValid() || faceplateCacheWidth != desiredWidth || faceplateCacheHeight != desiredHeight
         || ! juce::approximatelyEqual(faceplateCacheScale, layerScale))
        && desiredWidth > 0 && desiredHeight > 0)
    {
        faceplateCache = juce::Image(juce::Image::ARGB, desiredWidth, desiredHeight, true);
        juce::Graphics cacheGraphics(faceplateCache);
        cacheGraphics.addTransform(juce::AffineTransform::scale(layerScale));
        editor.paintRack(cacheGraphics);

        faceplateCacheWidth = desiredWidth;
        faceplateCacheHeight = desiredHeight;
        faceplateCacheScale = layerScale;
    }

    if (faceplateCache.isValid())
    {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(faceplateCache, 0, 0, getWidth(), getHeight(),
                    0, 0, faceplateCache.getWidth(), faceplateCache.getHeight());
    }
}

void BqtAudioProcessorEditor::paintRack(juce::Graphics& g)
{
    auto rack = rackComponent.getLocalBounds().toFloat();
    auto eqPanel = rack.removeFromLeft(rack.getWidth() * 0.5f);
    auto satPanel = rack;

    auto drawPlate = [&g](juce::Rectangle<float> plate, const juce::String& title)
    {
        // One clean painted pink: a barely-there top-to-bottom shift, no grain or brush lines.
        const auto pink = juce::Colour(panelPink);
        juce::ColourGradient plateGradient(pink, plate.getX(), plate.getY(),
                                           pink.interpolatedWith(juce::Colour(panelPinkDark), 0.35f),
                                           plate.getX(), plate.getBottom(), false);
        g.setGradientFill(plateGradient);
        g.fillRect(plate);

        // A single soft satin highlight across the upper part of the plate.
        juce::ColourGradient satin(juce::Colours::white.withAlpha(0.13f), plate.getCentreX(), plate.getY() + plate.getHeight() * 0.18f,
                                   juce::Colours::white.withAlpha(0.0f), plate.getCentreX() + plate.getWidth() * 0.95f,
                                   plate.getY() + plate.getHeight() * 0.18f, true);
        satin.addColour(0.45, juce::Colours::white.withAlpha(0.05f));
        g.setGradientFill(satin);
        g.fillRect(plate);

        g.setColour(juce::Colour(ink));
        g.drawRect(plate, 3.0f);

        const auto screwColour = juce::Colour(0xff7d7d7d);
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
            // Rack screws: a flat grey cross-head. Two greys, like the meter's screws.
            const auto screw = juce::Rectangle<float>(screwSize, screwSize).withCentre(screwCentre);
            g.setColour(screwColour);
            g.fillEllipse(screw);
            juce::Path cross;
            const auto arm = screwSize * 0.62f, thick = screwSize * 0.17f;
            cross.addRoundedRectangle(-arm * 0.5f, -thick * 0.5f, arm, thick, thick * 0.3f);
            cross.addRoundedRectangle(-thick * 0.5f, -arm * 0.5f, thick, arm, thick * 0.3f);
            g.setColour(screwColour.darker(0.25f));
            g.fillPath(cross, juce::AffineTransform::rotation(juce::degreesToRadians(20.0f)).translated(screwCentre));
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

        g.setColour(juce::Colour(plateInk));
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
        g.setColour(juce::Colour(plateInk));

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
        const auto x = static_cast<float>(left.getBounds().getCentreX() + right.getBounds().getCentreX()) * 0.5f;
        const auto y = static_cast<float>(left.getY() + right.getY()) * 0.5f + yOffset;
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

    auto drawSatCenterText = [&g](const juce::Slider& left, const juce::Slider& right, const juce::String& text, float yOffset)
    {
        const auto x = static_cast<float>(left.getBounds().getCentreX() + right.getBounds().getCentreX()) * 0.5f;
        const auto y = static_cast<float>(left.getBottom() + right.getBottom()) * 0.5f + yOffset;
        drawPanelText(g, text, juce::Rectangle<float>(132.0f, 42.0f).withCentre({ x, y }),
                      juce::Justification::centred, 34.0f);
    };

    drawSatCenterText(sideControls[0].drive, sideControls[1].drive, "drive", 1.0f);

    auto drawSatKnobText = [&g](const juce::Slider& slider, const juce::String& text)
    {
        const auto b = slider.getBounds().toFloat();
        drawPanelText(g, text, juce::Rectangle<float>(132.0f, 42.0f)
                                .withCentre({ b.getCentreX(), b.getBottom() - 2.0f }),
                      juce::Justification::centred, 30.0f);
    };

    for (const auto& controls : sideControls)
    {
        drawSatKnobText(controls.mix, "mix");
        drawSatKnobText(controls.outputTrim, "output");
    }
}

BqtAudioProcessorEditor::AboutPanel::AboutPanel()
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
    setWantsKeyboardFocus(true);
    setTitle("about");
    setDescription("bqst. Baxandall EQ and saturation. Concept, programming, DSP, UI and product design: "
                   "Rohan Kulshrestha. Version " JucePlugin_VersionString ", EBR Audio Tech.");
}

juce::Rectangle<int> BqtAudioProcessorEditor::AboutPanel::cardBounds() const
{
    return juce::Rectangle<int>(420, 230).withCentre(getLocalBounds().getCentre());
}

// The card's fill: everything the panel paints lies inside it.
bqst::vu::OpaqueCoverShape BqtAudioProcessorEditor::AboutPanel::opaqueShape() const
{
    return { cardBounds().toFloat(), 10.0f };
}

// A flat cream card with a hairline edge over the undimmed editor; every line in the face font's
// semibold, set apart by size and ink strength.
void BqtAudioProcessorEditor::AboutPanel::paint(juce::Graphics& g)
{
    const auto card = cardBounds();
    g.setColour(juce::Colour(menuPanel));
    g.fillPath(opaqueShape().toPath());
    g.setColour(juce::Colour(ink).withAlpha(0.25f));
    g.drawRoundedRectangle(card.toFloat().reduced(0.5f), 10.0f, 1.0f);

    auto line = [&g](const juce::String& text, juce::Rectangle<int> area, float height, float alpha,
                     juce::Justification justification = juce::Justification::centredLeft)
    {
        g.setColour(juce::Colour(ink).withAlpha(alpha));
        g.setFont(juce::Font(faceFont(height, true)));
        g.drawText(text, area, justification);
    };

    auto area = card.reduced(28, 22);
    line("bqst", area.removeFromTop(40), 32.0f, 1.0f);
    line("Baxandall EQ and saturation", area.removeFromTop(26), 17.0f, 0.75f);
    area.removeFromTop(16);
    line("Concept, programming, DSP, UI and product design", area.removeFromTop(22), 15.0f, 0.6f);
    line("Rohan Kulshrestha", area.removeFromTop(26), 17.0f, 1.0f);
    area.removeFromTop(16);
    line("Version " JucePlugin_VersionString ", EBR Audio Tech", area.removeFromTop(22), 15.0f, 0.6f);
    line("click anywhere to close", area.removeFromBottom(20), 13.0f, 0.45f, juce::Justification::centredRight);
}

// Beside the card a press closes it at once; on the card, the release does.
void BqtAudioProcessorEditor::AboutPanel::mouseDown(const juce::MouseEvent& event)
{
    if (! cardBounds().contains(event.getPosition()) && onClose)
        onClose();
}

void BqtAudioProcessorEditor::AboutPanel::mouseUp(const juce::MouseEvent& event)
{
    if (cardBounds().contains(event.getMouseDownPosition()) && onClose)
        onClose();
}

// Escape closes; Tab and undo/redo do nothing while it is open; anything else (the host's
// transport keys) passes through.
bool BqtAudioProcessorEditor::AboutPanel::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onClose)
            onClose();
        return true;
    }

    const auto mods = key.getModifiers();
    return key.getKeyCode() == juce::KeyPress::tabKey
        || ((mods.isCommandDown() || mods.isCtrlDown())
            && juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode())) == 'z');
}

std::unique_ptr<juce::AccessibilityHandler> BqtAudioProcessorEditor::AboutPanel::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::dialogWindow);
}

void BqtAudioProcessorEditor::RackComponent::setBypassed(bool shouldBeBypassed)
{
    if (bypassed == shouldBeBypassed)
        return;

    bypassed = shouldBeBypassed;
    repaint();
}

void BqtAudioProcessorEditor::RackComponent::paintOverChildren(juce::Graphics& g)
{
    if (! bypassed)
        return;

    const auto bounds = getLocalBounds().toFloat();
    // The same fills dim a native needle shown over the rack (bqst::vu::dimmedForBypass).
    g.setColour(bqst::vu::bypassShade);
    g.fillRect(bounds);
    g.setColour(bqst::vu::bypassWash);
    g.fillRect(bounds);
}

void BqtAudioProcessorEditor::resized()
{
    bqt::editor::storeWidth(audioProcessor.state().state, getWidth());
    const auto uiScale = static_cast<float>(getWidth()) / static_cast<float>(baseEditorWidth);

    auto applyUiScale = [uiScale](juce::Component& component)
    {
        component.setTransform(juce::AffineTransform::scale(uiScale));
    };

    for (auto* component : { static_cast<juce::Component*>(&rackComponent),
                             static_cast<juce::Component*>(&presetPrevious), static_cast<juce::Component*>(&presetMenuButton),
                             static_cast<juce::Component*>(&presetNext), static_cast<juce::Component*>(&presetSave),
                             static_cast<juce::Component*>(&aboutButton),
                             static_cast<juce::Component*>(&inputTrimLabel), static_cast<juce::Component*>(&inputTrim),
                             static_cast<juce::Component*>(&eqCaption), static_cast<juce::Component*>(&satCaption),
                             static_cast<juce::Component*>(&eqMode), static_cast<juce::Component*>(&satMode),
                             static_cast<juce::Component*>(&osRealtime), static_cast<juce::Component*>(&osRender),
                             static_cast<juce::Component*>(&autoGain), static_cast<juce::Component*>(&eqLink),
                             static_cast<juce::Component*>(&satLink),
                             static_cast<juce::Component*>(&bypass),
                             static_cast<juce::Component*>(&readoutBubble), static_cast<juce::Component*>(&aboutPanel) })
        applyUiScale(*component);

    auto bounds = juce::Rectangle<int>(0, 0, baseEditorWidth, baseEditorHeight).reduced(outerEditorMargin);
    auto headerSlot = bounds.removeFromTop(104);
    auto rackArea = bounds;
    rackArea.removeFromTop(10);
    auto header = getRackFaceBounds(rackArea.toFloat()).toNearestInt()
                      .withY(headerSlot.getY())
                      .withHeight(headerSlot.getHeight());
    presetBarBounds = header.removeFromTop(presetBarHeight);
    header.removeFromTop(headerRowGap);
    constexpr int pairGap = 4; // between the parts of one group

    // Row 1 (32 px controls): wordmark and presets from the left, bypass and about in the
    // top-right corner.
    auto presetRow = presetBarBounds.reduced(10, 8);
    presetRow.removeFromLeft(118); // wordmark
    auto placeLeft = [&](juce::Component& control, int width, int gapAfter)
    {
        control.setBounds(presetRow.removeFromLeft(width));
        presetRow.removeFromLeft(gapAfter);
    };
    placeLeft(presetPrevious, 30, pairGap);
    placeLeft(presetMenuButton, 200, pairGap);
    placeLeft(presetNext, 30, pairGap);
    placeLeft(presetSave, 56, 0);
    aboutButton.setBounds(presetRow.removeFromRight(68));
    presetRow.removeFromRight(8);
    bypass.setBounds(presetRow.removeFromRight(80));

    // Row 2 (30 px controls): input | eq mode + link | sat mode + link | oversampling | autogain.
    // Each group keeps its own widths; what is left is shared equally between the groups.
    auto globalRow = header.reduced(10, 7);
    struct Part { juce::Component* control; int width; };
    const std::vector<std::vector<Part>> groups {
        { { &inputTrimLabel, 38 }, { &inputTrim, 30 } },
        { { &eqCaption, 20 }, { &eqMode, 96 }, { &eqLink, 56 } },
        { { &satCaption, 26 }, { &satMode, 96 }, { &satLink, 56 } },
        { { &osRealtime, 110 }, { &osRender, 104 } },
        { { &autoGain, 84 } },
    };
    auto usedWidth = 0;
    for (const auto& group : groups)
        for (size_t i = 0; i < group.size(); ++i)
            usedWidth += group[i].width + (i > 0 ? pairGap : 0);
    const auto gaps = static_cast<int>(groups.size()) - 1;
    const auto spare = juce::jmax(0, globalRow.getWidth() - usedWidth);
    for (size_t g = 0; g < groups.size(); ++g)
    {
        for (size_t i = 0; i < groups[g].size(); ++i)
        {
            if (i > 0)
                globalRow.removeFromLeft(pairGap);
            groups[g][i].control->setBounds(globalRow.removeFromLeft(groups[g][i].width));
        }
        if (static_cast<int>(g) < gaps)
            globalRow.removeFromLeft(spare / gaps + (static_cast<int>(g) < spare % gaps ? 1 : 0));
    }

    bounds.removeFromTop(10);
    auto rackFloat = getRackFaceBounds(bounds.toFloat());
    auto rack = rackFloat.toNearestInt();
    rackComponent.setBounds(rack);
    aboutPanel.setBounds(juce::Rectangle<int>(0, 0, baseEditorWidth, baseEditorHeight));
    aboutPanel.toFront(false);
    auto rackLocal = juce::Rectangle<int>(0, 0, rack.getWidth(), rack.getHeight());
    auto eqPanel = rackLocal.removeFromLeft(rackLocal.getWidth() / 2).reduced(24, 26);
    auto satPanel = rackLocal.reduced(24, 26);

    auto& main = sideControls[0];
    auto& slave = sideControls[1];

    eqPanel.removeFromTop(46);
    const auto eqColW = (eqPanel.getWidth() - 24) / 2;
    auto eqLeft = eqPanel.removeFromLeft(eqColW);
    eqPanel.removeFromLeft(24);
    auto eqRight = eqPanel.removeFromLeft(eqColW);

    auto layoutEqColumn = [](SideControls& controls, juce::Rectangle<int> col)
    {
        controls.highGain.setBounds(col.getCentreX() - 88, col.getY() + 0, 176, 176);
        controls.highFreq.setBounds(col.getCentreX() - 53, col.getY() + 193, 106, 106);

        controls.lowGain.setBounds(col.getCentreX() - 88, col.getY() + 436, 176, 176);
        controls.lowFreq.setBounds(col.getCentreX() - 53, col.getY() + 313, 106, 106);
    };

    layoutEqColumn(main, eqLeft);
    layoutEqColumn(slave, eqRight);

    satPanel.removeFromTop(46);

    const auto satColW = (satPanel.getWidth() - 24) / 2;
    auto satLeft = satPanel.withTrimmedTop(278).removeFromLeft(satColW);
    auto satRight = satPanel.withTrimmedTop(278).withTrimmedLeft(satColW + 24);
    const auto leftDriveX = satLeft.getCentreX();
    const auto rightDriveX = satRight.getCentreX();

    const auto meterTop = satPanel.getY() - 32;
    constexpr int vuSize = 286;
    const auto screwX1 = static_cast<float>(satPanel.getX()) + static_cast<float>(satPanel.getWidth()) * 0.25f;
    const auto screwX2 = static_cast<float>(satPanel.getX()) + static_cast<float>(satPanel.getWidth()) * 0.75f;
    const auto previousVuSize = 272.0f;
    const auto leftMeterX = screwX1 - previousVuSize * 0.5f - 13.0f;
    const auto rightMeterRight = screwX2 + previousVuSize * 0.5f + 13.0f;
    meterA.setBounds(static_cast<int>(std::round(leftMeterX)), meterTop, vuSize, vuSize);
    meterB.setBounds(static_cast<int>(std::round(rightMeterRight - vuSize)), meterTop, vuSize, vuSize);

    vintage.setBounds(leftDriveX - 72, satPanel.getY() + 201, 144, 44);
    satTypeButton.setBounds(rightDriveX - 72, satPanel.getY() + 195, 144, 56);

    auto layoutSatColumn = [](SideControls& controls, juce::Rectangle<int> col)
    {
        controls.drive.setBounds(col.getCentreX() - 88, col.getY() - 18, 176, 176);
        controls.mix.setBounds(col.getCentreX() - 96, col.getY() + 180, 106, 106);
        controls.outputTrim.setBounds(col.getCentreX() - 10, col.getY() + 180, 106, 106);
    };

    layoutSatColumn(main, satLeft);
    layoutSatColumn(slave, satRight);

    vintage.toFront(false);
    satTypeButton.toFront(false);
    readoutBubble.toFront(false);
    aboutPanel.toFront(false);
}

// About is modal for this editor only (a real modal state would also block every other open
// BQST): it appears at once, takes the keyboard focus, and the controls underneath leave the
// accessibility tree until it closes.
void BqtAudioProcessorEditor::showAboutPanel()
{
    if (aboutPanel.isVisible())
        return;

    hideReadout();
    hideTopBarHelp();
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    focusBeforeAbout = focused != nullptr && (focused == this || isParentOf(focused)) ? focused : nullptr;
    hiddenFromAccessibility.clear();
    for (auto* child : getChildren())
        if (child != &aboutPanel && child->isAccessible())
        {
            hiddenFromAccessibility.emplace_back(child);
            child->setAccessible(false);
        }

    aboutPanel.setVisible(true);
    aboutPanel.toFront(false);
    if (aboutPanel.isShowing())
        aboutPanel.grabKeyboardFocus();
}

void BqtAudioProcessorEditor::hideAboutPanel()
{
    if (! aboutPanel.isVisible())
        return;

    const auto hadFocus = aboutPanel.hasKeyboardFocus(true);
    aboutPanel.setVisible(false);
    for (auto& child : hiddenFromAccessibility)
        if (child != nullptr)
            child->setAccessible(true);
    hiddenFromAccessibility.clear();

    // Focus goes back where it was; if it came from outside the editor, it is let go.
    if (auto* previous = focusBeforeAbout.getComponent(); previous != nullptr && previous->isShowing())
        previous->grabKeyboardFocus();
    else if (hadFocus)
        giveAwayKeyboardFocus();
    focusBeforeAbout = nullptr;
}

void BqtAudioProcessorEditor::setFocusRingsVisible(bool shouldShow)
{
    if (hardwareLookAndFeel.areFocusRingsVisible() == shouldShow)
        return;

    hardwareLookAndFeel.setFocusRingsVisible(shouldShow);
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent(); focused != nullptr && isParentOf(focused))
        focused->repaint();
}
