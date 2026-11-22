#pragma once

// The VU needle can be presented in a small Core Animation layer over each meter instead of being
// painted with the rest of the editor. Some hosts do not show every frame a plugin paints through
// JUCE's CoreGraphics path (seen in Ableton Live on a 120 Hz Retina screen), which makes a painted
// needle move unevenly. A layer is composited by the system itself, and each new needle position
// is handed over as a short animation, so the system's render server moves the needle at the
// display's own rate even when the host shows our painted frames late or not at all.
//
// Only the needle and its pivot dot live in the layer; the dial, scale and everything else are
// painted as before. The painted needle stays as the fallback wherever the layer is unavailable:
// other platforms, offscreen snapshots, a host view without a layer, while anything BQST draws
// covers the needle's sweep (About card, value bubbles, the bypassed look), or with the switch off.
//
// Compile-time switch: configure with -DBQST_VU_NATIVE_NEEDLE=OFF (CMake) to always paint the needle.

#include <juce_gui_basics/juce_gui_basics.h>

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

// What the layer draws, in the host view's own coordinates (points, origin top left, y down,
// which is also the JUCE peer's coordinate space).
struct NeedlePicture
{
    juce::Rectangle<float> clip;  // the meter's bounds: the needle is clipped here, as when painted
    juce::Point<float> pivot, tip;
    float lineWidth = 0.0f;
    float dotDiameter = 0.0f;
    float contentsScale = 1.0f;  // filled in by the layer from the host window's backing scale
    juce::Colour needleColour, dotColour;
};

// Whether two pictures share the same frame, so a move between them may be animated. A change of
// size, scale or position is applied at once instead, so the needle never lags the dial.
inline bool sameNeedleFrame(const NeedlePicture& a, const NeedlePicture& b)
{
    constexpr auto eps = 1.0e-3f;
    const auto near = [](float x, float y) { return std::abs(x - y) <= eps; };
    return near(a.clip.getX(), b.clip.getX()) && near(a.clip.getY(), b.clip.getY())
        && near(a.clip.getWidth(), b.clip.getWidth()) && near(a.clip.getHeight(), b.clip.getHeight())
        && near(a.pivot.x, b.pivot.x) && near(a.pivot.y, b.pivot.y)
        && near(a.lineWidth, b.lineWidth) && near(a.dotDiameter, b.dotDiameter)
        && a.needleColour == b.needleColour && a.dotColour == b.dotColour;
}

// True if anything painted after `component` (a later sibling of it or of any of its ancestors,
// up to its top-level component) is visible and overlaps `area` (in component's coordinates).
// Such content would be hidden under a native layer, so the needle must be painted instead.
// A parent's paintOverChildren is not visible from here; callers pass that state in themselves
// (the editor reports its bypassed look). paintedAreaOf, if given, narrows a sibling to the part
// of its bounds it actually paints (in its own coordinates); by default all of it counts.
inline bool isAreaCoveredFromAbove(const juce::Component& component, juce::Rectangle<int> area,
                                   const std::function<juce::Rectangle<int>(const juce::Component&)>& paintedAreaOf = {})
{
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
            const auto painted = paintedAreaOf ? paintedAreaOf(*sibling) : sibling->getLocalBounds();
            if (painted.intersects(sibling->getLocalArea(&component, area)))
                return true;
        }
    }
    return false;
}

enum class NeedleMode
{
    painted,
    native
};

struct NeedleConditions
{
    bool compiledIn = nativeNeedleCompiledIn;
    bool editorAllows = false;   // false while the editor dims the rack (bypassed look)
    bool showing = false;        // the meter is on screen in a window
    bool hasHostView = false;    // there is a native peer view to attach to
    bool coveredFromAbove = false;
};

inline NeedleMode chooseNeedleMode(const NeedleConditions& c)
{
    return c.compiledIn && c.editorAllows && c.showing && c.hasHostView && ! c.coveredFromAbove
         ? NeedleMode::native
         : NeedleMode::painted;
}

// How long each handed-over move takes: about one and a half update intervals, so consecutive
// moves join up even when updates arrive a little late, without adding more than a frame or two
// of lag. Bounded so a stalled host cannot stretch the needle into slow motion.
inline double needleAnimationSeconds(double typicalUpdateInterval)
{
    return juce::jlimit(1.0 / 240.0, 1.0 / 30.0, typicalUpdateInterval * 1.5);
}

// The needle and pivot dot in a Core Animation layer added on top of the JUCE peer view's own
// layer. Plain CALayer instances only: no custom Objective-C classes, delegates, observers, timers
// or blocks, so nothing of it can call back into the plugin after detach. Main thread only.
class NativeNeedle
{
public:
    NativeNeedle();
    ~NativeNeedle();

    // Shows `picture` over hostView (an NSView*). With animateSeconds > 0 the move from the
    // currently shown needle is handed to the system as an animation of that length; otherwise
    // it is applied at once. Returns false if the layer cannot be shown (no view, no layer), in
    // which case nothing is left on screen and the caller paints the needle.
    bool present(void* hostView, const NeedlePicture& picture, double animateSeconds);
    // Hides the layer but keeps it attached for a quick return.
    void hide();
    // Removes the layer from the host view and releases everything.
    void detach();

    // Test support: the layer's model state mapped back through Core Animation's own coordinate
    // conversions into host-view coordinates.
    struct Probe
    {
        bool attached = false;   // the layer is a sublayer of the host view's layer
        bool visible = false;
        bool animating = false;
        double animationSeconds = 0.0;
        juce::Rectangle<float> clip;
        juce::Point<float> pivot, tip;
        float lineWidth = 0.0f, dotDiameter = 0.0f, contentsScale = 0.0f;
    };
    Probe probe() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE(NativeNeedle)
};
} // namespace bqst::vu
