// Core Animation presentation of the VU needle (see BqtVuNeedleLayer.h). Compiled with ARC.

#include "BqtVuNeedleLayer.h"

#if BQST_VU_NATIVE_NEEDLE

#if ! __has_feature(objc_arc)
 #error "BqtVuNeedleLayer_mac.mm must be compiled with -fobjc-arc"
#endif

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

namespace bqst::vu
{
namespace
{
NSString* const animationKey = @"bqstNeedleMove";

CGColorRef createColour(juce::Colour colour)
{
    const CGFloat components[] { colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue(), colour.getFloatAlpha() };
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGColorRef result = CGColorCreate(space, components);
    CGColorSpaceRelease(space);
    return result;
}

// Same construction as juce::Graphics::drawLine: a butt-ended stroke from pivot to tip.
CGPathRef createNeedlePath(CGPoint pivot, CGPoint tip)
{
    CGMutablePathRef path = CGPathCreateMutable();
    CGPathMoveToPoint(path, nullptr, pivot.x, pivot.y);
    CGPathAddLineToPoint(path, nullptr, tip.x, tip.y);
    return path;
}

// Stops every implicit animation for the property changes inside it; the only motion is the
// explicit needle animation.
struct ScopedQuietTransaction
{
    ScopedQuietTransaction()
    {
        [CATransaction begin];
        [CATransaction setDisableActions: YES];
    }
    ~ScopedQuietTransaction() { [CATransaction commit]; }
};
} // namespace

struct NativeNeedle::Impl
{
    __weak NSView* host = nil;
    CALayer* container = nil;   // clips to the meter and sets a top-left, y-down space
    CAShapeLayer* needle = nil;
    CAShapeLayer* dot = nil;
    NeedlePicture shown;
    bool hasShown = false;

    void create()
    {
        container = [CALayer layer];
        container.masksToBounds = YES;
        container.anchorPoint = CGPointZero;
        // Never an accessibility element or a hit target: layers do not take events, and the
        // JUCE view under it keeps all input.
        needle = [CAShapeLayer layer];
        needle.anchorPoint = CGPointZero;
        needle.fillColor = nil;
        needle.lineCap = kCALineCapButt;
        dot = [CAShapeLayer layer];
        dot.anchorPoint = CGPointZero;
        dot.strokeColor = nil;
        [container addSublayer: needle];
        [container addSublayer: dot];   // the dot sits over the needle, as when painted
    }

    void detach()
    {
        if (container == nil)
            return;
        ScopedQuietTransaction quiet;
        [needle removeAllAnimations];
        [container removeFromSuperlayer];
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

bool NativeNeedle::present(void* hostView, const NeedlePicture& picture, double animateSeconds)
{
    NSView* view = (__bridge NSView*) hostView;
    if (view == nil || view.layer == nil || picture.clip.isEmpty())
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

    ScopedQuietTransaction quiet;

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
    const auto layerRect = [view convertRectToLayer: NSMakeRect(picture.clip.getX(), picture.clip.getY(),
                                                                 picture.clip.getWidth(), picture.clip.getHeight())];
    const auto p0 = [view convertPointToLayer: NSMakePoint(0, 0)];
    const auto p1 = [view convertPointToLayer: NSMakePoint(0, 1)];
    const auto layerIsYDown = p1.y > p0.y;

    const auto scale = view.window != nil ? view.window.backingScaleFactor : parent.contentsScale;
    auto drawn = picture;
    drawn.contentsScale = static_cast<float>(scale > 0 ? scale : 1.0);

    const auto local = [&picture](juce::Point<float> p)
    {
        return CGPointMake(p.x - picture.clip.getX(), p.y - picture.clip.getY());
    };
    CGPathRef newPath = createNeedlePath(local(picture.pivot), local(picture.tip));

    const auto unchanged = impl->hasShown && ! impl->container.hidden && sameNeedleFrame(impl->shown, drawn)
                        && juce::approximatelyEqual(impl->shown.contentsScale, drawn.contentsScale)
                        && impl->shown.tip == drawn.tip
                        && CGRectEqualToRect(impl->container.frame, layerRect);
    if (unchanged)
    {
        // Leave a running move alone: it already ends here.
        CGPathRelease(newPath);
        return true;
    }

    const auto animate = animateSeconds > 0.0 && impl->hasShown && sameNeedleFrame(impl->shown, drawn)
                      && juce::approximatelyEqual(impl->shown.contentsScale, drawn.contentsScale)
                      && ! impl->container.hidden;

    if (! animate)
    {
        auto* container = impl->container;
        container.frame = layerRect;
        container.geometryFlipped = ! layerIsYDown;
        container.contentsScale = drawn.contentsScale;

        const auto bounds = CGRectMake(0, 0, layerRect.size.width, layerRect.size.height);
        impl->needle.frame = bounds;
        impl->needle.contentsScale = drawn.contentsScale;
        impl->needle.lineWidth = picture.lineWidth;
        CGColorRef needleColour = createColour(picture.needleColour);
        impl->needle.strokeColor = needleColour;
        CGColorRelease(needleColour);

        impl->dot.frame = bounds;
        impl->dot.contentsScale = drawn.contentsScale;
        const auto pivot = local(picture.pivot);
        const auto r = picture.dotDiameter * 0.5f;
        CGPathRef dotPath = CGPathCreateWithEllipseInRect(CGRectMake(pivot.x - r, pivot.y - r, 2 * r, 2 * r), nullptr);
        impl->dot.path = dotPath;
        CGPathRelease(dotPath);
        CGColorRef dotColour = createColour(picture.dotColour);
        impl->dot.fillColor = dotColour;
        CGColorRelease(dotColour);

        [impl->needle removeAnimationForKey: animationKey];
        impl->needle.path = newPath;
    }
    else
    {
        // Start from wherever the needle is on screen right now (mid-way through the previous
        // move, if it is still running) so moves join without a jump.
        CAShapeLayer* onScreen = impl->needle.presentationLayer;
        CGPathRef from = onScreen != nil && onScreen.path != nullptr ? onScreen.path : impl->needle.path;

        CABasicAnimation* move = [CABasicAnimation animationWithKeyPath: @"path"];
        move.fromValue = (__bridge id) from;
        move.toValue = (__bridge id) newPath;
        move.duration = animateSeconds;
        move.timingFunction = [CAMediaTimingFunction functionWithName: kCAMediaTimingFunctionLinear];
        if (@available(macOS 12.0, *))
            move.preferredFrameRateRange = CAFrameRateRangeMake(60.0f, 120.0f, 120.0f);

        impl->needle.path = newPath;
        [impl->needle addAnimation: move forKey: animationKey];
    }

    CGPathRelease(newPath);
    impl->container.hidden = NO;
    impl->shown = drawn;
    impl->hasShown = true;
    return true;
}

void NativeNeedle::hide()
{
    if (impl->container == nil || impl->container.hidden)
        return;
    ScopedQuietTransaction quiet;
    [impl->needle removeAnimationForKey: animationKey];
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
    if (CAAnimation* move = [impl->needle animationForKey: animationKey])
    {
        result.animating = true;
        result.animationSeconds = move.duration;
    }

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
    if (impl->needle.path != nullptr)
        result.tip = toView(CGPathGetCurrentPoint(impl->needle.path), impl->needle);

    result.lineWidth = static_cast<float>(impl->needle.lineWidth);
    result.contentsScale = static_cast<float>(impl->needle.contentsScale);
    return result;
}
} // namespace bqst::vu

#endif
