#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "BqtParameterIds.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

// Editor decisions kept free of components so the chain tests can cover them headlessly.
namespace bqt::editor
{
using ParameterSnapshot = std::vector<std::pair<juce::String, float>>;

// Normalised-value tolerance for treating a snapshot entry as unchanged.
constexpr float snapshotTolerance = 0.00001f;

// Every parameter the editor's undo stack records: the processor's parameters minus workflow
// state, so undo never flips bypass or oversampling back under the user.
inline juce::StringArray undoableParameterIds(const juce::AudioProcessor& processor)
{
    juce::StringArray ids;
    for (const auto* parameter : processor.getParameters())
        if (const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(parameter))
            if (! workflowParameterIds().contains(withId->paramID))
                ids.add(withId->paramID);

    return ids;
}

inline bool snapshotsMatch(const ParameterSnapshot& a, const ParameterSnapshot& b)
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].first != b[i].first || std::abs(a[i].second - b[i].second) > snapshotTolerance)
            return false;

    return true;
}

// The entries of `target` that differ from `current` (or are missing from it): what a restore
// actually has to write. Unchanged parameters get no write and no host gesture.
inline ParameterSnapshot changedSnapshotEntries(const ParameterSnapshot& target, const ParameterSnapshot& current)
{
    ParameterSnapshot changed;
    for (const auto& entry : target)
    {
        const auto match = std::find_if(current.begin(), current.end(),
                                        [&entry](const auto& c) { return c.first == entry.first; });

        if (match == current.end() || std::abs(match->second - entry.second) > snapshotTolerance)
            changed.push_back(entry);
    }

    return changed;
}

// Tracks which controls are inside a genuine user gesture (drag, wheel step, double-click reset,
// accessibility set). JUCE nests these notifications -- a double-click reset arrives inside the
// mouse-down drag -- so this counts depth rather than holding a bool that would clear mid-drag.
class UserGestureTracker
{
public:
    void begin(const void* control) { ++depths[control]; }

    // Returns true when the outermost gesture on this control has ended.
    bool end(const void* control)
    {
        const auto found = depths.find(control);
        if (found == depths.end())
            return true;

        if (--found->second > 0)
            return false;

        depths.erase(found);
        return true;
    }

    bool isActive(const void* control) const { return depths.count(control) != 0; }

private:
    std::map<const void*, int> depths;
};

// A linked control is mirrored only for a user gesture on the source itself. Value changes that
// arrive through the parameter attachment (host automation, preset loads, setStateInformation,
// undo) must not be mirrored or written back to the host, even while the mouse rests on the knob.
inline bool shouldMirrorLinkedEdit(bool linkActive, bool sourceInUserGesture, bool alreadyMirroring)
{
    return linkActive && sourceInUserGesture && ! alreadyMirroring;
}

// The sat type button toggles side A and writes the result to both sides.
inline int nextSatTypeIndex(int sideATypeIndex)
{
    return sideATypeIndex == 0 ? 1 : 0;
}
} // namespace bqt::editor
