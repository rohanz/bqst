#include "BqtVuMeter.h"

#include "BqtEditorStyle.h"
#include "PluginProcessor.h"

#include <array>
#include <cmath>

using namespace bqst::ui;

namespace
{
// Deflection of a needle reading in VU (0 VU = 1).
float vuToDeflection(float vu) { return juce::Decibels::decibelsToGain(vu, -100.0f); }
} // namespace

float bqst::vu::vuDeflectionToScaleFraction(float deflection)
{
    static const auto fullScale = std::pow(10.0f, 3.0f / 20.0f); // +3 VU
    return juce::jlimit(0.0f, 1.0f, deflection / fullScale);
}

namespace
{

namespace cleanvu
{
// Same 2000 px geometry as the artwork so the face, scale and needle sit where they always did.
constexpr float artSize = 2000.0f, centreX = 1000.0f, centreY = 865.0f, faceRadius = 569.0f, cutY = 1189.0f;
// Slimmer bezel: the strip under the window is a little shallower than the artwork's, deep enough
// to read as a meter housing.
constexpr float plateLeft = 374.0f, plateRight = 1626.0f, plateTop = 258.0f, plateBottom = 1500.0f, plateCorner = 44.0f;
constexpr float ringRadius = 598.0f; // thinner than the artwork's ring (about 646)
constexpr float screwRadius = 42.0f;

void paint(juce::Graphics& g, juce::Rectangle<float> frame)
{
    const auto unit = frame.getWidth() / artSize;
    const auto pt = [frame, unit](float x, float y) { return juce::Point<float>(frame.getX() + x * unit, frame.getY() + y * unit); };
    const auto centre = pt(centreX, centreY);

    const auto plate = juce::Rectangle<float>(pt(plateLeft, plateTop), pt(plateRight, plateBottom));
    const auto plateColour = juce::Colour(0xff3a3735); // soft warm charcoal, lighter than the old near-black

    // One tight shadow, then a flat plate.
    g.setColour(juce::Colours::black.withAlpha(0.12f));
    g.fillRoundedRectangle(plate.translated(0.0f, 8.0f * unit).expanded(1.0f * unit), plateCorner * unit);
    g.setColour(plateColour);
    g.fillRoundedRectangle(plate, plateCorner * unit);

    // Thin flat ring around the window, stopping at the bottom bar.
    const auto cut = pt(0.0f, cutY).y;
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(juce::Rectangle<float>(plate.getX(), plate.getY(), plate.getWidth(), cut - plate.getY()).toNearestIntEdges());
        g.setColour(plateColour.brighter(0.11f));
        g.fillEllipse(juce::Rectangle<float>(ringRadius * 2.0f * unit, ringRadius * 2.0f * unit).withCentre(centre));

        // Plain cream face with only a faint top-to-bottom shift.
        const auto face = juce::Rectangle<float>(faceRadius * 2.0f * unit, faceRadius * 2.0f * unit).withCentre(centre);
        juce::ColourGradient cream(juce::Colour(0xfff6f2e8), 0.0f, face.getY(), juce::Colour(0xffefe9dc), 0.0f, cut, false);
        g.setGradientFill(cream);
        g.fillEllipse(face);
    }

    // Bottom bar edge: one hairline so the bar reads as a separate strip.
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.fillRect(juce::Rectangle<float>(plate.getX(), cut, plate.getWidth(), juce::jmax(0.6f, 3.0f * unit)));

    // The artwork's screws: a flat grey head with one slightly darker inset and nothing else.
    // Corner screws carry a rounded hexagon socket, the zero adjust a rounded slot.
    const auto headColour = juce::Colour(0xff9f9f9f);
    const auto insetColour = juce::Colour(0xff898787);
    const auto drawHead = [&](juce::Point<float> c)
    {
        const auto r = screwRadius * unit;
        g.setColour(headColour);
        g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c));
    };
    const auto drawHexScrew = [&](juce::Point<float> c)
    {
        drawHead(c);
        juce::Path socket;
        socket.addPolygon(c, 6, 27.0f * unit, juce::degreesToRadians(15.0f));
        g.setColour(insetColour);
        g.fillPath(socket.createPathWithRoundedCorners(7.0f * unit));
    };
    for (const auto& c : { pt(462.0f, 346.0f), pt(1538.0f, 346.0f), pt(462.0f, 1345.0f), pt(1538.0f, 1345.0f) })
        drawHexScrew(c);

    const auto zeroAdjust = pt(centreX, 1345.0f);
    drawHead(zeroAdjust);
    juce::Path slot;
    slot.addRoundedRectangle(-31.0f * unit, -9.0f * unit, 62.0f * unit, 18.0f * unit, 5.0f * unit);
    g.setColour(insetColour);
    g.fillPath(slot, juce::AffineTransform::rotation(juce::degreesToRadians(-42.0f)).translated(zeroAdjust));
}
} // namespace cleanvu

// The needle, shared by the painted needle and the native layer so the two are identical.
const auto needleColour = juce::Colour(0xff1f1a17);
constexpr float needleWidth = 2.6f;
constexpr float pivotDotDiameter = 6.5f;
constexpr float needleRepaintPadding = 6.0f;

// Where the end signs sit: degrees from each end of the arc and depth inside it.
constexpr float endSignMinusOffset = 1.0f, endSignPlusOffset = -5.0f, endSignInset = 14.0f;

struct DialGeometry
{
    explicit DialGeometry(juce::Rectangle<float> bounds)
        : frame(bounds.reduced(1.0f)),
          inner(frame.withTrimmedLeft(frame.getWidth() * 0.27f)
                     .withTrimmedRight(frame.getWidth() * 0.27f)
                     .withTrimmedTop(frame.getHeight() * 0.22f)
                     .withTrimmedBottom(frame.getHeight() * 0.35f)),
          centre(inner.getCentreX(), inner.getY() + inner.getHeight() * 0.87f),
          pivot(inner.getCentreX(), inner.getY() + inner.getHeight() * 0.88f),
          radius(inner.getWidth() * 0.62f) {}

    juce::Rectangle<float> frame, inner;
    juce::Point<float> centre, pivot;
    float radius;
    static constexpr float start = 137.0f, end = 43.0f;
    static constexpr float needleExtension = 7.0f;
};
} // namespace

BqtVuMeter::BqtVuMeter(BqtAudioProcessor& p, int sideIndex)
    : audioProcessor(p), side(sideIndex)
{
}

BqtVuMeter::~BqtVuMeter()
{
    nativeNeedle.detach();
}

void BqtVuMeter::setNeedleClock(bqst::vu::NeedleClock* clock)
{
    dropNativeNeedle();
    needleClock = clock;
    nativeNeedle.setClock(clock);
}

void BqtVuMeter::paint(juce::Graphics& g)
{
    // The target's real pixel density (editor scale x display or snapshot scale) keeps the face
    // sharp at any editor size.
    // Exactly the target's pixel density, no more: an oversized layer has to be resampled down
    // on every needle frame, which is the costliest part of drawing the meter.
    // On screen this never exceeds 4 (2x editor on a 2x display); the higher bound only serves
    // large offscreen renders.
    const auto layerScale = juce::jlimit(1.0f, 16.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto desiredLayerWidth = static_cast<int>(std::ceil(static_cast<float>(getWidth()) * layerScale));
    const auto desiredLayerHeight = static_cast<int>(std::ceil(static_cast<float>(getHeight()) * layerScale));

    if (staticLayerWidth != desiredLayerWidth || staticLayerHeight != desiredLayerHeight
        || ! juce::approximatelyEqual(staticLayerScale, layerScale) || ! staticLayer.isValid())
        rebuildStaticLayer(layerScale);

    if (staticLayer.isValid())
    {
        // The layer already matches the pixel grid, so the costly high-quality filter buys nothing.
        g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
        g.drawImage(staticLayer, 0, 0, getWidth(), getHeight(),
                    0, 0, staticLayer.getWidth(), staticLayer.getHeight());
    }

    // While the native layer presents the needle, only the dial is painted here.
    if (needleIsNative)
        return;

    lastPaintedLevel = displayedLevel;
    const auto needle = needleLine(displayedLevel);
    g.setColour(needleColour);
    g.drawLine(needle, needleWidth);
    g.setColour(needleColour.darker(0.35f));
    g.fillEllipse(juce::Rectangle<float>(pivotDotDiameter, pivotDotDiameter).withCentre(needle.getStart()));
}

// Pivot to tip, for a level on the 0..1 scale. Same geometry the scale is drawn with.
juce::Line<float> BqtVuMeter::needleLine(float level) const
{
    const DialGeometry dial(getLocalBounds().toFloat());
    return { dial.pivot, bqst::vu::needleTip(dial.centre, dial.radius + DialGeometry::needleExtension,
                                             DialGeometry::start, DialGeometry::end, level) };
}

// Only the strip the needle left and the strip it moved to need redrawing, not the whole meter.
void BqtVuMeter::repaintNeedle(float previousLevel)
{
    const auto area = [this](float level)
    {
        const auto line = needleLine(level);
        return juce::Rectangle<float>(line.getStart(), line.getEnd()).expanded(needleRepaintPadding).getSmallestIntegerContainer();
    };
    repaint(area(previousLevel));
    repaint(area(displayedLevel));
}

void BqtVuMeter::repaintNeedleAt(float level)
{
    const auto line = needleLine(level);
    repaint(juce::Rectangle<float>(line.getStart(), line.getEnd()).expanded(needleRepaintPadding).getSmallestIntegerContainer());
}

juce::Rectangle<int> BqtVuMeter::needleSweepArea() const
{
    // The tip runs along an arc; its highest point is at the middle of the scale.
    juce::Rectangle<float> area(needleLine(0.0f).getStart(), needleLine(0.0f).getEnd());
    for (int i = 1; i <= 32; ++i)
    {
        const auto line = needleLine(static_cast<float>(i) / 32.0f);
        area = area.getUnion(juce::Rectangle<float>(line.getStart(), line.getEnd()));
    }
    return area.expanded(needleRepaintPadding).getSmallestIntegerContainer().getIntersection(getLocalBounds());
}

juce::Rectangle<int> BqtVuMeter::paintedArea() const
{
    // The housing plate and its shadow, in the 2000 px art space the meter is drawn in.
    const auto frame = DialGeometry(getLocalBounds().toFloat()).frame.toNearestInt().toFloat();
    const auto unit = frame.getWidth() / cleanvu::artSize;
    return juce::Rectangle<float>(frame.getX() + cleanvu::plateLeft * unit, frame.getY() + cleanvu::plateTop * unit,
                                  (cleanvu::plateRight - cleanvu::plateLeft) * unit,
                                  (cleanvu::plateBottom - cleanvu::plateTop + 10.0f) * unit)
        .expanded(2.0f)
        .getSmallestIntegerContainer()
        .getIntersection(getLocalBounds());
}

// Shows the needle in the native layer, moved from here on by the editor's needle clock; false if
// the layer cannot be used here and now (then nothing of it is on screen).
bool BqtVuMeter::presentNeedle()
{
    using namespace bqst::vu;

    auto* peer = getPeer();
    NeedleConditions conditions;
    conditions.hasClock = needleClock != nullptr;
    conditions.showing = isShowing();
    conditions.hasHostView = peer != nullptr && peer->getNativeHandle() != nullptr;
    // The other meter's bounds overlap this one's, but it paints only its housing.
    const auto paintedAreaOf = [](const juce::Component& c)
    {
        auto* meter = dynamic_cast<const BqtVuMeter*>(&c);
        return meter != nullptr ? meter->paintedArea() : c.getLocalBounds();
    };
    // What BQST paints over the sweep: left out of the layer if every cover reports its shape.
    const auto cover = conditions.showing ? coverFromAbove(*this, needleSweepArea(), paintedAreaOf) : CoverFromAbove {};
    conditions.coveredFromAbove = ! cover.excludable;
    if (chooseNeedleMode(conditions) != NeedleMode::native)
        return false;

    // Into the peer view's coordinates: through every component transform (the editor scales its
    // contents) and the desktop scale, exactly where the painted needle would land.
    auto& top = peer->getComponent();
    const auto desktopScale = top.getDesktopScaleFactor();
    const auto toView = [&](juce::Point<float> p) { return top.getLocalPoint(this, p) * desktopScale; };
    const auto origin = toView({ 0.0f, 0.0f });
    const auto xAxis = toView({ 1.0f, 0.0f });
    const auto yAxis = toView({ 0.0f, 1.0f });

    const DialGeometry dial(getLocalBounds().toFloat());
    NeedleGeometry geometry;
    geometry.clip = juce::Rectangle<float>(origin, toView({ static_cast<float>(getWidth()), static_cast<float>(getHeight()) }));
    geometry.meterToView = juce::AffineTransform(xAxis.x - origin.x, yAxis.x - origin.x, origin.x,
                                                 xAxis.y - origin.y, yAxis.y - origin.y, origin.y);
    geometry.pivot = dial.pivot;
    geometry.centre = dial.centre;
    geometry.radius = dial.radius + DialGeometry::needleExtension;
    geometry.startDegrees = DialGeometry::start;
    geometry.endDegrees = DialGeometry::end;
    const auto unit = origin.getDistanceFrom(xAxis);
    geometry.lineWidth = needleWidth * unit;
    geometry.dotDiameter = pivotDotDiameter * unit;
    // Exactly what the rack's bypass fills make of the painted needle: the fills are affine in
    // colour, so the layer's antialiased edges over the dimmed face match too.
    const auto colourOf = [this](juce::Colour c) { return needleDimmed ? dimmedForBypass(c) : c; };
    geometry.exclusion = cover.shape;
    geometry.exclusion.applyTransform(geometry.meterToView);
    geometry.needleColour = colourOf(needleColour);
    geometry.dotColour = colourOf(needleColour.darker(0.35f));

    // Read on the clock thread at every refresh: the audio-side movement, lock-free.
    auto reading = [&processor = audioProcessor, meterSide = side]
    {
        return bqst::vu::vuDeflectionToScaleFraction(processor.getMeterLevel(meterSide));
    };
    return nativeNeedle.present(peer->getNativeHandle(), geometry, displayedLevel, std::move(reading));
}

void BqtVuMeter::showPaintedNeedle()
{
    // Without a peer the view the layer sits on is gone or going: let go of it entirely. JUCE
    // sends no notice when a peer is deleted, so this also catches removeFromDesktop.
    if (getPeer() == nullptr)
        nativeNeedle.detach();

    if (! needleIsNative)
        return;
    nativeNeedle.hide();
    needleIsNative = false;
    repaintNeedleAt(displayedLevel);
}

void BqtVuMeter::syncNeedlePresentation(bool rackDimmed)
{
    // While bypassed the audio side moves the needle to the left stop and holds it there exactly.
    // The needle stays in the layer throughout, in the dimmed colours (a colour change never moves
    // it), so the fall is at display rate; once the reading stops changing, the clock commits
    // nothing more on its own (see BqtVuNeedleLayer_mac.mm), so a resting needle costs no drawing.
    needleDimmed = rackDimmed;
    // The audio-side movement already supplies the physical response; the clock applies it.
    if (! presentNeedle())
    {
        showPaintedNeedle();
        return;
    }

    if (! needleIsNative)
    {
        // Take the painted needle away in the same frame the layer appears.
        needleIsNative = true;
        repaintNeedleAt(displayedLevel);
    }
}

void BqtVuMeter::syncNeedleGeometry()
{
    if (needleIsNative && ! presentNeedle())
        showPaintedNeedle();
}

void BqtVuMeter::dropNativeNeedle()
{
    const auto wasNative = needleIsNative;
    nativeNeedle.detach();
    needleIsNative = false;
    if (wasNative)
        repaintNeedleAt(displayedLevel);
}

void BqtVuMeter::rebuildStaticLayer(float layerScale)
{
    const auto logicalWidth = getWidth();
    const auto logicalHeight = getHeight();
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
    const DialGeometry dial(bounds);
    const auto frameBounds = dial.frame;
    const auto inner = dial.inner;

    const auto frameArea = frameBounds.toNearestInt();
    juce::Graphics g(staticLayer);
    g.addTransform(juce::AffineTransform::scale(layerScale));
    cleanvu::paint(g, frameArea.toFloat());

    auto polar = [](juce::Point<float> centre, float radius, float degrees)
    {
        const auto radians = juce::degreesToRadians(degrees);
        return juce::Point<float>(centre.x + std::cos(radians) * radius,
                                  centre.y - std::sin(radians) * radius);
    };

    const auto centre = dial.centre;
    const auto radius = dial.radius;
    const auto start = DialGeometry::start;
    const auto end = DialGeometry::end;

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

    // The scale is linear in amplitude, as on a moving-coil VU: the marks crowd towards the left.
    const auto angleOf = [start, end](float vu)
    {
        return start + (end - start) * bqst::vu::vuDeflectionToScaleFraction(vuToDeflection(vu));
    };

    juce::Path redArc;
    for (int i = 0; i <= 18; ++i)
    {
        const auto angle = angleOf(0.0f) + (end - angleOf(0.0f)) * (static_cast<float>(i) / 18.0f);
        const auto point = polar(centre, radius - 3.0f, angle);
        i == 0 ? redArc.startNewSubPath(point) : redArc.lineTo(point);
    }
    g.setColour(meterRed.withAlpha(0.92f));
    g.strokePath(redArc, juce::PathStrokeType(3.4f));

    // Numbered marks of the classic scale. Plain numbers; the scale carries one minus sign at its
    // left end and one plus at its right.
    struct Mark { float vu; const char* label; };
    const std::array<Mark, 11> majors { { { -20.0f, "20" }, { -10.0f, "10" }, { -7.0f, "7" }, { -5.0f, "5" },
                                          { -3.0f, "3" }, { -2.0f, "2" }, { -1.0f, "1" }, { 0.0f, "0" },
                                          { 1.0f, "1" }, { 2.0f, "2" }, { 3.0f, "3" } } };
    const auto labelFont = juce::Font(vuFont(11.07f, false));
    for (const auto& mark : majors)
    {
        const auto angle = angleOf(mark.vu);
        const auto red = mark.vu >= 0.0f;
        g.setColour(red ? meterRed.withAlpha(0.98f)
                        : meterBlack.withAlpha(0.95f));
        g.drawLine({ polar(centre, radius - 7.0f, angle), polar(centre, radius + 3.0f, angle) },
                   mark.vu == 0.0f ? 2.6f : 1.9f);
        // Centre the number on its tick, with the near edge of its ink box a fixed gap past the
        // tick end; the box's extent along the tick direction depends on the angle.
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(labelFont, mark.label, 0.0f, 0.0f);
        const auto inkBox = glyphs.getBoundingBox(0, -1, true);
        const auto radians = juce::degreesToRadians(angle);
        const auto extent = std::abs(inkBox.getWidth() * 0.5f * std::cos(radians))
                          + std::abs(inkBox.getHeight() * 0.5f * std::sin(radians));
        const auto labelCentre = polar(centre, radius + 3.0f + 2.5f + extent, angle);
        glyphs.draw(g, juce::AffineTransform::translation(labelCentre - inkBox.getCentre()));
    }

    // One unnumbered mark halves the wide -20..-10 span; everywhere else the numbered marks are
    // already as close as the smallest editor size can separate.
    {
        const auto angle = angleOf(-15.0f);
        g.setColour(meterBlack.withAlpha(0.58f));
        g.drawLine({ polar(centre, radius - 4.0f, angle), polar(centre, radius + 2.5f, angle) }, 1.2f);
    }

    const auto drawEndSign = [&](const juce::String& sign, juce::Colour colour, float angle, float inset)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(juce::Font(vuFont(11.07f * 1.25f, true)), sign, 0.0f, 0.0f);
        g.setColour(colour);
        glyphs.draw(g, juce::AffineTransform::translation(polar(centre, radius - inset, angle)
                                                         - glyphs.getBoundingBox(0, -1, true).getCentre()));
    };
    // Inside the arc: the minus at the rest stop, left of the crowded low end; the plus under the
    // red band, between the +2 and +3 marks.
    drawEndSign(juce::String::charToString(static_cast<juce::juce_wchar>(0x2212)), meterBlack.withAlpha(0.95f),
                start + endSignMinusOffset, endSignInset);
    drawEndSign("+", meterRed.withAlpha(0.98f), end + endSignPlusOffset, endSignInset);

    g.setColour(meterBlack.withAlpha(0.98f));
    g.setFont(juce::Font(vuFont(19.0f * 0.98f, true)));
    g.drawText("vu", inner.withTrimmedTop(inner.getHeight() * 0.38f).withHeight(22.0f).toNearestInt(),
               juce::Justification::centred);
}

bool BqtVuMeter::updateLevel()
{
    const auto raw = audioProcessor.getMeterLevel(side);
    const auto previousLevel = lastPaintedLevel;
    displayedLevel = bqst::vu::vuDeflectionToScaleFraction(raw);

    if (std::abs(displayedLevel - previousLevel) < 0.0001f)
        return false;
    // The native layer is moved by the needle clock instead.
    if (! needleIsNative)
        repaintNeedle(previousLevel);
    return true;
}
