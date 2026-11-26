#pragma once

// The VU needle can be presented in a small Core Animation layer over each meter instead of being
// painted with the rest of the editor, and moved by a clock of its own.
//
// Why: a painted needle can only move when the host's message thread lets JUCE run its display
// callback and paint. Hosts throttle that thread (Ableton Live was seen delivering 60 callbacks a
// second, and fewer while busy), so a painted needle cannot follow a 120 Hz screen. The needle's
// physics already run on the audio side and can be read from any thread at any instant
// (bqt::MeterCapture::level), so on macOS a display-link thread bound to the editor's screen reads
// the reading at every refresh and moves the needle layer in a Core Animation transaction of its
// own. Those commits reach the screen without the message thread (see tests/vu_needle_tests.mm,
// which captures the window while the message thread is stalled).
//
// Division of work:
//  - the clock thread (NeedleClock) only replaces the needle's path, at every refresh where the
//    reading changed;
//  - the message thread supplies everything else when it changes (where the meter is, its
//    scale, clip, colours, what covers it, the screen the window is on) and shows or hides the
//    layer. Those changes land in the message thread's own Core Animation commit, the same one
//    that carries JUCE's repaint (of the painted needle, or of a cover appearing, moving or going),
//    so a hand-over never shows both needles or neither, and a cover is never a frame early or late.
//
// Only the needle and its pivot dot live in the layer; the dial, scale and everything else are
// painted as before. Anything BQST paints over the needle's sweep that reports its opaque shape
// (OpaqueCover: the About card, the value readout) is left out of the layer by a mask of that
// shape, so the needle still appears to pass behind it. The bypassed look, painted over the whole
// rack, is matched by dimming the needle's colours instead (see BqtVuMeter). The painted needle
// stays as the fallback wherever the layer is unavailable: other platforms, offscreen snapshots,
// a host view without a layer, a cover that does not report its shape (or two that overlap),
// or with the switch off.
//
// The clock commits only readings that changed: a needle at rest (held there while bypassed, or
// fully released) costs one atomic read per refresh and nothing for the compositor.
//
// Compile-time switch: configure with -DBQST_VU_NATIVE_NEEDLE=OFF (CMake) to always paint the needle.

#include <juce_gui_basics/juce_gui_basics.h>

#include "BqtFrameLog.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>

#ifndef BQST_VU_NATIVE_NEEDLE
 #define BQST_VU_NATIVE_NEEDLE 0
#endif

#if BQST_VU_NATIVE_NEEDLE && ! JUCE_MAC
 #undef BQST_VU_NATIVE_NEEDLE
 #define BQST_VU_NATIVE_NEEDLE 0
#endif

namespace bqst::vu
{
inline constexpr bool nativeNeedleCompiledIn = BQST_VU_NATIVE_NEEDLE != 0;

// The needle tip for a position on the scale (0 = left stop, 1 = right stop). The painted needle
// and the clock use this one function, so both draw the same line for the same reading.
inline juce::Point<float> needleTip(juce::Point<float> centre, float radius, float startDegrees, float endDegrees,
                                    float scaleFraction)
{
    const auto radians = juce::degreesToRadians(startDegrees + juce::jlimit(0.0f, 1.0f, scaleFraction) * (endDegrees - startDegrees));
    return { centre.x + std::cos(radians) * radius, centre.y - std::sin(radians) * radius };
}

// Everything about the needle except where it points, supplied by the message thread when it
// changes. Dial values are in the meter's own coordinates; meterToView maps them into the host
// view's coordinates (points, origin top left, y down, the JUCE peer's space), where clip, line
// width and dot diameter already are.
struct NeedleGeometry
{
    juce::Rectangle<float> clip;  // the meter's bounds: the needle is clipped here, as when painted
    juce::AffineTransform meterToView;
    juce::Point<float> pivot, centre;
    float radius = 0.0f, startDegrees = 0.0f, endDegrees = 0.0f;
    float lineWidth = 0.0f, dotDiameter = 0.0f;
    juce::Colour needleColour, dotColour;
    // What is painted over the needle and left out of the layer (OpaqueCover shapes), in view
    // coordinates; empty when nothing covers it.
    juce::Path exclusion;

    juce::Point<float> tipInView(float scaleFraction) const
    {
        return needleTip(centre, radius, startDegrees, endDegrees, scaleFraction).transformedBy(meterToView);
    }
    juce::Point<float> pivotInView() const { return pivot.transformedBy(meterToView); }

    // Everything that decides where the needle is drawn, colours and exclusion aside.
    bool sameShape(const NeedleGeometry& o) const
    {
        const auto& a = meterToView;
        const auto& b = o.meterToView;
        return clip == o.clip && pivot == o.pivot && centre == o.centre && radius == o.radius
            && startDegrees == o.startDegrees && endDegrees == o.endDegrees && lineWidth == o.lineWidth
            && dotDiameter == o.dotDiameter
            && a.mat00 == b.mat00 && a.mat01 == b.mat01 && a.mat02 == b.mat02
            && a.mat10 == b.mat10 && a.mat11 == b.mat11 && a.mat12 == b.mat12;
    }
    bool operator==(const NeedleGeometry& o) const
    {
        return sameShape(o) && needleColour == o.needleColour && dotColour == o.dotColour && exclusion == o.exclusion;
    }
    bool operator!=(const NeedleGeometry& o) const { return ! operator==(o); }
};

// The needle's scale position now (0..1). Called on the clock thread at every refresh, so it
// must be lock-free, allocation-free and safe to call from any thread.
using NeedleReading = std::function<float()>;

// The shape a cover paints opaquely: a rounded rectangle, in the component's own coordinates.
// Only this: its outline never crosses itself, so the layer's even-odd cut-out of it is exactly
// the area the component fills. (An arbitrary path could overlap itself and show the needle again
// where it does.) The component paints its fill with toPath(), so the two always agree.
struct OpaqueCoverShape
{
    juce::Rectangle<float> area;
    float cornerSize = 0.0f;

    juce::Path toPath() const
    {
        juce::Path path;
        path.addRoundedRectangle(area, cornerSize);
        return path;
    }
};

// A component that says where it paints opaquely. Painted over a needle's sweep, it is left out of
// the native needle layer (the layer is masked by its shape), so the needle stays at display rate
// and still appears to pass behind it. Contract: everything the component paints lies inside the
// shape, inside its bounds, and is fully opaque. The editor re-presents the needles whenever such
// a component is shown, hidden, moved or brought to front, in the same message-thread turn as
// that change's repaint, so a frame never shows the cover with an old mask.
//
// The cut-out's antialiased edge is not true compositing under the cover: on the one-pixel fringe
// where the cover has coverage a over the needle N, the cover C and the face B, the screen shows
// (1-a)N + a(aC + (1-a)B) where painting the needle under the cover gives (1-a)N + aC; they differ
// by a(1-a)(B-C), at most a quarter of B-C. That is the limit of a layer above the paint.
class OpaqueCover
{
public:
    virtual ~OpaqueCover() = default;
    virtual OpaqueCoverShape opaqueShape() const = 0;
};

// The transform from `from`'s coordinates into `to`'s, through every component transform.
inline juce::AffineTransform transformBetween(const juce::Component& from, const juce::Component& to)
{
    const auto o = to.getLocalPoint(&from, juce::Point<float>());
    const auto x = to.getLocalPoint(&from, juce::Point<float>(1.0f, 0.0f));
    const auto y = to.getLocalPoint(&from, juce::Point<float>(0.0f, 1.0f));
    return { x.x - o.x, y.x - o.x, o.x, x.y - o.y, y.y - o.y, o.y };
}

// What is painted over `area` (in component's coordinates) after `component`: by later siblings of
// it or of any of its ancestors, up to its top-level component. One rule for every cover: a cover
// that reports its opaque shape (OpaqueCover, at full component alpha) is collected into `shape`
// (component's coordinates) for the layer to leave out; any other cover over the area cannot be
// left out exactly, so the needle must be painted (excludable = false). Two reported covers that
// overlap each other also count as not excludable: the layer's even-odd mask would show the needle
// again where they overlap. paintedAreaOf, if given, narrows a sibling that is not an OpaqueCover
// to the part of its bounds it actually paints (in its own coordinates); by default all of it.
// A parent's paintOverChildren is not visible from here (the editor's bypassed look is one; the
// needle copes with it by taking the dimmed colours).
struct CoverFromAbove
{
    bool excludable = true;
    juce::Path shape;
};

inline CoverFromAbove coverFromAbove(const juce::Component& component, juce::Rectangle<int> area,
                                     const std::function<juce::Rectangle<int>(const juce::Component&)>& paintedAreaOf = {})
{
    CoverFromAbove result;
    const juce::Component* child = &component;
    for (auto* parent = component.getParentComponent(); parent != nullptr;
         child = parent, parent = parent->getParentComponent())
    {
        const auto index = parent->getIndexOfChildComponent(child);
        for (int i = index + 1; i < parent->getNumChildComponents(); ++i)
        {
            auto* sibling = parent->getChildComponent(i);
            if (sibling == nullptr || ! sibling->isVisible())
                continue;
            const auto areaInSibling = sibling->getLocalArea(&component, area);
            auto* opaque = dynamic_cast<const OpaqueCover*>(sibling);
            if (opaque != nullptr && sibling->getAlpha() >= 1.0f)
            {
                auto shape = opaque->opaqueShape().toPath();
                if (! shape.getBounds().intersects(areaInSibling.toFloat()))
                    continue;
                shape.applyTransform(transformBetween(*sibling, component));
                if (! result.shape.isEmpty() && result.shape.getBounds().intersects(shape.getBounds()))
                    return { false, {} };
                result.shape.addPath(shape);
                continue;
            }
            const auto painted = paintedAreaOf ? paintedAreaOf(*sibling) : sibling->getLocalBounds();
            if (painted.intersects(areaInSibling))
                return { false, {} };
        }
    }
    return result;
}

enum class NeedleMode
{
    painted,
    native
};

struct NeedleConditions
{
    bool compiledIn = nativeNeedleCompiledIn;
    bool hasClock = false;       // a needle clock was supplied by the editor
    bool showing = false;        // the meter is on screen in a window
    bool hasHostView = false;    // there is a native peer view to attach to
    bool coveredFromAbove = false;  // by something the layer cannot leave out (coverFromAbove)
};

inline NeedleMode chooseNeedleMode(const NeedleConditions& c)
{
    return c.compiledIn && c.hasClock && c.showing && c.hasHostView && ! c.coveredFromAbove
         ? NeedleMode::native
         : NeedleMode::painted;
}

// One per editor: a display link bound to the screen the editor's window is on, which moves every
// shown needle at each refresh of that screen, off the message thread. It runs only while at least
// one needle is shown. Stopping waits for a tick in progress, so after stop() (and in the
// destructor) no code of this clock runs on any thread; a tick never waits for the message thread.
// All member functions are message-thread only.
class NeedleClock
{
public:
    NeedleClock();
    ~NeedleClock();

    // Binds the clock to the screen hostView's (an NSView*) window is on. Cheap when nothing
    // changed; called whenever a needle is presented, so a window moved to another screen, or a
    // screen whose refresh rate changed, is picked up at the next display callback or move.
    void follow(void* hostView);
    // Permanently stops ticking and waits out a tick in progress. Shown needles stay where they are.
    // Used at editor teardown; later layer detach/present calls cannot restart this clock.
    void stop();

    // Counters the clock thread updates for the frame log (owned here, read by its writer).
    bqt::FrameLogNeedle& frameLogStats();

    struct Probe
    {
        bool running = false;
        uint32_t display = 0;       // CGDirectDisplayID the clock is bound to
        double refreshHz = 0.0;     // the display link's current refresh rate
        uint64_t ticks = 0, presents = 0;
        double lastPresentMs = 0.0; // FrameLog::nowMs() clock
    };
    Probe probe() const;

private:
    friend class NativeNeedle;
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE(NeedleClock)
};

// The needle and pivot dot in a Core Animation layer added on top of the JUCE peer view's own
// layer, moved by a NeedleClock. Plain CALayer instances only: no custom Objective-C classes,
// delegates, observers or blocks, so nothing of it can call back into the plugin after detach.
// Message thread only.
class NativeNeedle
{
public:
    NativeNeedle();
    ~NativeNeedle();

    // The clock that moves this needle; must outlive it. Without one the layer is never used.
    void setClock(NeedleClock* clock);

    // Shows the needle over hostView (an NSView*), at scaleFraction now, then moved by the clock
    // from `reading` at every refresh. Cheap when geometry is unchanged. scaleFraction is used only
    // when the needle appears or its shape (place, size, scale) changes; a colour change alone
    // leaves the clock's last drawn position untouched, so it can never move the needle. Returns false if the
    // layer cannot be shown (no clock, no view, no layer), in which case nothing is left on screen
    // and the caller paints the needle.
    bool present(void* hostView, const NeedleGeometry& geometry, float scaleFraction, NeedleReading reading);
    // Hides the layer and stops moving it, but keeps it attached for a quick return.
    void hide();
    // Removes the layer from the host view and the clock, and releases everything.
    void detach();

    // Test support: the layer's model state mapped back through Core Animation's own coordinate
    // conversions into host-view coordinates.
    struct Probe
    {
        bool attached = false;   // the layer is a sublayer of the host view's layer
        bool visible = false;
        bool animating = false;  // any Core Animation animation on the needle
        float scaleFraction = 0.0f;  // the reading the shown tip was drawn for
        juce::Rectangle<float> clip;
        juce::Point<float> pivot, tip;
        float lineWidth = 0.0f, dotDiameter = 0.0f, contentsScale = 0.0f;
        juce::Colour needleColour, dotColour;
        bool masked = false;     // a cover is left out of the layer
    };
    Probe probe() const;
    // Test support: whether the layer shows its content at viewPoint (host-view coordinates):
    // inside its clip and not left out by its mask, tested on the mask's own path through Core
    // Animation's coordinate conversions.
    bool showsAt(juce::Point<float> viewPoint) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE(NativeNeedle)
};
} // namespace bqst::vu
