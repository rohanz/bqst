// Native VU needle checks (macOS, no host needed).
//
// The editor is hosted the way a plugin host hosts it (added to an NSView), but in a window that
// is never ordered on screen: nothing appears, nothing takes focus, no input is sent. What a real
// host and a 120 Hz screen add (whether the system then shows every frame) cannot be checked here.
//
// Checks:
//  - fallback selection rules and the "covered from above" test, on plain components;
//  - an editor that is not on the desktop (as in BqstEditorSnapshot) paints its needle;
//  - hosted, the layer is attached and its needle, pivot dot and clip land exactly where the
//    painted needle would, at several editor sizes, mapped back through Core Animation;
//  - a moving reading is handed over as a short animation ending at the painted position;
//  - anything over the needle's sweep (a component, the About card) and the bypassed look switch
//    to the painted needle, and back;
//  - removing the editor from its view, and deleting the editor, leave no layer behind.

#include "../src/BqtEditorSize.h"
#include "../src/BqtEditorStyle.h"
#include "../src/BqtVuMeter.h"
#include "../src/BqtVuNeedleLayer.h"
#include "../src/PluginEditor.h"
#include "../src/PluginProcessor.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <iostream>
#include <typeinfo>

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
    ok.editorAllows = ok.showing = ok.hasHostView = true;
    check(chooseNeedleMode(ok) == NeedleMode::native, "all conditions met selects the layer");

    auto c = ok; c.compiledIn = false;
    check(chooseNeedleMode(c) == NeedleMode::painted, "switch off paints");
    c = ok; c.editorAllows = false;
    check(chooseNeedleMode(c) == NeedleMode::painted, "bypassed look paints");
    c = ok; c.showing = false;
    check(chooseNeedleMode(c) == NeedleMode::painted, "not showing paints");
    c = ok; c.hasHostView = false;
    check(chooseNeedleMode(c) == NeedleMode::painted, "no host view (offscreen) paints");
    c = ok; c.coveredFromAbove = true;
    check(chooseNeedleMode(c) == NeedleMode::painted, "covered needle paints");

    check(needleAnimationSeconds(1.0 / 120.0) <= 2.0 / 120.0, "animation at 120 Hz is under two frames");
    check(needleAnimationSeconds(1.0 / 60.0) <= 2.0 / 60.0, "animation at 60 Hz is under two frames");
    check(needleAnimationSeconds(1.0) <= 1.0 / 30.0 + 1e-9, "animation is bounded when updates stall");
    check(needleAnimationSeconds(0.0) > 0.0, "animation is never zero length");

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
        meter->syncNeedlePresentation(true, 1.0 / 60.0);
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

struct Expected
{
    juce::Rectangle<float> clip;
    juce::Point<float> pivot, tip;
    float scale = 1.0f;
};

// Where the painted needle lands in the peer view, through JUCE's own mapping.
Expected expectedFor(BqtVuMeter& meter)
{
    auto& top = meter.getPeer()->getComponent();
    const auto desktop = top.getDesktopScaleFactor();
    const auto line = meter.currentNeedleLine();
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
    const auto e = expectedFor(meter);
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
            meter->syncNeedlePresentation(true, 1.0 / 120.0);
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
            meters[i]->syncNeedlePresentation(true, 1.0 / 120.0);
            checkAligned(*meters[i], window, "width " + juce::String(width) + " meter " + juce::String(static_cast<int>(i)));
        }
    }
    check(sublayerCount(peerView) == baselineSublayers + 2, "exactly one layer per meter");

    // Resizing re-presents at once (through the movement watcher), with no frame callback.
    editor->setSize(1100, bqt::editor::heightForWidth(1100));
    for (auto* meter : meters)
        checkAligned(*meter, window, "after resize, before any frame");

    // A moving reading: the layer's model ends exactly at the painted needle, with a short
    // animation from where it was.
    driveMeters(processor, 0.3f);
    for (auto* meter : meters)
    {
        const auto before = meter->nativeNeedleProbe().tip;
        check(meter->updateLevel(1.0 / 120.0), "needle moves with signal");
        meter->syncNeedlePresentation(true, 1.0 / 120.0);
        const auto probe = meter->nativeNeedleProbe();
        checkAligned(*meter, window, "moving");
        check(probe.animating, "a move is handed over as an animation");
        check(probe.animationSeconds > 0.0 && probe.animationSeconds <= 2.0 / 60.0,
              "the move animation is short (" + juce::String(probe.animationSeconds * 1000.0, 2) + " ms)");
        check(! near(before, probe.tip, 0.01f), "the tip changed");
        // Same reading again: the running move is left alone.
        meter->syncNeedlePresentation(true, 1.0 / 120.0);
        check(meter->nativeNeedleProbe().animating, "an unchanged frame leaves the move running");
    }

    // Bypassed look: painted, layer hidden; and back.
    for (auto* meter : meters)
    {
        meter->syncNeedlePresentation(false, 1.0 / 120.0);
        check(! meter->isNeedleNative() && ! meter->nativeNeedleProbe().visible, "bypassed look paints the needle");
        meter->syncNeedlePresentation(true, 1.0 / 120.0);
        checkAligned(*meter, window, "after bypass");
    }

    // A component over a needle's sweep: that meter paints, the other keeps its layer.
    {
        juce::Component cover;
        editor->addAndMakeVisible(cover);
        cover.setBounds(editor->getLocalArea(meters[0], meters[0]->needleSweepArea().withSizeKeepingCentre(10, 10)));
        for (auto* meter : meters)
            meter->syncNeedlePresentation(true, 1.0 / 120.0);
        check(! meters[0]->isNeedleNative() && ! meters[0]->nativeNeedleProbe().visible, "covered meter paints its needle");
        check(meters[1]->isNeedleNative(), "uncovered meter keeps the layer");
        editor->removeChildComponent(&cover);
    }
    meters[0]->syncNeedlePresentation(true, 1.0 / 120.0);
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
            meter->syncNeedlePresentation(true, 1.0 / 120.0);
            check(! meter->isNeedleNative(), "About card open: needle painted");
        }
        about->setVisible(false);
        for (auto* meter : meters)
        {
            meter->syncNeedlePresentation(true, 1.0 / 120.0);
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
        meter->syncNeedlePresentation(true, 1.0 / 120.0);
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
        meter->syncNeedlePresentation(true, 1.0 / 120.0);
        checkAligned(*meter, window, "reattached");
    }
    check(sublayerCount(secondView) == secondBaseline + 2, "reattached: one layer per meter");
    editor.reset();
    check(sublayerCount(secondView) == secondBaseline, "deleting the editor leaves no layer behind");

    [window close];
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
        @autoreleasepool { hosted(processor); }

        processor.releaseResources();
    }

    std::cout << checks - failures << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
