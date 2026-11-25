// Core Animation presentation of the VU needle and the display-link clock that moves it (see
// BqtVuNeedleLayer.h). Compiled with ARC.

#include "BqtVuNeedleLayer.h"

#if BQST_VU_NATIVE_NEEDLE

#if ! __has_feature(objc_arc)
 #error "BqtVuNeedleLayer_mac.mm must be compiled with -fobjc-arc"
#endif

#import <AppKit/AppKit.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>

#include <array>
#include <mutex>
#include <thread>
#include <time.h>

// CVDisplayLink is deprecated from macOS 15 in favour of CADisplayLink, which needs an Objective-C
// target class. It still works everywhere from the 10.13 deployment target up, calls a plain C
// function on its own thread, and its stop waits for a callback in progress.
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wdeprecated-declarations")

namespace bqst::vu
{
namespace
{
CGColorRef createColour(juce::Colour colour)
{
    const CGFloat components[] { colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue(), colour.getFloatAlpha() };
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGColorRef result = CGColorCreate(space, components);
    CGColorSpaceRelease(space);
    return result;
}

// Same construction as juce::Graphics::drawLine: a butt-ended stroke from pivot to tip, in the
// needle layer's space (the clip's top left is the origin).
CGPathRef createNeedlePath(const NeedleGeometry& geometry, float scaleFraction)
{
    const auto origin = geometry.clip.getPosition();
    const auto pivot = geometry.pivotInView() - origin;
    const auto tip = geometry.tipInView(scaleFraction) - origin;
    CGMutablePathRef path = CGPathCreateMutable();
    CGPathMoveToPoint(path, nullptr, pivot.x, pivot.y);
    CGPathAddLineToPoint(path, nullptr, tip.x, tip.y);
    return path;
}

// No implicit animation for anything this code changes: every change is applied exactly.
NSDictionary<NSString*, id<CAAction>>* noActions()
{
    static NSDictionary* actions = @{ @"position": NSNull.null, @"bounds": NSNull.null, @"frame": NSNull.null,
                                      @"hidden": NSNull.null, @"path": NSNull.null, @"contentsScale": NSNull.null,
                                      @"strokeColor": NSNull.null, @"fillColor": NSNull.null, @"lineWidth": NSNull.null,
                                      @"geometryFlipped": NSNull.null, @"sublayers": NSNull.null,
                                      @"onOrderIn": NSNull.null, @"onOrderOut": NSNull.null };
    return actions;
}

double threadCpuMs()
{
    return static_cast<double>(clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID)) * 1.0e-6;
}

// One needle the clock moves. Only touched with ClockShared::mutex held.
struct NeedleSlot
{
    const void* owner = nullptr;
    bool active = false;
    CAShapeLayer* needle = nil;
    NeedleGeometry geometry;
    NeedleReading reading;
    float scaleFraction = 0.0f;
};

// Everything the display-link thread touches. The clock's message-thread side owns it and frees it
// only after the link is stopped and released.
struct ClockShared
{
    std::mutex mutex;
    std::array<NeedleSlot, 4> slots;
    std::atomic<bool> running { false };
    std::atomic<int> inTick { 0 };
    std::atomic<bool> restartGap { true };
    std::atomic<uint64_t> ticks { 0 };
    std::atomic<double> lastPresentMs { 0.0 };
    bqt::FrameLogNeedle stats;
    double previousPresentMs = 0.0;  // clock thread only

    void tick(const CVTimeStamp* outputTime)
    {
        const auto cpuStart = threadCpuMs();
        ticks.fetch_add(1);
        if (outputTime != nullptr && outputTime->videoRefreshPeriod > 0
            && (outputTime->flags & kCVTimeStampVideoRefreshPeriodValid) != 0)
            stats.refreshHz.store(static_cast<double>(outputTime->videoTimeScale) / static_cast<double>(outputTime->videoRefreshPeriod),
                                  std::memory_order_relaxed);

        // Never wait for the message thread: if it is changing a needle right now, skip this
        // refresh (the gap statistics show it).
        std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
        if (! lock.owns_lock())
            return;

        auto moved = false;
        [CATransaction begin];
        [CATransaction setDisableActions: YES];
        for (auto& slot : slots)
        {
            if (! slot.active || slot.needle == nil || ! slot.reading)
                continue;
            slot.scaleFraction = slot.reading();
            CGPathRef path = createNeedlePath(slot.geometry, slot.scaleFraction);
            slot.needle.path = path;
            CGPathRelease(path);
            moved = true;
        }
        lock.unlock();
        [CATransaction commit];

        if (! moved)
            return;
        const auto now = bqt::FrameLog::nowMs();
        if (restartGap.exchange(false))
            previousPresentMs = 0.0;
        const auto gap = previousPresentMs > 0.0 ? now - previousPresentMs : 0.0;
        previousPresentMs = now;
        lastPresentMs.store(now);
        stats.recordPresent(gap, threadCpuMs() - cpuStart);
    }
};

CVReturn displayLinkTick(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp* outputTime,
                         CVOptionFlags, CVOptionFlags*, void* context)
{
    auto& shared = *static_cast<ClockShared*>(context);
    // Counted before running is checked, so stop() can wait out a tick that saw running == true.
    shared.inTick.fetch_add(1);
    if (shared.running.load())
    {
        @autoreleasepool
        {
            shared.tick(outputTime);
        }
    }
    shared.inTick.fetch_sub(1);
    return kCVReturnSuccess;
}
} // namespace

//==============================================================================
struct NeedleClock::Impl
{
    std::unique_ptr<ClockShared> shared = std::make_unique<ClockShared>();
    CVDisplayLinkRef link = nullptr;
    CGDirectDisplayID display = 0;
    double screenMaxFps = 0.0;
    bool stopped = false; // message thread only; public stop is final

    ~Impl()
    {
        stop();
        if (link != nullptr)
            CVDisplayLinkRelease(link);
        link = nullptr;
        // Only now, with the link stopped and released, is the shared state let go of.
        shared.reset();
    }

    void start()
    {
        if (stopped || link == nullptr || shared->running.load())
            return;
        shared->restartGap.store(true);
        shared->running.store(true);
        if (CVDisplayLinkStart(link) != kCVReturnSuccess)
            shared->running.store(false);
    }

    void stop()
    {
        if (link == nullptr)
            return;
        shared->running.store(false);
        if (CVDisplayLinkIsRunning(link))
            CVDisplayLinkStop(link);
        // CVDisplayLinkStop already waits for a callback in progress; this makes it explicit.
        while (shared->inTick.load() != 0)
            std::this_thread::yield();
    }

    bool anyActive()
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        for (auto& slot : shared->slots)
            if (slot.active)
                return true;
        return false;
    }

    void updateRunning()
    {
        if (anyActive())
            start();
        else
            stop();
    }

    void bind(CGDirectDisplayID newDisplay, double newMaxFps)
    {
        shared->stats.screenMaxFps.store(newMaxFps, std::memory_order_relaxed);
        if (link != nullptr && newDisplay == display && newMaxFps == screenMaxFps)
            return;

        const auto wasRunning = shared->running.load();
        stop();
        if (link == nullptr)
        {
            if (CVDisplayLinkCreateWithCGDisplay(newDisplay, &link) != kCVReturnSuccess || link == nullptr)
            {
                link = nullptr;
                return;
            }
            CVDisplayLinkSetOutputCallback(link, displayLinkTick, shared.get());
        }
        else
        {
            // Also re-applied for the same display when its refresh rate changed.
            CVDisplayLinkSetCurrentCGDisplay(link, newDisplay);
        }
        display = newDisplay;
        screenMaxFps = newMaxFps;
        if (wasRunning)
            start();
    }
};

NeedleClock::NeedleClock() : impl(std::make_unique<Impl>()) {}
NeedleClock::~NeedleClock() = default;

void NeedleClock::follow(void* hostView)
{
    NSView* view = (__bridge NSView*) hostView;
    NSScreen* screen = view != nil && view.window != nil ? view.window.screen : nil;
    if (screen == nil)
        screen = NSScreen.mainScreen;
    if (screen == nil)
        return;
    NSNumber* number = screen.deviceDescription[@"NSScreenNumber"];
    if (number == nil)
        return;
    double maxFps = 0.0;
    if (@available(macOS 12.0, *))
        maxFps = static_cast<double>(screen.maximumFramesPerSecond);
    impl->bind(static_cast<CGDirectDisplayID>(number.unsignedIntValue), maxFps);
}

void NeedleClock::stop()
{
    impl->stopped = true;
    impl->stop();
}

bqt::FrameLogNeedle& NeedleClock::frameLogStats()
{
    return impl->shared->stats;
}

NeedleClock::Probe NeedleClock::probe() const
{
    Probe result;
    const auto& shared = *impl->shared;
    result.running = shared.running.load() && impl->link != nullptr && CVDisplayLinkIsRunning(impl->link);
    result.display = impl->display;
    result.refreshHz = shared.stats.refreshHz.load();
    result.ticks = shared.ticks.load();
    result.presents = shared.stats.presents.load();
    result.lastPresentMs = shared.lastPresentMs.load();
    return result;
}

//==============================================================================
struct NativeNeedle::Impl
{
    NeedleClock* clock = nullptr;
    __weak NSView* host = nil;
    CALayer* container = nil;   // clips to the meter and sets a top-left, y-down space
    CAShapeLayer* needle = nil;
    CAShapeLayer* dot = nil;
    NeedleGeometry shown;
    CGFloat shownScale = 0;
    bool hasShown = false;
    int slotIndex = -1;

    ClockShared* shared() const { return clock != nullptr ? clock->impl->shared.get() : nullptr; }

    void create()
    {
        container = [CALayer layer];
        container.masksToBounds = YES;
        container.anchorPoint = CGPointZero;
        container.actions = noActions();
        // Never an accessibility element or a hit target: layers do not take events, and the
        // JUCE view under it keeps all input.
        needle = [CAShapeLayer layer];
        needle.anchorPoint = CGPointZero;
        needle.fillColor = nil;
        needle.lineCap = kCALineCapButt;
        needle.actions = noActions();
        dot = [CAShapeLayer layer];
        dot.anchorPoint = CGPointZero;
        dot.strokeColor = nil;
        dot.actions = noActions();
        [container addSublayer: needle];
        [container addSublayer: dot];   // the dot sits over the needle, as when painted
    }

    // With the clock's lock held.
    NeedleSlot* slot(ClockShared& s, bool claim)
    {
        if (slotIndex >= 0)
            return &s.slots[static_cast<size_t>(slotIndex)];
        if (! claim)
            return nullptr;
        for (size_t i = 0; i < s.slots.size(); ++i)
        {
            if (s.slots[i].owner == nullptr)
            {
                s.slots[i] = {};
                s.slots[i].owner = this;
                slotIndex = static_cast<int>(i);
                return &s.slots[i];
            }
        }
        return nullptr;
    }

    void deactivate()
    {
        if (auto* s = shared())
        {
            {
                std::lock_guard<std::mutex> lock(s->mutex);
                if (auto* entry = slot(*s, false))
                    entry->active = false;
            }
            clock->impl->updateRunning();
        }
    }

    void release()
    {
        if (auto* s = shared())
        {
            NeedleReading oldReading;
            {
                std::lock_guard<std::mutex> lock(s->mutex);
                if (auto* entry = slot(*s, false))
                {
                    oldReading = std::move(entry->reading);
                    *entry = {};
                }
                slotIndex = -1;
            }
            clock->impl->updateRunning();
        }
        slotIndex = -1;
    }

    void detach()
    {
        release();
        if (container == nil)
            return;
        // Committed at once: the host view may be about to go.
        [CATransaction begin];
        [CATransaction setDisableActions: YES];
        [container removeFromSuperlayer];
        [CATransaction commit];
        container = nil;
        needle = nil;
        dot = nil;
        host = nil;
        hasShown = false;
    }
};

NativeNeedle::NativeNeedle() : impl(std::make_unique<Impl>()) {}

NativeNeedle::~NativeNeedle()
{
    detach();
}

void NativeNeedle::setClock(NeedleClock* newClock)
{
    if (impl->clock == newClock)
        return;
    detach();
    impl->clock = newClock;
}

bool NativeNeedle::present(void* hostView, const NeedleGeometry& geometry, float scaleFraction, NeedleReading reading)
{
    NSView* view = (__bridge NSView*) hostView;
    auto* shared = impl->shared();
    if (shared == nullptr || view == nil || view.layer == nil || geometry.clip.isEmpty() || ! reading)
    {
        detach();
        return false;
    }

    if (impl->host != view)
        impl->detach();

    if (impl->container == nil)
    {
        impl->create();
        impl->host = view;
    }

    // These changes go into the message thread's own Core Animation commit, the one that also
    // carries JUCE's repaint, so the layer and the painted needle swap in the same frame.
    CALayer* parent = view.layer;
    if (impl->container.superlayer != parent)
    {
        [impl->container removeFromSuperlayer];
        [parent addSublayer: impl->container];
        impl->hasShown = false;
    }

    // The JUCE view is flipped (y down), its layer may or may not be: ask AppKit how the view's
    // space maps to the layer's, and flip the container to match so everything inside it is in
    // the view's own top-left, y-down units.
    const auto& clip = geometry.clip;
    const auto layerRect = [view convertRectToLayer: NSMakeRect(clip.getX(), clip.getY(), clip.getWidth(), clip.getHeight())];
    const auto p0 = [view convertPointToLayer: NSMakePoint(0, 0)];
    const auto p1 = [view convertPointToLayer: NSMakePoint(0, 1)];
    const auto layerIsYDown = p1.y > p0.y;
    const auto windowScale = view.window != nil ? view.window.backingScaleFactor : parent.contentsScale;
    const auto scale = windowScale > 0 ? windowScale : 1.0;

    const auto geometryChanged = ! impl->hasShown || impl->shown != geometry || impl->shownScale != scale
                              || ! CGRectEqualToRect(impl->container.frame, layerRect);
    if (geometryChanged)
    {
        auto* container = impl->container;
        container.frame = layerRect;
        container.geometryFlipped = ! layerIsYDown;
        container.contentsScale = scale;

        const auto bounds = CGRectMake(0, 0, layerRect.size.width, layerRect.size.height);
        impl->needle.frame = bounds;
        impl->needle.contentsScale = scale;
        impl->needle.lineWidth = geometry.lineWidth;
        CGColorRef needleColour = createColour(geometry.needleColour);
        impl->needle.strokeColor = needleColour;
        CGColorRelease(needleColour);

        impl->dot.frame = bounds;
        impl->dot.contentsScale = scale;
        const auto pivot = geometry.pivotInView() - clip.getPosition();
        const auto r = geometry.dotDiameter * 0.5f;
        CGPathRef dotPath = CGPathCreateWithEllipseInRect(CGRectMake(pivot.x - r, pivot.y - r, 2 * r, 2 * r), nullptr);
        impl->dot.path = dotPath;
        CGPathRelease(dotPath);
        CGColorRef dotColour = createColour(geometry.dotColour);
        impl->dot.fillColor = dotColour;
        CGColorRelease(dotColour);
    }

    const auto becomingVisible = impl->container.hidden || ! impl->hasShown;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        auto* entry = impl->slot(*shared, true);
        if (entry == nullptr)
        {
            // Unreachable with two meters per clock; paint rather than share a slot.
            impl->container.hidden = YES;
            return false;
        }
        const auto wasActive = entry->active;
        if (geometryChanged || ! wasActive || becomingVisible)
        {
            entry->needle = impl->needle;
            entry->geometry = geometry;
            entry->reading = std::move(reading);
            // Placed now as well, so the layer is right in the very frame it replaces the painted
            // needle (or follows a resize); from here on the clock moves it.
            entry->scaleFraction = scaleFraction;
            CGPathRef path = createNeedlePath(geometry, scaleFraction);
            impl->needle.path = path;
            CGPathRelease(path);
        }
        entry->active = true;
    }

    impl->container.hidden = NO;
    impl->shown = geometry;
    impl->shownScale = scale;
    impl->hasShown = true;

    impl->clock->follow(hostView);
    impl->clock->impl->updateRunning();
    if (! impl->clock->impl->shared->running.load())
    {
        // No display link for this screen, or it would not start: a needle nothing moves must
        // not stay on screen, so paint it instead.
        impl->deactivate();
        impl->container.hidden = YES;
        return false;
    }
    return true;
}

void NativeNeedle::hide()
{
    impl->deactivate();
    if (impl->container == nil || impl->container.hidden)
        return;
    impl->container.hidden = YES;
}

void NativeNeedle::detach()
{
    impl->detach();
}

NativeNeedle::Probe NativeNeedle::probe() const
{
    Probe result;
    NSView* view = impl->host;
    if (impl->container == nil || view == nil || view.layer == nil)
        return result;

    CALayer* parent = view.layer;
    result.attached = impl->container.superlayer == parent;
    result.visible = result.attached && ! impl->container.hidden;
    result.animating = impl->needle.animationKeys.count > 0 || impl->container.animationKeys.count > 0;

    const auto toView = [&](CGPoint p, CALayer* from)
    {
        const auto inParent = [parent convertPoint: p fromLayer: from];
        const auto inView = [view convertPointFromLayer: inParent];
        return juce::Point<float>(static_cast<float>(inView.x), static_cast<float>(inView.y));
    };

    const auto bounds = impl->container.bounds;
    const auto a = toView(bounds.origin, impl->container);
    const auto b = toView(CGPointMake(CGRectGetMaxX(bounds), CGRectGetMaxY(bounds)), impl->container);
    result.clip = juce::Rectangle<float>(a, b);

    if (impl->dot.path != nullptr)
    {
        const auto box = CGPathGetPathBoundingBox(impl->dot.path);
        result.pivot = toView(CGPointMake(CGRectGetMidX(box), CGRectGetMidY(box)), impl->dot);
        result.dotDiameter = static_cast<float>(box.size.width);
    }

    // The clock replaces the path under its lock; read it and the reading it was drawn for together.
    CGPathRef path = nullptr;
    if (auto* shared = impl->shared())
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (auto* entry = impl->slot(*shared, false))
            result.scaleFraction = entry->scaleFraction;
        path = CGPathRetain(impl->needle.path);
    }
    if (path != nullptr)
    {
        result.tip = toView(CGPathGetCurrentPoint(path), impl->needle);
        CGPathRelease(path);
    }

    result.lineWidth = static_cast<float>(impl->needle.lineWidth);
    result.contentsScale = static_cast<float>(impl->needle.contentsScale);
    return result;
}
} // namespace bqst::vu

JUCE_END_IGNORE_WARNINGS_GCC_LIKE

#endif
