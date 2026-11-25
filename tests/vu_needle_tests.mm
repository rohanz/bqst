// Native VU needle checks (macOS, no host needed).
//
// The editor is hosted the way a plugin host hosts it (added to an NSView), but in a window that
// is never ordered on screen: nothing appears, nothing takes focus, no input is sent. The
// presentation proof alone orders a small window in (floating, without activating the app or
// taking focus, ignoring the mouse) and captures it.
//
// Checks:
//  - fallback selection rules and the "covered from above" test, on plain components;
//  - an editor that is not on the desktop (as in BqstEditorSnapshot) paints its needle;
//  - hosted, the layer is attached and its needle, pivot dot and clip land exactly where the
//    painted needle would, at several editor sizes, mapped back through Core Animation;
//  - a moving reading is presented exactly at the painted position without animation;
//  - anything over the needle's sweep (a component, the About card) and the bypassed look switch
//    to the painted needle, and back;
//  - removing the editor from its view, and deleting the editor, leave no layer behind;
//  - the needle clock moves the layer on its own thread, exactly along the painted needle's path,
//    and stops while no needle is native;
//  - on screen (needs Screen Recording permission, else skipped): the window server shows a new
//    needle picture at every refresh while the message thread is stalled or throttled to 30/s,
//    a painted-needle control freezes, and a window moved to another screen rebinds the clock;
//  - teardown with the clock ticking and audio running: rapid open/close, peer changes, editor
//    closed, plugin destroyed. Run under AddressSanitizer/ThreadSanitizer for the new code.

#include "../src/BqtEditorSize.h"
#include "../src/BqtEditorStyle.h"
#include "../src/BqtVuMeter.h"
#include "../src/BqtVuNeedleLayer.h"
#include "../src/PluginEditor.h"
#include "../src/PluginProcessor.h"

#import <AppKit/AppKit.h>
#import <CoreMedia/CoreMedia.h>
#import <QuartzCore/QuartzCore.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <atomic>
#include <iostream>
#include <mach/mach_time.h>
#include <thread>
#include <typeinfo>

// Receives the window server's captures of the display area under the test window's meter. A frame counts as a
// new picture only if the window server reports new content and its pixels differ from the
// previous picture; gaps are measured between the display times the window server reports.
API_AVAILABLE(macos(12.3))
@interface BqstNeedleCaptureOutput : NSObject <SCStreamOutput>
{
@public
    std::atomic<bool> measuring;
    std::atomic<int> pictures;
    std::atomic<double> maxGapMs;
    double lastDisplayMs;
    uint64_t lastHash;
}
@end

@implementation BqstNeedleCaptureOutput
- (void) stream: (SCStream*) stream didOutputSampleBuffer: (CMSampleBufferRef) sampleBuffer ofType: (SCStreamOutputType) type
{
    juce::ignoreUnused(stream);
    if (type != SCStreamOutputTypeScreen)
        return;
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    if (attachments == nullptr || CFArrayGetCount(attachments) == 0)
        return;
    NSDictionary* info = (__bridge NSDictionary*) CFArrayGetValueAtIndex(attachments, 0);
    NSNumber* status = info[SCStreamFrameInfoStatus];
    if (status == nil || status.integerValue != SCFrameStatusComplete)
        return;
    CVPixelBufferRef pixels = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (pixels == nullptr)
        return;
    CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
    const auto* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(pixels));
    const auto rowBytes = CVPixelBufferGetBytesPerRow(pixels);
    const auto width = CVPixelBufferGetWidth(pixels), height = CVPixelBufferGetHeight(pixels);
    uint64_t hash = 1469598103934665603ull;
    for (size_t y = 0; y < height; ++y)
        for (size_t x = 0; x < width * 4; ++x)
            hash = (hash ^ base[y * rowBytes + x]) * 1099511628211ull;
    CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
    if (hash == lastHash)
        return;
    lastHash = hash;

    NSNumber* displayTime = info[SCStreamFrameInfoDisplayTime];
    static const double ticksToMs = []
    {
        mach_timebase_info_data_t timebase {};
        mach_timebase_info(&timebase);
        return static_cast<double>(timebase.numer) / static_cast<double>(timebase.denom) * 1.0e-6;
    }();
    const auto displayMs = displayTime != nil ? static_cast<double>(displayTime.unsignedLongLongValue) * ticksToMs : 0.0;
    if (! measuring.load())
    {
        lastDisplayMs = displayMs;
        return;
    }
    if (lastDisplayMs > 0.0 && displayMs > lastDisplayMs)
    {
        const auto gap = displayMs - lastDisplayMs;
        if (gap > maxGapMs.load())
            maxGapMs.store(gap);
    }
    lastDisplayMs = displayMs;
    pictures.fetch_add(1);
}
@end

namespace
{
int failures = 0;
int checks = 0;

void check(bool condition, const juce::String& what)
{
    ++checks;
    if (! condition)
    {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

bool near(float a, float b, float tolerance) { return std::abs(a - b) <= tolerance; }
bool near(juce::Point<float> a, juce::Point<float> b, float tolerance) { return a.getDistanceFrom(b) <= tolerance; }
juce::String str(juce::Point<float> p) { return "(" + juce::String(p.x, 3) + ", " + juce::String(p.y, 3) + ")"; }

void collectMeters(juce::Component& root, std::vector<BqtVuMeter*>& out)
{
    for (auto* child : root.getChildren())
    {
        if (auto* meter = dynamic_cast<BqtVuMeter*>(child))
            out.push_back(meter);
        collectMeters(*child, out);
    }
}

void switchAccessibility(BqtAudioProcessor& processor)
{
    // JUCE exposes accessibility handlers only with a native peer. This host window
    // is never ordered on screen, and the hidden editor keeps the pill deterministic.
    NSWindow* window = [[NSWindow alloc] initWithContentRect: NSMakeRect(0, 0, 2000, 2000)
                                                   styleMask: NSWindowStyleMaskBorderless
                                                     backing: NSBackingStoreBuffered defer: NO];
    window.releasedWhenClosed = NO;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->addToDesktop(0, (__bridge void*) window.contentView);
    editor->setVisible(false);
    struct Gestures final : juce::AudioProcessorParameter::Listener
    {
        int begins = 0, ends = 0, depth = 0;
        bool balanced = true;
        void parameterValueChanged(int, float) override {}
        void parameterGestureChanged(int, bool starting) override
        {
            if (starting) { ++begins; balanced = balanced && depth == 0; ++depth; }
            else { ++ends; --depth; balanced = balanced && depth == 0; }
        }
    };
    int switches = 0;
    for (auto* child : editor->getChildren())
    {
        auto* control = dynamic_cast<BqtSwitch*>(child);
        if (control == nullptr) continue;
        ++switches;
        const auto isEq = control->getTitle() == "eq mode";
        auto* parameter = processor.state().getParameter(isEq ? "eqMode" : "satMode");
        const juce::String context = isEq ? "EQ" : "Saturation";
        Gestures gestures;
        parameter->addListener(&gestures);
        const auto agrees = [&]
        {
            const auto selected = control->getSelected();
            check(control->getHalf(selected).getToggleState()
                      && ! control->getHalf(1 - selected).getToggleState(), "exactly selected half is checked");
            check(control->pillBounds() == control->getHalf(selected).getBounds().toFloat(), "pill agrees with selection");
            check(juce::roundToInt(parameter->convertFrom0to1(parameter->getValue())) == selected,
                  "parameter agrees with selection");
        };
        for (int i = 0; i < 2; ++i)
            check(control->getHalf(i).getAccessibilityHandler()->getTitle()
                      == context + (i == 0 ? " left/right" : " mid/side"), "each half has contextual accessible title");
        for (const auto action : { juce::AccessibilityActionType::press, juce::AccessibilityActionType::toggle })
        {
            auto selected = control->getSelected();
            auto begins = gestures.begins;
            auto ends = gestures.ends;
            check(control->getHalf(selected).getAccessibilityHandler()->getActions().invoke(action), "selected action is supported");
            agrees();
            check(gestures.begins == begins && gestures.ends == ends, "reselecting sends no parameter gesture");
            check(control->getHalf(1 - selected).getAccessibilityHandler()->getActions().invoke(action), "unselected action is supported");
            agrees();
            check(control->getSelected() == 1 - selected, "accessibility selects the other half");
            check(gestures.begins == begins + 1 && gestures.ends == ends + 1
                      && gestures.depth == 0 && gestures.balanced, "a user change sends one balanced gesture");
        }
        // Re-applying an unchanged parameter selection repairs independently altered button state.
        control->getHalf(control->getSelected()).setToggleState(false, juce::dontSendNotification);
        control->setSelected(control->getSelected());
        agrees();
        parameter->removeListener(&gestures);
    }
    check(switches == 2, "both mode switches exercised");
    editor->setSize(1103, bqt::editor::heightForWidth(1103));
    const auto bounds = editor->getBounds();
    BqtPresetManager presets(processor.state());
    check(presets.loadPreset(0), "factory preset loads with editor open");
    check(editor->getBounds() == bounds && bqt::editor::restoredWidth(processor.state().state) == 1103,
          "loading a factory preset preserves current window size");
    editor.reset();
    [window close];
}

void selectionRules()
{
    using namespace bqst::vu;
    NeedleConditions ok;
    ok.compiledIn = true;
    ok.hasClock = ok.editorAllows = ok.showing = ok.hasHostView = true;
    check(chooseNeedleMode(ok) == NeedleMode::native, "all conditions met selects the layer");

    auto c = ok; c.compiledIn = false;
    check(chooseNeedleMode(c) == NeedleMode::painted, "switch off paints");
    c = ok; c.hasClock = false;
    check(chooseNeedleMode(c) == NeedleMode::painted, "no needle clock paints");
    c = ok; c.editorAllows = false;
    check(chooseNeedleMode(c) == NeedleMode::painted, "bypassed look paints");
    c = ok; c.showing = false;
    check(chooseNeedleMode(c) == NeedleMode::painted, "not showing paints");
    c = ok; c.hasHostView = false;
    check(chooseNeedleMode(c) == NeedleMode::painted, "no host view (offscreen) paints");
    c = ok; c.coveredFromAbove = true;
    check(chooseNeedleMode(c) == NeedleMode::painted, "covered needle paints");

    // z-order: later siblings (of the component or of any ancestor) cover; earlier ones and
    // hidden ones do not.
    juce::Component top, rack, meter, before, after, overlay;
    top.setBounds(0, 0, 400, 400);
    top.addAndMakeVisible(rack);
    rack.setBounds(10, 10, 300, 300);
    rack.setTransform(juce::AffineTransform::scale(1.5f));
    rack.addAndMakeVisible(before);
    rack.addAndMakeVisible(meter);
    rack.addAndMakeVisible(after);
    meter.setBounds(20, 20, 100, 100);
    before.setBounds(0, 0, 200, 200);
    after.setBounds(150, 150, 20, 20);
    const auto area = juce::Rectangle<int>(10, 10, 50, 50);
    check(! isAreaCoveredFromAbove(meter, area), "earlier or distant siblings do not cover");
    after.setBounds(40, 40, 10, 10);
    check(isAreaCoveredFromAbove(meter, area), "a later sibling over the area covers");
    after.setVisible(false);
    check(! isAreaCoveredFromAbove(meter, area), "a hidden sibling does not cover");
    top.addAndMakeVisible(overlay);
    overlay.setBounds(300, 300, 50, 50);
    check(! isAreaCoveredFromAbove(meter, area), "an ancestor's later sibling away from the area does not cover");
    overlay.setBounds(60, 60, 10, 10);   // rack (10,10) + 1.5 * (20+10..20+60) = 55..130
    check(isAreaCoveredFromAbove(meter, area), "an ancestor's later sibling over the scaled area covers");
    const auto paintsNothing = [](const juce::Component&) { return juce::Rectangle<int>(); };
    check(! isAreaCoveredFromAbove(meter, area, paintsNothing), "a sibling that paints elsewhere in its bounds does not cover");
}

void paintedWhenOffscreen(BqtAudioProcessor& processor)
{
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    std::vector<BqtVuMeter*> meters;
    collectMeters(*editor, meters);
    check(meters.size() == 2, "editor has two meters");
    for (auto* meter : meters)
    {
        meter->syncNeedlePresentation(true);
        check(! meter->isNeedleNative(), "offscreen editor paints its needle");

        // The snapshot shows the needle: the point a third of the way from pivot to tip is the
        // needle's near-black.
        const auto image = meter->createComponentSnapshot(meter->getLocalBounds(), true, 2.0f);
        const auto p = meter->currentNeedleLine().getPointAlongLineProportionally(0.33f) * 2.0f;
        const auto pixel = image.getPixelAt(juce::roundToInt(p.x), juce::roundToInt(p.y));
        check(pixel.getBrightness() < 0.3f, "offscreen snapshot shows the painted needle (brightness "
                                                + juce::String(pixel.getBrightness()) + ")");
    }
}

void slowPaintedFade(BqtAudioProcessor& processor)
{
    processor.setNonRealtime(true);
    processor.prepareToPlay(48000.0, 48);
    BqtVuMeter meter(processor, 0);
    meter.setSize(300, 200);
    juce::AudioBuffer<float> audio(2, 48);
    juce::MidiBuffer midi;
    int repaints = 0;
    for (int block = 0; block < 4000; ++block)
    {
        const auto value = block < 2000 ? 0.05f : 0.05f - (block - 2000) * 0.000001f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 48; ++i)
                audio.setSample(ch, i, value);
        processor.processBlock(audio, midi);
        if (meter.updateLevel())
        {
            if (block >= 2100) ++repaints;
            meter.createComponentSnapshot(meter.getLocalBounds());
        }
    }
    check(repaints > 10, "sub-threshold frame changes accumulate against last painted needle");
    processor.setNonRealtime(false);
    processor.prepareToPlay(48000.0, 512);
}

struct Expected
{
    juce::Rectangle<float> clip;
    juce::Point<float> pivot, tip;
    float scale = 1.0f;
};

// Where the painted needle lands in the peer view, through JUCE's own mapping, for the reading
// the native tip was drawn for (the clock moves it on its own thread between checks).
Expected expectedFor(BqtVuMeter& meter, float scaleFraction)
{
    auto& top = meter.getPeer()->getComponent();
    const auto desktop = top.getDesktopScaleFactor();
    const auto line = meter.needleLineAt(scaleFraction);
    Expected e;
    e.pivot = top.getLocalPoint(&meter, line.getStart()) * desktop;
    e.tip = top.getLocalPoint(&meter, line.getEnd()) * desktop;
    e.clip = juce::Rectangle<float>(top.getLocalPoint(&meter, juce::Point<float>()) * desktop,
                                    top.getLocalPoint(&meter, juce::Point<float>(static_cast<float>(meter.getWidth()),
                                                                                 static_cast<float>(meter.getHeight()))) * desktop);
    e.scale = top.getLocalPoint(&meter, juce::Point<float>(100.0f, 0.0f)).getDistanceFrom(top.getLocalPoint(&meter, juce::Point<float>())) / 100.0f * desktop;
    return e;
}

void checkAligned(BqtVuMeter& meter, NSWindow* window, const juce::String& label)
{
    const auto probe = meter.nativeNeedleProbe();
    const auto e = expectedFor(meter, probe.scaleFraction);
    check(meter.isNeedleNative(), label + ": layer selected");
    check(probe.attached && probe.visible, label + ": layer attached and visible");
    check(near(probe.pivot, e.pivot, 0.01f), label + ": pivot " + str(probe.pivot) + " vs painted " + str(e.pivot));
    check(near(probe.tip, e.tip, 0.01f), label + ": tip " + str(probe.tip) + " vs painted " + str(e.tip));
    check(near(probe.clip.getX(), e.clip.getX(), 0.01f) && near(probe.clip.getY(), e.clip.getY(), 0.01f)
              && near(probe.clip.getWidth(), e.clip.getWidth(), 0.01f) && near(probe.clip.getHeight(), e.clip.getHeight(), 0.01f),
          label + ": clip " + probe.clip.toString() + " vs meter " + e.clip.toString());
    check(meter.getPeer()->getAreaCoveredBy(meter).toFloat().expanded(0.01f).contains(probe.clip),
          label + ": clip inside the meter's area as JUCE reports it");
    check(near(probe.lineWidth, 2.6f * e.scale, 0.001f), label + ": needle width " + juce::String(probe.lineWidth));
    check(near(probe.dotDiameter, 6.5f * e.scale, 0.01f), label + ": dot " + juce::String(probe.dotDiameter));
    check(near(probe.contentsScale, static_cast<float>(window.backingScaleFactor), 0.001f),
          label + ": layer drawn at the window's pixel density " + juce::String(probe.contentsScale));
}

void driveMeters(BqtAudioProcessor& processor, float amplitude)
{
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    for (int block = 0; block < 60; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample(ch, i, amplitude * std::sin(static_cast<float>(i + block * 512) * 0.0573f));
        processor.processBlock(buffer, midi);
    }
}

size_t sublayerCount(NSView* view)
{
    return view.layer != nil ? view.layer.sublayers.count : 0;
}

const bqst::vu::NeedleClock& clockOf(juce::AudioProcessorEditor& editor)
{
    return static_cast<BqtAudioProcessorEditor&>(editor).needleClockForTest();
}

void hosted(BqtAudioProcessor& processor)
{
    // Borderless, never ordered in: a window AppKit lays out but never shows.
    NSWindow* window = [[NSWindow alloc] initWithContentRect: NSMakeRect(0, 0, 2000, 2000)
                                                   styleMask: NSWindowStyleMaskBorderless
                                                     backing: NSBackingStoreBuffered
                                                       defer: NO];
    window.releasedWhenClosed = NO;
    NSView* content = window.contentView;

    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
    editor->setVisible(true);
    editor->addToDesktop(0, (__bridge void*) content);
    check(editor->getPeer() != nullptr, "editor has a peer in the host view");
    if (editor->getPeer() == nullptr)
        return;

    NSView* peerView = (__bridge NSView*) editor->getPeer()->getNativeHandle();
    check(peerView.layer != nil, "JUCE's peer view is layer backed");
    const auto baselineSublayers = sublayerCount(peerView);

    std::vector<BqtVuMeter*> meters;
    collectMeters(*editor, meters);
    if (meters.size() != 2)
        return;

    if (! bqst::vu::nativeNeedleCompiledIn)
    {
        // Switched off at build time: hosted or not, the needle is painted and no layer is added.
        for (auto* meter : meters)
        {
            meter->syncNeedlePresentation(true);
            check(! meter->isNeedleNative(), "switched off: hosted meter paints its needle");
        }
        check(sublayerCount(peerView) == baselineSublayers, "switched off: no layer added");
        editor.reset();
        [window close];
        return;
    }

    // Several editor sizes (the editor scales its contents with a transform).
    for (const auto width : { bqt::editor::minimumWidth, 865, 1000, 1297, bqt::editor::maximumWidth })
    {
        editor->setSize(width, bqt::editor::heightForWidth(width));
        for (size_t i = 0; i < meters.size(); ++i)
        {
            meters[i]->syncNeedlePresentation(true);
            checkAligned(*meters[i], window, "width " + juce::String(width) + " meter " + juce::String(static_cast<int>(i)));
        }
    }
    check(sublayerCount(peerView) == baselineSublayers + 2, "exactly one layer per meter");

    // Resizing re-presents at once (through the movement watcher), with no frame callback.
    editor->setSize(1100, bqt::editor::heightForWidth(1100));
    for (auto* meter : meters)
        checkAligned(*meter, window, "after resize, before any frame");

    // A moving reading: the clock moves the layer on its own, with no message-thread call, and
    // every position it shows is exactly the painted needle for the reading it drew (no glide).
    // After the audio stops, the movement falls back to rest, so the reading keeps changing.
    auto& clock = clockOf(*editor);
    check(clock.probe().running, "clock runs while a needle is native");
    driveMeters(processor, 0.3f);
    for (auto* meter : meters)
        check(meter->updateLevel(), "needle moves with signal");
    {
        const auto before = clock.probe();
        std::vector<juce::Point<float>> tips;
        for (int i = 0; i < 6; ++i)
        {
            juce::Thread::sleep(25);   // no event loop: only the clock thread can move the needle
            for (auto* meter : meters)
            {
                const auto probe = meter->nativeNeedleProbe();
                checkAligned(*meter, window, "moving, sample " + juce::String(i));
                check(! probe.animating, "movement is presented exactly without animation");
                tips.push_back(probe.tip);
            }
        }
        const auto after = clock.probe();
        check(after.presents > before.presents + 4, "clock presented on its own thread ("
                                                     + juce::String(static_cast<juce::int64>(after.presents - before.presents)) + " presents in 150 ms)");
        check(after.refreshHz > 20.0, "clock knows its refresh rate (" + juce::String(after.refreshHz, 2) + " Hz)");
        check(! near(tips.front(), tips[tips.size() - 2], 0.01f), "the clock moved the tip without the message thread");
    }

    // Bypassed look: painted, layer hidden; and back. With both needles painted the clock stops.
    for (auto* meter : meters)
    {
        meter->syncNeedlePresentation(false);
        check(! meter->isNeedleNative() && ! meter->nativeNeedleProbe().visible, "bypassed look paints the needle");
    }
    check(! clock.probe().running, "clock stops while no needle is native");
    {
        const auto stoppedAt = clock.probe().presents;
        juce::Thread::sleep(40);
        check(clock.probe().presents == stoppedAt, "a stopped clock presents nothing");
    }
    for (auto* meter : meters)
    {
        meter->syncNeedlePresentation(true);
        checkAligned(*meter, window, "after bypass");
    }
    check(clock.probe().running, "clock restarts with a native needle");

    // A component over a needle's sweep: that meter paints, the other keeps its layer.
    {
        juce::Component cover;
        editor->addAndMakeVisible(cover);
        cover.setBounds(editor->getLocalArea(meters[0], meters[0]->needleSweepArea().withSizeKeepingCentre(10, 10)));
        for (auto* meter : meters)
            meter->syncNeedlePresentation(true);
        check(! meters[0]->isNeedleNative() && ! meters[0]->nativeNeedleProbe().visible, "covered meter paints its needle");
        check(meters[1]->isNeedleNative(), "uncovered meter keeps the layer");
        editor->removeChildComponent(&cover);
    }
    meters[0]->syncNeedlePresentation(true);
    checkAligned(*meters[0], window, "after cover removed");

    // The About card covers everything.
    juce::Component* about = nullptr;
    for (auto* child : editor->getChildren())
        if (juce::String(typeid(*child).name()).contains("AboutPanel"))
            about = child;
    check(about != nullptr, "found the About card");
    if (about != nullptr)
    {
        about->setVisible(true);
        for (auto* meter : meters)
        {
            meter->syncNeedlePresentation(true);
            check(! meter->isNeedleNative(), "About card open: needle painted");
        }
        about->setVisible(false);
        for (auto* meter : meters)
        {
            meter->syncNeedlePresentation(true);
            check(meter->isNeedleNative(), "About card closed: layer back");
        }
    }

    // Detach: removing the editor from its view switches to the painted needle at once (JUCE
    // hides the editor), and the layers let go of the old view by the next frame call. JUCE gives
    // no notice when a peer is deleted; the old view is off screen by then either way.
    editor->removeFromDesktop();
    for (auto* meter : meters)
    {
        check(! meter->isNeedleNative(), "no peer: painted");
        meter->syncNeedlePresentation(true);
        check(! meter->isNeedleNative(), "no peer after a frame: still painted");
        check(! meter->nativeNeedleProbe().attached, "no peer after a frame: layer detached");
    }
    check(sublayerCount(peerView) == baselineSublayers, "removing from the host view leaves no layer behind");

    // Reattach, then delete the editor while the layers are showing.
    editor->addToDesktop(0, (__bridge void*) content);
    editor->setVisible(true);
    NSView* secondView = (__bridge NSView*) editor->getPeer()->getNativeHandle();
    const auto secondBaseline = sublayerCount(secondView);
    for (auto* meter : meters)
    {
        meter->syncNeedlePresentation(true);
        checkAligned(*meter, window, "reattached");
    }
    check(sublayerCount(secondView) == secondBaseline + 2, "reattached: one layer per meter");
    // Public stop begins teardown and must stay final while the other meter is attached.
    auto& shutdownClock = const_cast<bqst::vu::NeedleClock&>(clock);
    shutdownClock.stop();
    const auto stoppedTicks = clock.probe().ticks;
    meters[0]->setNeedleClock(nullptr);
    check(! clock.probe().running, "detaching first needle cannot restart a stopped clock");
    meters[1]->syncNeedlePresentation(true);
    check(! clock.probe().running, "presenting remaining needle cannot restart a stopped clock");
    juce::Thread::sleep(40);
    check(clock.probe().ticks == stoppedTicks, "no clock ticks after final shutdown");
    editor.reset();
    check(sublayerCount(secondView) == secondBaseline, "deleting the editor leaves no layer behind");

    [window close];
}

// Feeds the processor from its own thread the way a host's audio thread does: a 440 Hz tone whose
// level swings slowly, so the needle never settles or rests on a stop.
class AudioFeed
{
public:
    explicit AudioFeed(BqtAudioProcessor& p) : processor(p)
    {
        thread = std::thread([this]
        {
            juce::AudioBuffer<float> buffer(2, 480);
            juce::MidiBuffer midi;
            double phase = 0.0, time = 0.0;
            while (! stopping.load())
            {
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    // Peak 0.03..0.15, about -15 to -1.5 VU (0 VU = -18 dBFS RMS): always on the scale.
                    const auto level = 0.03 + 0.12 * (0.5 + 0.5 * std::sin(time * 2.0 * juce::MathConstants<double>::pi * 1.3));
                    const auto sample = static_cast<float>(level * std::sin(phase));
                    buffer.setSample(0, i, sample);
                    buffer.setSample(1, i, sample);
                    phase += 2.0 * juce::MathConstants<double>::pi * 440.0 / 48000.0;
                    time += 1.0 / 48000.0;
                }
                processor.processBlock(buffer, midi);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    ~AudioFeed()
    {
        stopping.store(true);
        thread.join();
    }

private:
    BqtAudioProcessor& processor;
    std::atomic<bool> stopping { false };
    std::thread thread;
};

void pump(double seconds)
{
    const auto end = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    while (juce::Time::getMillisecondCounterHiRes() < end)
        @autoreleasepool { [NSRunLoop.mainRunLoop runMode: NSDefaultRunLoopMode beforeDate: [NSDate dateWithTimeIntervalSinceNow: 0.005]]; }
}

NSWindow* makeHostWindow(NSRect frame)
{
    NSWindow* window = [[NSWindow alloc] initWithContentRect: frame styleMask: NSWindowStyleMaskBorderless
                                                     backing: NSBackingStoreBuffered defer: NO];
    window.releasedWhenClosed = NO;
    window.ignoresMouseEvents = YES;
    return window;
}

// Editor closed and plugin destroyed while the clock ticks and audio runs on another thread, rapid
// open and close, and peer changes. Meant to run under AddressSanitizer or ThreadSanitizer too.
void teardown()
{
    NSWindow* window = makeHostWindow(NSMakeRect(0, 0, 2000, 2000));   // never ordered on screen
    NSView* content = window.contentView;

    for (int round = 0; round < 4; ++round)
    {
        auto processor = std::make_unique<BqtAudioProcessor>();
        processor->setPlayConfigDetails(2, 2, 48000.0, 480);
        processor->prepareToPlay(48000.0, 480);
        auto feed = std::make_unique<AudioFeed>(*processor);

        // Rapid open and close, sometimes before the clock's first tick, sometimes after.
        for (int i = 0; i < 12; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
            editor->setVisible(true);
            editor->addToDesktop(0, (__bridge void*) content);
            std::vector<BqtVuMeter*> meters;
            collectMeters(*editor, meters);
            for (auto* meter : meters)
                meter->syncNeedlePresentation(true);
            if (bqst::vu::nativeNeedleCompiledIn && i == 0)
                check(clockOf(*editor).probe().running, "teardown: clock running before close");
            if (i % 3 == 1)
                juce::Thread::sleep(i % 7 + 1);
            if (i % 4 == 2)
            {
                // A peer change while ticking.
                editor->removeFromDesktop();
                editor->addToDesktop(0, (__bridge void*) content);
                for (auto* meter : meters)
                    meter->syncNeedlePresentation(true);
                juce::Thread::sleep(3);
            }
            editor.reset();
        }

        // Plugin destroyed the way a host does it: editor gone, audio stopped, then the processor,
        // with the clock ticking up to the editor's deletion.
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
            editor->setVisible(true);
            editor->addToDesktop(0, (__bridge void*) content);
            std::vector<BqtVuMeter*> meters;
            collectMeters(*editor, meters);
            for (auto* meter : meters)
                meter->syncNeedlePresentation(true);
            juce::Thread::sleep(30);
            if (bqst::vu::nativeNeedleCompiledIn)
                check(clockOf(*editor).probe().presents > 0, "teardown: clock presented before the plugin closed");
            editor.reset();
            feed.reset();
            processor->releaseResources();
            processor.reset();
        }
    }
    [window close];
}

// The proof that matters: with the editor in a real window on screen, the needle reaches the
// screen at the display's refresh rate while the message thread is stalled outright, and while it
// is throttled to 30 events a second. Presentation is measured by the window server itself: a
// ScreenCaptureKit stream of the display, cropped to the meter's area, reports every new
// composited picture of it, with the time it was displayed. Nothing else in that area can change while the message thread is stalled
// (JUCE cannot paint), and a control run with the painted needle shows that the capture does see
// a stalled message thread. Needs Screen Recording permission for the process running the test;
// without it the proof is skipped with a message, not failed.
void presentation(BqtAudioProcessor& processor)
{
    if (! bqst::vu::nativeNeedleCompiledIn)
        return;
    if (@available(macOS 12.3, *))
    {
        if (! CGPreflightScreenCaptureAccess())
        {
            std::cout << "SKIP presentation proof: no Screen Recording permission for this process\n";
            return;
        }

        // An accessory process never becomes the active app; the window is ordered in without
        // taking focus and ignores the mouse. It is not moved by the user's input in any way.
        [NSApp setActivationPolicy: NSApplicationActivationPolicyAccessory];
        NSScreen* fastest = NSScreen.mainScreen;
        for (NSScreen* screen in NSScreen.screens)
            if (screen.maximumFramesPerSecond > fastest.maximumFramesPerSecond)
                fastest = screen;
        const auto width = bqt::editor::minimumWidth, height = bqt::editor::heightForWidth(width);
        const auto placeOn = [&](NSScreen* screen)
        {
            const auto area = screen.visibleFrame;
            return NSMakeRect(NSMaxX(area) - width - 20, NSMinY(area) + 20, width, height);
        };
        NSWindow* window = makeHostWindow(placeOn(fastest));
        window.level = NSFloatingWindowLevel;
        [window orderFrontRegardless];

        auto feed = std::make_unique<AudioFeed>(processor);
        // The editor's own frame log, as a host capture would record it.
        const auto logFile = juce::File::createTempFile(".log");
        setenv("BQST_FRAME_LOG", logFile.getFullPathName().toRawUTF8(), 1);
        auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
        unsetenv("BQST_FRAME_LOG");
        editor->setSize(width, height);
        editor->setVisible(true);
        editor->addToDesktop(0, (__bridge void*) window.contentView);

        // Counts the same JUCE display callback the editor's meters run on.
        std::atomic<int> juceCallbacks { 0 };
        auto counter = std::make_unique<juce::VBlankAttachment>(editor.get(), [&juceCallbacks] { juceCallbacks.fetch_add(1); });

        std::vector<BqtVuMeter*> meters;
        collectMeters(*editor, meters);
        pump(0.8);
        check(meters.size() == 2 && meters[0]->isNeedleNative() && meters[1]->isNeedleNative(), "on screen: needles native");
        const auto& clock = clockOf(*editor);

        // Capture the left meter's area (host-view points = window points here).
        const auto clip = meters[0]->nativeNeedleProbe().clip;
        // Completion handlers run on other queues: hand results over through semaphores.
        const auto waitFor = [](dispatch_semaphore_t done)
        {
            while (dispatch_semaphore_wait(done, DISPATCH_TIME_NOW) != 0)
                pump(0.01);
        };
        __block SCShareableContent* shareable = nil;
        dispatch_semaphore_t listed = dispatch_semaphore_create(0);
        [SCShareableContent getShareableContentExcludingDesktopWindows: NO onScreenWindowsOnly: YES
                                                     completionHandler: ^(SCShareableContent* c, NSError*) { shareable = c; dispatch_semaphore_signal(listed); }];
        waitFor(listed);
        // The display itself, as composited for scan-out (not the window's own surface, which
        // can update more often than the screen refreshes), cropped to the meter.
        NSScreen* screen = window.screen;
        const auto screenId = [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
        SCDisplay* captured = nil;
        for (SCDisplay* candidate in shareable.displays)
            if (candidate.displayID == screenId)
                captured = candidate;
        check(captured != nil, "capture: the window's display listed");
        if (captured == nil)
        {
            counter.reset();
            editor.reset();
            [window close];
            return;
        }
        SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay: captured excludingWindows: @[]];
        SCStreamConfiguration* config = [SCStreamConfiguration new];
        // Display points, top-left origin; Cocoa window frames are bottom-left, global.
        const auto frame = window.frame;
        config.sourceRect = CGRectMake(NSMinX(frame) - NSMinX(screen.frame) + clip.getX(),
                                       NSMaxY(screen.frame) - NSMaxY(frame) + clip.getY(),
                                       clip.getWidth(), clip.getHeight());
        config.width = static_cast<size_t>(clip.getWidth() * window.backingScaleFactor);
        config.height = static_cast<size_t>(clip.getHeight() * window.backingScaleFactor);
        config.minimumFrameInterval = CMTimeMake(1, 480);
        config.queueDepth = 8;
        config.showsCursor = NO;
        config.pixelFormat = kCVPixelFormatType_32BGRA;
        BqstNeedleCaptureOutput* output = [BqstNeedleCaptureOutput new];
        SCStream* stream = [[SCStream alloc] initWithFilter: filter configuration: config delegate: nil];
        dispatch_queue_t queue = dispatch_queue_create("bqst.test.capture", DISPATCH_QUEUE_SERIAL);
        [stream addStreamOutput: output type: SCStreamOutputTypeScreen sampleHandlerQueue: queue error: nil];
        __block NSError* startError = nil;
        dispatch_semaphore_t started = dispatch_semaphore_create(0);
        [stream startCaptureWithCompletionHandler: ^(NSError* e) { startError = e; dispatch_semaphore_signal(started); }];
        waitFor(started);
        check(startError == nil, "capture started");
        pump(0.5);

        struct Measured { double pictures, presents, callbacks, gapMs, refreshHz; };
        const auto measure = [&](const char* label, const std::function<void()>& messageThread)
        {
            output->maxGapMs.store(0.0);
            output->pictures.store(0);
            output->measuring.store(true);
            const auto c0 = clock.probe();
            const auto v0 = juceCallbacks.load();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            messageThread();
            const auto seconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            output->measuring.store(false);
            const auto c1 = clock.probe();
            Measured m { output->pictures.load() / seconds, static_cast<double>(c1.presents - c0.presents) / seconds,
                         (juceCallbacks.load() - v0) / seconds, output->maxGapMs.load(), c1.refreshHz };
            std::cout << label << ": display " << m.refreshHz << " Hz, on-screen needle pictures " << m.pictures
                      << "/s (longest gap " << m.gapMs << " ms), clock presents " << m.presents
                      << "/s, JUCE display callbacks " << m.callbacks << "/s\n";
            return m;
        };
        const auto atDisplayRate = [&](const Measured& m, const juce::String& label)
        {
            const auto period = 1000.0 / m.refreshHz;
            check(m.pictures >= 0.9 * m.refreshHz, label + ": needle pictures reach the screen at display rate");
            check(m.pictures <= 1.03 * m.refreshHz + 1.0, label + ": pictures counted are display frames, not commits");
            check(m.presents >= 0.95 * m.refreshHz, label + ": clock presents at display rate");
            check(m.gapMs <= 3.5 * period, label + ": no long gap on screen (" + juce::String(m.gapMs, 1) + " ms)");
        };

        const auto stalled = measure("message thread stalled 2 s", [] { std::this_thread::sleep_for(std::chrono::seconds(2)); });
        atDisplayRate(stalled, "stalled");
        check(stalled.callbacks < 1.0, "stalled: no JUCE callback ran");

        const auto throttled = measure("message thread throttled to 30/s", []
        {
            const auto end = juce::Time::getMillisecondCounterHiRes() + 2000.0;
            while (juce::Time::getMillisecondCounterHiRes() < end)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
                @autoreleasepool { [NSRunLoop.mainRunLoop runMode: NSDefaultRunLoopMode beforeDate: [NSDate date]]; }
            }
        });
        atDisplayRate(throttled, "throttled");
        check(throttled.callbacks <= 31.0, "throttled: JUCE callbacks limited to 30/s");
        check(meters[0]->isNeedleNative() && meters[1]->isNeedleNative(), "throttled: needles stayed native");

        // Control: with the bypassed look the needles are painted, so a stalled message thread
        // must freeze them on screen. This shows the capture detects message-thread dependence.
        auto* bypass = processor.state().getParameter("bypass");
        bypass->setValueNotifyingHost(1.0f);
        pump(0.5);
        check(! meters[0]->isNeedleNative(), "control: bypassed look paints the needle");
        const auto control = measure("control: painted needle, message thread stalled 1.5 s",
                                     [] { std::this_thread::sleep_for(std::chrono::milliseconds(1500)); });
        check(control.pictures < 3.0, "control: a painted needle does not move while the message thread is stalled");
        check(control.presents < 1.0, "control: clock idle while no needle is native");
        bypass->setValueNotifyingHost(0.0f);
        pump(0.5);
        check(meters[0]->isNeedleNative(), "control: layer back after bypass");

        // Window moved to another screen: the clock follows it and keeps the needle at that
        // screen's rate.
        for (NSScreen* screen in NSScreen.screens)
        {
            if (screen == window.screen)
                continue;
            [window setFrame: placeOn(screen) display: YES];
            pump(0.6);
            const auto id = [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
            check(clock.probe().display == id, "moved: clock rebound to the window's new screen");
            // The capture stays on the first display; here the clock's own count is the evidence.
            const auto moved = measure("moved to another screen (capture not following), message thread stalled 1 s",
                                       [] { std::this_thread::sleep_for(std::chrono::seconds(1)); });
            check(moved.presents >= 0.95 * moved.refreshHz, "moved: clock presents at the new screen's rate");
            break;
        }

        dispatch_semaphore_t stopped = dispatch_semaphore_create(0);
        [stream stopCaptureWithCompletionHandler: ^(NSError*) { dispatch_semaphore_signal(stopped); }];
        waitFor(stopped);

        // Closed while ticking, with audio still running.
        counter.reset();
        editor.reset();
        feed.reset();
        [window close];
        pump(0.1);

        // The frame log shows the needle's real rate next to the JUCE callback rate: a second with
        // no display callback at all (the stall) still has the needle presented at display rate.
        auto stallLineFound = false;
        juce::StringArray lines;
        lines.addLines(logFile.loadFileAsString());
        for (const auto& line : lines)
        {
            const auto field = [&line](const char* name)
            {
                return line.fromFirstOccurrenceOf(juce::String(" ") + name + "=", false, false).upToFirstOccurrenceOf(" ", false, false).getDoubleValue();
            };
            const auto value = [&line](const char* name) { return line.fromFirstOccurrenceOf(juce::String(name) + "=", false, false).getDoubleValue(); };
            if (! stallLineFound && value("frames") == 0.0 && field("refresh_hz") > 20.0
                && field("needle_presents_per_s") >= 0.95 * field("refresh_hz"))
            {
                stallLineFound = true;
                std::cout << "frame log during the stall: " << line << "\n";
            }
        }
        check(stallLineFound, "frame log: a stalled second shows frames=0 with the needle at display rate");
        logFile.deleteFile();
    }
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    {
        BqtAudioProcessor processor;
        processor.setPlayConfigDetails(2, 2, 48000.0, 512);
        processor.prepareToPlay(48000.0, 512);

        @autoreleasepool { switchAccessibility(processor); }
        selectionRules();
        paintedWhenOffscreen(processor);
        slowPaintedFade(processor);
        @autoreleasepool { hosted(processor); }
        @autoreleasepool { presentation(processor); }

        processor.releaseResources();
    }
    @autoreleasepool { teardown(); }

    std::cout << checks - failures << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
