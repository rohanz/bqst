#include "PluginEditor.h"

#include "BqtEditorStyle.h"

#include <cmath>

namespace
{
using namespace bqst::ui;
} // namespace

void BqtAudioProcessorEditor::timerCallback()
{
    const auto bypassIsOn = audioProcessor.state().getRawParameterValue("bypass")->load() > 0.5f;
    bypass.setToggleState(bypassIsOn, juce::dontSendNotification);
    requestRackBypassVisualState(bypassIsOn);

    satTypeButton.setToggleState(sideControls[0].satType.getSelectedItemIndex() == 1, juce::dontSendNotification);
    // Picks up link changes from host automation, preset loads and state restore; a click on a
    // link button also calls this directly.
    updateLinkedAttachments();

    if (activeReadoutSlider != nullptr)
    {
        if (activeReadoutSlider->isMouseButtonDown())
            updateDragValueReadout(*activeReadoutSlider);
        else
            hideDragValueReadout();
    }

    syncHoverTargetsFromMouse();
    updateHoverValueReadout();
    updateTopBarHelp();
}

bool BqtAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    const auto keyCode = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
    const auto modifiers = key.getModifiers();
    const auto wantsUndo = keyCode == 'z' && (modifiers.isCommandDown() || modifiers.isCtrlDown());
    const auto wantsRedo = wantsUndo && modifiers.isShiftDown();

    if (wantsRedo)
        return redoLastPluginEdit();

    if (wantsUndo)
        return undoLastPluginEdit();

    return false;
}

bool BqtAudioProcessorEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    return keyPressed(key);
}

void BqtAudioProcessorEditor::mouseDown(const juce::MouseEvent&)
{
    beginUndoableEdit();
}

void BqtAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    juce::Component::SafePointer<BqtAudioProcessorEditor> safeThis(this);
    juce::MessageManager::callAsync([safeThis]
    {
        if (safeThis != nullptr)
            safeThis->finishUndoableEdit();
    });
}

void BqtAudioProcessorEditor::sliderValueChanged(juce::Slider* slider)
{
    if (slider == nullptr)
        return;

    if (slider == &inputTrim)
    {
        const auto currentInputTrim = inputTrim.getValue();
        const auto deltaDb = currentInputTrim - inputTrimCompensationStart;

        if (userGestures.isActive(&inputTrim)
            && inputTrim.isMouseButtonDown()
            && juce::ModifierKeys::currentModifiers.isCtrlDown()
            && std::abs(deltaDb) > 0.0)
        {
            const juce::ScopedValueSetter<bool> scopedMirror(isMirroringLinkedControl, true);
            for (size_t index = 0; index < sideControls.size(); ++index)
            {
                auto& controls = sideControls[index];
                const auto compensatedValue = juce::jlimit(controls.outputTrim.getMinimum(),
                                                          controls.outputTrim.getMaximum(),
                                                          outputTrimCompensationStart[index] - deltaDb);
                controls.outputTrim.setValue(compensatedValue, juce::sendNotificationSync);
            }
        }
    }

    updateDragValueReadout(*slider);
}

bool BqtAudioProcessorEditor::isGroupLinked(bqt::editor::LinkGroup group) const
{
    return audioProcessor.state().getRawParameterValue(bqt::editor::linkParameterId(group))->load() > 0.5f;
}

// Only Ctrl-drags on an unlinked group are mirrored. A linked group needs no mirroring: the
// processor links it, and side B's controls are attached to side A's parameters.
bool BqtAudioProcessorEditor::shouldMirrorToOtherSide(bqt::editor::LinkGroup group) const
{
    return bqt::editor::ctrlMirrorsBothSides(isGroupLinked(group), juce::ModifierKeys::currentModifiers.isCtrlDown());
}

// While a group is linked, side B's knobs display and edit side A's parameters (two sliders can
// share one parameter), so a linked move writes exactly one parameter -- one undo step in hosts
// that record undo per parameter. Unlinked, they go back to side B's own parameters.
void BqtAudioProcessorEditor::updateLinkedAttachments()
{
    using bqt::editor::LinkGroup;

    struct LinkedControl
    {
        const char* suffix;
        juce::Slider SideControls::* slider;
        std::unique_ptr<SliderAttachment> SideControls::* attachment;
    };

    static const std::array<LinkedControl, 4> eqControls { {
        { "LowGain",  &SideControls::lowGain,  &SideControls::lowGainAttachment },
        { "LowFreq",  &SideControls::lowFreq,  &SideControls::lowFreqAttachment },
        { "HighGain", &SideControls::highGain, &SideControls::highGainAttachment },
        { "HighFreq", &SideControls::highFreq, &SideControls::highFreqAttachment },
    } };
    static const std::array<LinkedControl, 3> satControls { {
        { "Drive",      &SideControls::drive,      &SideControls::driveAttachment },
        { "Mix",        &SideControls::mix,        &SideControls::mixAttachment },
        { "OutputTrim", &SideControls::outputTrim, &SideControls::outputTrimAttachment },
    } };

    const auto update = [this](LinkGroup group, const auto& controls)
    {
        const auto groupIndex = static_cast<size_t>(group);
        const auto linked = isGroupLinked(group);
        if (sideBAttachedToA[groupIndex] == linked)
            return;

        // Swapping an attachment mid-drag would leave its gesture open on the old parameter, so a
        // host link change during a drag lands once the drag ends (this runs on every timer tick).
        for (const auto& control : controls)
            for (auto& side : sideControls)
                if (userGestures.isActive(&(side.*control.slider)))
                    return;

        auto& sideB = sideControls[1];
        for (const auto& control : controls)
        {
            (sideB.*control.attachment).reset();
            (sideB.*control.attachment) = std::make_unique<SliderAttachment>(
                audioProcessor.state(), bqt::editor::attachedParameterId(1, control.suffix, linked), sideB.*control.slider);
        }

        sideBAttachedToA[groupIndex] = linked;
    };

    update(LinkGroup::eq, eqControls);
    update(LinkGroup::sat, satControls);
}

// The user clicked a link button off. Side B has been ignored while linked, so its stored values
// may be stale; copy side A's in (one gesture per parameter, one plugin undo step together with
// the link change) so R/S keeps sounding the same. Unlinking via host automation or state restore
// does not come through here: there side B simply reveals its stored values.
void BqtAudioProcessorEditor::unlinkGroupFromUi(bqt::editor::LinkGroup group)
{
    // A mouse click already opened the capture in mouseDown (before the link parameter changed),
    // so beginUndoableEdit is a no-op then; a keyboard press on the button gets its own capture.
    beginUndoableEdit();
    const auto writes = bqt::editor::unlinkCopyWrites(group, [this](const juce::String& id)
    {
        auto* parameter = audioProcessor.state().getParameter(id);
        return parameter != nullptr ? parameter->getValue() : 0.0f;
    });

    for (const auto& [id, value] : writes)
    {
        if (auto* parameter = audioProcessor.state().getParameter(id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(value);
            parameter->endChangeGesture();
        }
    }
    finishUndoableEdit();
}

void BqtAudioProcessorEditor::beginMirroredParameterGesture(const juce::String& parameterId)
{
    if (auto* parameter = audioProcessor.state().getParameter(parameterId))
    {
        if (! activeMirroredGestureParameters.contains(parameter))
        {
            parameter->beginChangeGesture();
            activeMirroredGestureParameters.add(parameter);
        }
    }
}

void BqtAudioProcessorEditor::beginLinkedMirrorGestureFor(juce::Slider& slider)
{
    const auto eqLinked = shouldMirrorToOtherSide(bqt::editor::LinkGroup::eq);
    const auto satLinked = shouldMirrorToOtherSide(bqt::editor::LinkGroup::sat);

    for (int sideIndex = 0; sideIndex < 2; ++sideIndex)
    {
        auto& controls = sideControls[static_cast<size_t>(sideIndex)];
        const auto otherPrefix = sidePrefix(1 - sideIndex);

        if (eqLinked)
        {
            if (&slider == &controls.lowGain)       beginMirroredParameterGesture(otherPrefix + "LowGain");
            else if (&slider == &controls.highGain) beginMirroredParameterGesture(otherPrefix + "HighGain");
        }

        if (satLinked)
        {
            if (&slider == &controls.drive)           beginMirroredParameterGesture(otherPrefix + "Drive");
            else if (&slider == &controls.mix)        beginMirroredParameterGesture(otherPrefix + "Mix");
            else if (&slider == &controls.outputTrim) beginMirroredParameterGesture(otherPrefix + "OutputTrim");
        }
    }
}

void BqtAudioProcessorEditor::endLinkedMirrorGestures()
{
    for (auto* parameter : activeMirroredGestureParameters)
        if (parameter != nullptr)
            parameter->endChangeGesture();

    activeMirroredGestureParameters.clear();
}

void BqtAudioProcessorEditor::mirrorLinkedSteppedFrequencyVisual(juce::Slider& slider)
{
    // The frequency knobs only write their parameter on release, so the other side's knob would
    // otherwise lag the drag: show it moving whether the processor links it or Ctrl mirrors it.
    if (! isGroupLinked(bqt::editor::LinkGroup::eq) && ! shouldMirrorToOtherSide(bqt::editor::LinkGroup::eq))
        return;

    auto& left = sideControls[0];
    auto& right = sideControls[1];
    const juce::ScopedValueSetter<bool> scopedMirror(isMirroringLinkedControl, true);

    if (&slider == &left.lowFreq)
        right.lowFreq.setValue(left.lowFreq.getValue(), juce::dontSendNotification);
    else if (&slider == &right.lowFreq)
        left.lowFreq.setValue(right.lowFreq.getValue(), juce::dontSendNotification);
    else if (&slider == &left.highFreq)
        right.highFreq.setValue(left.highFreq.getValue(), juce::dontSendNotification);
    else if (&slider == &right.highFreq)
        left.highFreq.setValue(right.highFreq.getValue(), juce::dontSendNotification);
}

void BqtAudioProcessorEditor::commitParameterGesture(const juce::String& parameterId, float plainValue)
{
    if (auto* parameter = audioProcessor.state().getParameter(parameterId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
        parameter->endChangeGesture();
    }
}

void BqtAudioProcessorEditor::toggleSatTypeBothSides(juce::Button& button)
{
    auto* sideA = audioProcessor.state().getParameter("aSatType");
    auto* sideB = audioProcessor.state().getParameter("bSatType");
    if (sideA == nullptr || sideB == nullptr)
        return;

    // Toggle from side A even if a loaded session left the sides different; both get the result.
    const auto next = bqt::editor::nextSatTypeIndex(juce::roundToInt(sideA->convertFrom0to1(sideA->getValue())));
    const auto normalisedNext = sideA->convertTo0to1(static_cast<float>(next));

    // One undo step for both writes. A mouse click already opened the capture in mouseDown, so
    // beginUndoableEdit is a no-op then; a keyboard press on the button gets its own capture.
    beginUndoableEdit();
    sideA->beginChangeGesture();
    sideB->beginChangeGesture();
    sideA->setValueNotifyingHost(normalisedNext);
    sideB->setValueNotifyingHost(normalisedNext);
    sideA->endChangeGesture();
    sideB->endChangeGesture();
    finishUndoableEdit();

    satTypeButton.setToggleState(next == 1, juce::dontSendNotification);

    if (hoveredHelpComponent == &button && helpVisible)
    {
        hoveredHelpText = button.getProperties()[next == 1 ? "bqtGritHelp" : "bqtCreamHelp"].toString();
        showReadout(button, hoveredHelpText);
    }
}

void BqtAudioProcessorEditor::commitMirroredSteppedFrequency(juce::Slider& slider)
{
    if (! shouldMirrorToOtherSide(bqt::editor::LinkGroup::eq))
        return;

    for (int sideIndex = 0; sideIndex < 2; ++sideIndex)
    {
        const auto& controls = sideControls[static_cast<size_t>(sideIndex)];
        const auto& other = sideControls[static_cast<size_t>(1 - sideIndex)];
        const auto otherPrefix = sidePrefix(1 - sideIndex);

        if (&slider == &controls.lowFreq)
            commitParameterGesture(otherPrefix + "LowFreq", static_cast<float>(other.lowFreq.getValue()));
        else if (&slider == &controls.highFreq)
            commitParameterGesture(otherPrefix + "HighFreq", static_cast<float>(other.highFreq.getValue()));
    }
}

void BqtAudioProcessorEditor::sliderDragStarted(juce::Slider* slider)
{
    if (slider == nullptr)
        return;

    userGestures.begin(slider);
    beginUndoableEdit();
    beginLinkedMirrorGestureFor(*slider);
    hideHoverValueReadout();

    if (slider == &inputTrim)
    {
        inputTrimCompensationStart = inputTrim.getValue();
        for (size_t index = 0; index < sideControls.size(); ++index)
            outputTrimCompensationStart[index] = sideControls[index].outputTrim.getValue();
    }

    updateDragValueReadout(*slider);
}

void BqtAudioProcessorEditor::sliderDragEnded(juce::Slider* slider)
{
    if (slider == nullptr)
        return;

    // A double-click reset or wheel step sends its own drag start/end nested inside the mouse
    // drag; only the outermost end closes the gesture.
    if (! userGestures.end(slider))
        return;

    commitMirroredSteppedFrequency(*slider);

    endLinkedMirrorGestures();
    finishUndoableEdit();
    hideDragValueReadout();
}

void BqtAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (auto* slider = dynamic_cast<juce::Slider*>(event.originalComponent))
        mirrorLinkedSteppedFrequencyVisual(*slider);
}

void BqtAudioProcessorEditor::mouseEnter(const juce::MouseEvent& event)
{
    auto* component = event.originalComponent;
    while (component != nullptr && component != this)
    {
        if (component->getProperties().contains("bqtHelpText"))
        {
            hideHoverValueReadout();
            hoveredHelpComponent = component;
            hoveredHelpText = component->getProperties()["bqtHelpText"].toString();
            helpHoverStartMs = juce::Time::getMillisecondCounter();
            helpVisible = false;
            return;
        }

        component = component->getParentComponent();
    }

    if (auto* slider = dynamic_cast<juce::Slider*>(event.originalComponent))
    {
        hideTopBarHelp();
        hoveredReadoutSlider = slider;
        hoverReadoutStartMs = juce::Time::getMillisecondCounter();
        hoverReadoutVisible = false;
    }
}

void BqtAudioProcessorEditor::mouseMove(const juce::MouseEvent& event)
{
    auto* component = event.originalComponent;
    while (component != nullptr && component != this)
    {
        if (component->getProperties().contains("bqtHelpText"))
        {
            if (hoveredHelpComponent != component)
            {
                hideTopBarHelp();
                hoveredHelpComponent = component;
                hoveredHelpText = component->getProperties()["bqtHelpText"].toString();
                helpHoverStartMs = juce::Time::getMillisecondCounter();
                helpVisible = false;
            }

            return;
        }

        component = component->getParentComponent();
    }

    if (auto* slider = dynamic_cast<juce::Slider*>(event.originalComponent))
    {
        if (hoveredReadoutSlider != slider)
        {
            hoveredReadoutSlider = slider;
            hoverReadoutVisible = false;
            hoverReadoutStartMs = juce::Time::getMillisecondCounter();
        }
    }
}

void BqtAudioProcessorEditor::mouseExit(const juce::MouseEvent& event)
{
    if (event.originalComponent == hoveredReadoutSlider)
        hideHoverValueReadout();

    auto* component = event.originalComponent;
    while (component != nullptr && component != this)
    {
        if (component == hoveredHelpComponent)
        {
            hideTopBarHelp();
            return;
        }

        component = component->getParentComponent();
    }
}

void BqtAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (! event.mods.isCtrlDown())
        return;

    auto* component = event.originalComponent;
    while (component != nullptr && component != this)
    {
        if (component == &inputTrim)
        {
            const juce::ScopedValueSetter<bool> scopedMirror(isMirroringLinkedControl, true);
            for (auto& controls : sideControls)
                controls.outputTrim.setValue(0.0, juce::sendNotificationSync);

            inputTrimCompensationStart = inputTrim.getValue();
            outputTrimCompensationStart = { 0.0, 0.0 };
            return;
        }

        component = component->getParentComponent();
    }
}

void BqtAudioProcessorEditor::updateDragValueReadout(juce::Slider& slider)
{
    if (! slider.isMouseButtonDown())
        return;

    activeReadoutSlider = &slider;
    juce::Component::SafePointer<juce::Slider> safeSlider(&slider);
    juce::MessageManager::callAsync([this, safeSlider]
    {
        if (safeSlider == nullptr || activeReadoutSlider != safeSlider.getComponent())
            return;

        auto& currentSlider = *safeSlider;
        if (! currentSlider.isMouseButtonDown())
            return;

        showReadout(currentSlider, currentSlider.getTextFromValue(currentSlider.getValue()));
    });
}

void BqtAudioProcessorEditor::hideDragValueReadout()
{
    activeReadoutSlider = nullptr;
    if (hoveredReadoutSlider == nullptr && hoveredHelpComponent == nullptr)
        hideReadout();
}

void BqtAudioProcessorEditor::syncHoverTargetsFromMouse()
{
    if (activeReadoutSlider != nullptr || juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        return;

    auto* component = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();

    // getMainMouseSource() is process-global, so this can be a component belonging to a different
    // BQST instance (or another plugin entirely). The help walk below breaks on any component
    // carrying "bqtHelpText", which every instance sets on its own controls, so without this
    // guard instance A would show a readout for instance B's knob -- and keep a pointer to it.
    if (component != nullptr && component != this && ! isParentOf(component))
        component = nullptr;

    if (satTypeButton.isParentOf(component) || component == &satTypeButton)
    {
        const auto gritSelected = sideControls[0].satType.getSelectedItemIndex() == 1;
        auto helpText = satTypeButton.getProperties()[gritSelected ? "bqtGritHelp" : "bqtCreamHelp"].toString();

        if (hoveredHelpComponent != &satTypeButton || hoveredHelpText != helpText)
        {
            hideHoverValueReadout();
            hoveredHelpComponent = &satTypeButton;
            hoveredHelpText = helpText;
            helpHoverStartMs = juce::Time::getMillisecondCounter();
            helpVisible = false;
        }

        return;
    }

    auto* helpComponent = component;
    while (helpComponent != nullptr && helpComponent != this)
    {
        if (helpComponent->getProperties().contains("bqtHelpText"))
            break;

        helpComponent = helpComponent->getParentComponent();
    }

    if (helpComponent != nullptr && helpComponent != this)
    {
        if (hoveredHelpComponent != helpComponent)
        {
            hideHoverValueReadout();
            hoveredHelpComponent = helpComponent;
            hoveredHelpText = helpComponent->getProperties()["bqtHelpText"].toString();
            helpHoverStartMs = juce::Time::getMillisecondCounter();
            helpVisible = false;
        }

        return;
    }

    if (hoveredHelpComponent != nullptr)
        hideTopBarHelp();

    auto* sliderComponent = component;
    while (sliderComponent != nullptr && sliderComponent != this)
    {
        if (auto* slider = dynamic_cast<juce::Slider*>(sliderComponent))
        {
            if (hoveredReadoutSlider != slider)
            {
                hideHoverValueReadout();
                hoveredReadoutSlider = slider;
                hoverReadoutStartMs = juce::Time::getMillisecondCounter();
                hoverReadoutVisible = false;
            }

            return;
        }

        sliderComponent = sliderComponent->getParentComponent();
    }

    if (hoveredReadoutSlider != nullptr)
        hideHoverValueReadout();
}

void BqtAudioProcessorEditor::updateHoverValueReadout()
{
    if (hoveredReadoutSlider == nullptr || activeReadoutSlider != nullptr)
        return;

    if (hoveredReadoutSlider->isMouseButtonDown())
        return;

    if (! hoveredReadoutSlider->isMouseOver())
    {
        hideHoverValueReadout();
        return;
    }

    if (! hoverReadoutVisible
        && juce::Time::getMillisecondCounter() - hoverReadoutStartMs >= hoverValueDelayMs)
    {
        showReadout(*hoveredReadoutSlider, hoveredReadoutSlider->getTextFromValue(hoveredReadoutSlider->getValue()));
        hoverReadoutVisible = true;
    }
}

void BqtAudioProcessorEditor::hideHoverValueReadout()
{
    if (activeReadoutSlider == nullptr && hoveredHelpComponent == nullptr)
        hideReadout();

    hoveredReadoutSlider = nullptr;
    hoverReadoutVisible = false;
}

void BqtAudioProcessorEditor::updateTopBarHelp()
{
    if (hoveredHelpComponent == nullptr || activeReadoutSlider != nullptr)
        return;

    if (! hoveredHelpComponent->isMouseOver(true))
    {
        hideTopBarHelp();
        return;
    }

    if (! helpVisible
        && juce::Time::getMillisecondCounter() - helpHoverStartMs >= hoverHelpDelayMs)
    {
        showReadout(*hoveredHelpComponent, hoveredHelpText);
        helpVisible = true;
    }
}

void BqtAudioProcessorEditor::hideTopBarHelp()
{
    if (activeReadoutSlider == nullptr && ! hoverReadoutVisible)
        hideReadout();

    hoveredHelpComponent = nullptr;
    hoveredHelpText.clear();
    helpVisible = false;
}

void BqtAudioProcessorEditor::setTopBarHelp(juce::Component& component, const juce::String& text)
{
    if (auto* slider = dynamic_cast<juce::Slider*>(&component))
        slider->setTooltip({});
    else if (auto* combo = dynamic_cast<juce::ComboBox*>(&component))
        combo->setTooltip({});
    else if (auto* button = dynamic_cast<juce::Button*>(&component))
        button->setTooltip({});

    component.getProperties().set("bqtHelpText", text);
    if (dynamic_cast<juce::Slider*>(&component) == nullptr)
        component.addMouseListener(this, true);
}

void BqtAudioProcessorEditor::showReadout(juce::Component& target, const juce::String& text)
{
    if (text.isEmpty())
        return;

    const auto font = juce::Font(faceFont(16.5f, true));
    const auto width = static_cast<int>(std::ceil(getTextWidth(font, text) + 22.0f));
    constexpr int height = 31;
    const auto uiScale = static_cast<float>(getWidth()) / static_cast<float>(baseEditorWidth);
    auto editorTargetBounds = getLocalArea(&target, target.getLocalBounds()).toFloat();
    if (uiScale > 0.0f)
        editorTargetBounds = editorTargetBounds / uiScale;

    auto bounds = juce::Rectangle<int>(width, height)
                      .withCentre({ static_cast<int>(std::round(editorTargetBounds.getCentreX())),
                                    static_cast<int>(std::round(editorTargetBounds.getY() - 8.0f)) });

    if (bounds.getY() < 4)
        bounds.setY(static_cast<int>(std::round(editorTargetBounds.getBottom() + 8.0f)));

    readoutBubble.setText(text);
    readoutBubble.setBounds(bounds.constrainedWithin({ 0, 0, baseEditorWidth, baseEditorHeight }));
    readoutBubble.setVisible(true);
    readoutBubble.toFront(false);
}

void BqtAudioProcessorEditor::hideReadout()
{
    readoutBubble.setVisible(false);
}

void BqtAudioProcessorEditor::beginUndoableEdit()
{
    if (restoringPluginEditState || undoCaptureActive)
        return;

    pendingUndoState = capturePluginEditState();
    undoCaptureActive = true;
}

void BqtAudioProcessorEditor::finishUndoableEdit()
{
    if (! undoCaptureActive)
        return;

    undoCaptureActive = false;
    const auto nextState = capturePluginEditState();

    if (! bqt::editor::snapshotsMatch(pendingUndoState, nextState))
    {
        undoStack.push_back(pendingUndoState);
        redoStack.clear();

        constexpr size_t maxUndoSteps = 64;
        if (undoStack.size() > maxUndoSteps)
            undoStack.erase(undoStack.begin());
    }

    pendingUndoState.clear();
}

void BqtAudioProcessorEditor::cancelUndoableEdit()
{
    undoCaptureActive = false;
    pendingUndoState.clear();
}

bool BqtAudioProcessorEditor::undoLastPluginEdit()
{
    finishUndoableEdit();

    if (undoStack.empty())
        return false;

    const auto currentState = capturePluginEditState();
    auto previousState = undoStack.back();
    undoStack.pop_back();
    redoStack.push_back(currentState);
    restorePluginEditState(previousState);
    return true;
}

bool BqtAudioProcessorEditor::redoLastPluginEdit()
{
    finishUndoableEdit();

    if (redoStack.empty())
        return false;

    const auto currentState = capturePluginEditState();
    auto nextState = redoStack.back();
    redoStack.pop_back();
    undoStack.push_back(currentState);
    restorePluginEditState(nextState);
    return true;
}

bqt::editor::ParameterSnapshot BqtAudioProcessorEditor::capturePluginEditState() const
{
    bqt::editor::ParameterSnapshot snapshot;
    snapshot.reserve(static_cast<size_t>(undoableParameterIds.size()));

    for (const auto& id : undoableParameterIds)
        if (auto* parameter = audioProcessor.state().getParameter(id))
            snapshot.emplace_back(id, parameter->getValue());

    return snapshot;
}

void BqtAudioProcessorEditor::restorePluginEditState(const bqt::editor::ParameterSnapshot& snapshot)
{
    const juce::ScopedValueSetter<bool> scopedRestore(restoringPluginEditState, true);
    const juce::ScopedValueSetter<bool> scopedMirror(isMirroringLinkedControl, true);

    endLinkedMirrorGestures();

    // Only what the step changed: rewriting every parameter sent the host a gesture (and an
    // automation write) for each untouched control.
    for (const auto& [id, value] : bqt::editor::changedSnapshotEntries(snapshot, capturePluginEditState()))
    {
        if (auto* parameter = audioProcessor.state().getParameter(id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(value);
            parameter->endChangeGesture();
        }
    }

    inputTrimCompensationStart = inputTrim.getValue();
    for (size_t index = 0; index < sideControls.size(); ++index)
        outputTrimCompensationStart[index] = sideControls[index].outputTrim.getValue();
    repaint();
}
