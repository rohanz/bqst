#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BqtVuNeedleLayer.h"

class BqtAudioProcessor;

namespace bqst::vu
{
// Position on the VU scale (0 = left stop, 1 = right stop) of a needle deflection, where deflection
// is linear amplitude relative to 0 VU (0 = no signal, 1 = 0 VU). Linear in amplitude like a moving
// coil, with full scale at +3 VU: 0 VU sits at about 71% of the arc, -20 VU at about 7%.
float vuDeflectionToScaleFraction(float deflection);
} // namespace bqst::vu

class BqtVuMeter final : public juce::Component
{
public:
    BqtVuMeter(BqtAudioProcessor& processor, int sideIndex);
    ~BqtVuMeter() override;
    void paint(juce::Graphics& g) override;
    // The editor's clock that moves the native needle (see BqtVuNeedleLayer.h); must outlive the
    // meter. Without one the needle is always painted.
    void setNeedleClock(bqst::vu::NeedleClock* clock);
    // Draws the audio-side deflection directly.
    bool updateLevel();
    // Called once per display frame after updateLevel: shows the needle in the native layer when
    // it can (see BqtVuNeedleLayer.h), otherwise paints it. While native, the needle is moved by
    // the needle clock at the screen's own rate, not here. editorAllowsNative is false while the
    // editor paints anything over the meters that this meter cannot see (the bypassed look).
    void syncNeedlePresentation(bool editorAllowsNative);

    // For tests: the needle as painted at the current reading (pivot to tip, meter coordinates),
    // whether the native layer is presenting it, and the layer's own state.
    juce::Line<float> currentNeedleLine() const { return needleLine(displayedLevel); }
    // For tests: the painted needle at a position on the scale (0..1), meter coordinates.
    juce::Line<float> needleLineAt(float scaleFraction) const { return needleLine(scaleFraction); }
    // For tests and snapshots: puts the painted needle at a deflection (1 = 0 VU) without audio.
    void showDeflectionForTest(float deflection) { displayedLevel = bqst::vu::vuDeflectionToScaleFraction(deflection); }
    bool isNeedleNative() const { return needleIsNative; }
    bqst::vu::NativeNeedle::Probe nativeNeedleProbe() const { return nativeNeedle.probe(); }
    // The area the needle and pivot dot can ever occupy, in meter coordinates.
    juce::Rectangle<int> needleSweepArea() const;
    // The part of the meter's bounds it paints (its housing); the rest is transparent.
    juce::Rectangle<int> paintedArea() const;

private:
    // Re-presents the needle at once when the meter or any parent moves, resizes, changes peer or
    // visibility, so the layer never trails the painted dial during a resize or a display change.
    class Watcher final : public juce::ComponentMovementWatcher
    {
    public:
        explicit Watcher(BqtVuMeter& m) : juce::ComponentMovementWatcher(&m), meter(m) {}
        // Every move or resize of the meter or a parent, including transform changes (the editor
        // scales its contents with one), which the base class does not pass on.
        void componentMovedOrResized(juce::Component& c, bool wasMoved, bool wasResized) override
        {
            juce::ComponentMovementWatcher::componentMovedOrResized(c, wasMoved, wasResized);
            meter.syncNeedleGeometry();
        }
        void componentMovedOrResized(bool, bool) override {}
        void componentPeerChanged() override { meter.dropNativeNeedle(); }
        void componentVisibilityChanged() override { meter.syncNeedleGeometry(); }
        using juce::ComponentMovementWatcher::componentVisibilityChanged;

    private:
        BqtVuMeter& meter;
    };

    void rebuildStaticLayer(float layerScale);
    juce::Line<float> needleLine(float level) const;
    void repaintNeedle(float previousLevel);
    void repaintNeedleAt(float level);
    bool presentNeedle();
    void syncNeedleGeometry();
    void dropNativeNeedle();
    void showPaintedNeedle();

    BqtAudioProcessor& audioProcessor;
    int side = 0;
    float displayedLevel = 0.0f;
    float lastPaintedLevel = 0.0f;
    juce::Image staticLayer;
    int staticLayerWidth = 0;
    int staticLayerHeight = 0;
    float staticLayerScale = 0.0f;

    bqst::vu::NeedleClock* needleClock = nullptr;
    bqst::vu::NativeNeedle nativeNeedle;
    bool needleIsNative = false;
    bool editorAllowsNativeNeedle = false;
    Watcher watcher { *this };
};
