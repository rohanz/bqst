#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BqtVuNeedleLayer.h"

class BqtAudioProcessor;

class BqtVuMeter final : public juce::Component
{
public:
    BqtVuMeter(BqtAudioProcessor& processor, int sideIndex);
    ~BqtVuMeter() override;
    void paint(juce::Graphics& g) override;
    void setRenderScale(float newScale);
    // Advances the needle and marks the area it swept for repaint; true if it moved.
    // secondsElapsed is the real time since the previous call. The needle smoothing used to use a
    // fixed per-tick coefficient, which made its speed depend on how punctually the timer fired.
    bool updateLevel(double secondsElapsed);
    // Called once per display frame after updateLevel: shows the needle in the native layer when
    // it can (see BqtVuNeedleLayer.h), otherwise paints it. editorAllowsNative is false while the
    // editor paints anything over the meters that this meter cannot see (the bypassed look).
    void syncNeedlePresentation(bool editorAllowsNative, double secondsElapsed);

    // For tests: the needle as painted at the current reading (pivot to tip, meter coordinates),
    // whether the native layer is presenting it, and the layer's own state.
    juce::Line<float> currentNeedleLine() const { return needleLine(displayedLevel); }
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
    bool presentNeedle(double animateSeconds);
    void syncNeedleGeometry();
    void dropNativeNeedle();
    void showPaintedNeedle();

    BqtAudioProcessor& audioProcessor;
    int side = 0;
    float targetLevel = 0.0f;
    float displayedLevel = 0.0f;
    juce::Image staticLayer;
    int staticLayerWidth = 0;
    int staticLayerHeight = 0;
    float staticLayerScale = 0.0f;
    float renderScale = 1.0f;

    bqst::vu::NativeNeedle nativeNeedle;
    bool needleIsNative = false;
    bool editorAllowsNativeNeedle = false;
    float nativeShownLevel = -1.0f;
    double typicalUpdateInterval = 1.0 / 60.0;
    Watcher watcher { *this };
};
