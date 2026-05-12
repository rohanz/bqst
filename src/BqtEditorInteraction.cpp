#include "PluginEditor.h"

#include "BqtEditorStyle.h"

#include <array>
#include <cmath>

namespace
{
using namespace bqst::ui;

constexpr std::array<const char*, 28> undoableParameterIds {
    "eqMode", "satMode", "osRealtime", "osRender", "inputTrim", "autoGain",
    "eqBypass", "satBypass", "eqLink", "satLink", "bypass", "vintage",
    "aLowGain", "aLowFreq", "aHighGain", "aHighFreq", "aDrive", "aSatType", "aMix", "aOutputTrim",
    "bLowGain", "bLowFreq", "bHighGain", "bHighFreq", "bDrive", "bSatType", "bMix", "bOutputTrim"
};

bool snapshotsMatch(const std::vector<std::pair<juce::String, float>>& a,
                    const std::vector<std::pair<juce::String, float>>& b)
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].first != b[i].first || std::abs(a[i].second - b[i].second) > 0.00001f)
            return false;

    return true;
}
} // namespace

void BqtAudioProcessorEditor::timerCallback()
{
    const auto eqIsIn = audioProcessor.state().getRawParameterValue("eqBypass")->load() < 0.5f;
    const auto satIsIn = audioProcessor.state().getRawParameterValue("satBypass")->load() < 0.5f;
    const auto bypassIsOn = audioProcessor.state().getRawParameterValue("bypass")->load() > 0.5f;
    eqBypass.setToggleState(eqIsIn, juce::dontSendNotification);
    satBypass.setToggleState(satIsIn, juce::dontSendNotification);
    bypass.setToggleState(bypassIsOn, juce::dontSendNotification);
    requestRackBypassVisualState(bypassIsOn);

    for (auto& controls : sideControls)
        controls.satTypeButton.setToggleState(controls.satType.getSelectedItemIndex() == 1, juce::dontSendNotification);

    if (! rackComponent.isBypassed())
    {
        meterA.updateLevel();
        meterB.updateLevel();
        rackComponent.repaint();
    }

    updateLinkedControlStates();
    updateDynamicTooltips();

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
        const auto deltaDb = currentInputTrim - previousInputTrimForCompensation;
        previousInputTrimForCompensation = currentInputTrim;

        if (inputTrim.isMouseButtonDown()
            && juce::ModifierKeys::currentModifiers.isCtrlDown()
            && std::abs(deltaDb) > 0.0)
        {
            const juce::ScopedValueSetter<bool> scopedMirror(isMirroringLinkedControl, true);
            for (auto& controls : sideControls)
            {
                const auto compensatedValue = juce::jlimit(controls.outputTrim.getMinimum(),
                                                          controls.outputTrim.getMaximum(),
                                                          controls.outputTrim.getValue() - deltaDb);
                controls.outputTrim.setValue(compensatedValue, juce::sendNotificationSync);
            }
        }
    }

    updateDragValueReadout(*slider);
}

bool BqtAudioProcessorEditor::shouldMirrorLinkedControls(const char* linkParameterId) const
{
    const auto linked = audioProcessor.state().getRawParameterValue(linkParameterId)->load() > 0.5f;
    const auto controlDown = juce::ModifierKeys::currentModifiers.isCtrlDown();
    return linked != controlDown;
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
    auto& left = sideControls[0];
    auto& right = sideControls[1];

    if (shouldMirrorLinkedControls("eqLink"))
    {
        if (&slider == &left.lowGain)       beginMirroredParameterGesture("bLowGain");
        else if (&slider == &right.lowGain) beginMirroredParameterGesture("aLowGain");
        else if (&slider == &left.highGain) beginMirroredParameterGesture("bHighGain");
        else if (&slider == &right.highGain) beginMirroredParameterGesture("aHighGain");
    }

    if (shouldMirrorLinkedControls("satLink"))
    {
        if (&slider == &left.drive)            beginMirroredParameterGesture("bDrive");
        else if (&slider == &right.drive)      beginMirroredParameterGesture("aDrive");
        else if (&slider == &left.mix)         beginMirroredParameterGesture("bMix");
        else if (&slider == &right.mix)        beginMirroredParameterGesture("aMix");
        else if (&slider == &left.outputTrim)  beginMirroredParameterGesture("bOutputTrim");
        else if (&slider == &right.outputTrim) beginMirroredParameterGesture("aOutputTrim");
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
    if (! shouldMirrorLinkedControls("eqLink"))
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

void BqtAudioProcessorEditor::commitMirroredSteppedFrequency(juce::Slider& slider)
{
    if (! shouldMirrorLinkedControls("eqLink"))
        return;

    auto& left = sideControls[0];
    auto& right = sideControls[1];

    if (&slider == &left.lowFreq)
        commitParameterGesture("bLowFreq", static_cast<float>(right.lowFreq.getValue()));
    else if (&slider == &right.lowFreq)
        commitParameterGesture("aLowFreq", static_cast<float>(left.lowFreq.getValue()));
    else if (&slider == &left.highFreq)
        commitParameterGesture("bHighFreq", static_cast<float>(right.highFreq.getValue()));
    else if (&slider == &right.highFreq)
        commitParameterGesture("aHighFreq", static_cast<float>(left.highFreq.getValue()));
}

void BqtAudioProcessorEditor::sliderDragStarted(juce::Slider* slider)
{
    if (slider == nullptr)
        return;

    beginUndoableEdit();
    beginLinkedMirrorGestureFor(*slider);
    hideHoverValueReadout();

    if (slider == &inputTrim)
        previousInputTrimForCompensation = inputTrim.getValue();

    updateDragValueReadout(*slider);
}

void BqtAudioProcessorEditor::sliderDragEnded(juce::Slider* slider)
{
    if (slider != nullptr)
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
            }

            helpHoverStartMs = juce::Time::getMillisecondCounter();
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
        }

        hoverReadoutStartMs = juce::Time::getMillisecondCounter();

        if (hoverReadoutVisible)
            hideHoverValueReadout();
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

            previousInputTrimForCompensation = 0.0;
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

    auto& mainControls = sideControls[0];
    if (mainControls.satTypeButton.isParentOf(component) || component == &mainControls.satTypeButton)
    {
        const auto gritSelected = mainControls.satType.getSelectedItemIndex() == 1;
        auto helpText = mainControls.satTypeButton.getProperties()[gritSelected ? "bqtGritHelp" : "bqtCreamHelp"].toString();

        if (hoveredHelpComponent != &mainControls.satTypeButton || hoveredHelpText != helpText)
        {
            hideHoverValueReadout();
            hoveredHelpComponent = &mainControls.satTypeButton;
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

    constexpr juce::uint32 hoverDelayMs = 300;
    if (! hoverReadoutVisible
        && juce::Time::getMillisecondCounter() - hoverReadoutStartMs >= hoverDelayMs)
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

    constexpr juce::uint32 hoverDelayMs = 300;
    if (! helpVisible
        && juce::Time::getMillisecondCounter() - helpHoverStartMs >= hoverDelayMs)
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

    if (! snapshotsMatch(pendingUndoState, nextState))
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

std::vector<std::pair<juce::String, float>> BqtAudioProcessorEditor::capturePluginEditState() const
{
    std::vector<std::pair<juce::String, float>> snapshot;
    snapshot.reserve(undoableParameterIds.size());

    for (const auto* id : undoableParameterIds)
        if (auto* parameter = audioProcessor.state().getParameter(id))
            snapshot.emplace_back(id, parameter->getValue());

    return snapshot;
}

void BqtAudioProcessorEditor::restorePluginEditState(const std::vector<std::pair<juce::String, float>>& snapshot)
{
    const juce::ScopedValueSetter<bool> scopedRestore(restoringPluginEditState, true);
    const juce::ScopedValueSetter<bool> scopedMirror(isMirroringLinkedControl, true);

    endLinkedMirrorGestures();

    for (const auto& [id, value] : snapshot)
    {
        if (auto* parameter = audioProcessor.state().getParameter(id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(value);
            parameter->endChangeGesture();
        }
    }

    previousInputTrimForCompensation = inputTrim.getValue();
    updateLinkedControlStates();
    requestRackBypassVisualState(audioProcessor.state().getRawParameterValue("bypass")->load() > 0.5f);
    repaint();
}

void BqtAudioProcessorEditor::updateDynamicTooltips()
{
}

void BqtAudioProcessorEditor::updateLinkedControlStates()
{
    auto& right = sideControls[1];

    auto keepVisible = [](juce::Component& component)
    {
        component.setEnabled(true);
        component.setAlpha(1.0f);
    };

    keepVisible(right.lowGain);
    keepVisible(right.lowFreq);
    keepVisible(right.highGain);
    keepVisible(right.highFreq);
    keepVisible(right.lowGainLabel);
    keepVisible(right.lowFreqLabel);
    keepVisible(right.highGainLabel);
    keepVisible(right.highFreqLabel);
    keepVisible(right.drive);
    keepVisible(right.satType);
    keepVisible(right.satTypeButton);
    keepVisible(right.mix);
    keepVisible(right.outputTrim);
    keepVisible(right.driveLabel);
    keepVisible(right.satTypeLabel);
    keepVisible(right.mixLabel);
    keepVisible(right.outputTrimLabel);
}
