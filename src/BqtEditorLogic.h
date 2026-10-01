#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "BqtParameterIds.h"

#include <algorithm>
#include <cmath>
#include <functional>
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

// A control is mirrored onto the other side only for a user gesture on the source itself. Value
// changes that arrive through the parameter attachment (host automation, preset loads,
// setStateInformation, undo) must not be mirrored or written back to the host, even while the
// mouse rests on the knob.
inline bool shouldMirrorLinkedEdit(bool mirrorActive, bool sourceInUserGesture, bool alreadyMirroring)
{
    return mirrorActive && sourceInUserGesture && ! alreadyMirroring;
}

// The two link groups. Sat type is not in either: its one button always sets both sides.
enum class LinkGroup { eq, sat };

inline const char* linkParameterId(LinkGroup group)
{
    return group == LinkGroup::eq ? "eqLink" : "satLink";
}

// Per-side parameter suffixes each link covers.
inline const juce::StringArray& linkGroupSuffixes(LinkGroup group)
{
    static const juce::StringArray eq { "LowGain", "LowFreq", "HighGain", "HighFreq" };
    static const juce::StringArray sat { "Drive", "Mix", "OutputTrim" };
    return group == LinkGroup::eq ? eq : sat;
}

// Linking happens in the processor (side B follows side A), so a linked edit writes only side A
// and the editor mirrors nothing. Ctrl-drag moves both sides only while the group is unlinked.
inline bool ctrlMirrorsBothSides(bool groupLinked, bool controlDown)
{
    return controlDown && ! groupLinked;
}

// The parameter a side's control is attached to: while linked, side B displays and edits side A's.
inline juce::String attachedParameterId(int sideIndex, const juce::String& suffix, bool groupLinked)
{
    return sidePrefix(groupLinked ? 0 : sideIndex) + suffix;
}

// Writes for unlinking a group from the UI: side B takes side A's current (normalised) values, so
// nothing jumps when B starts playing its own parameters again. A and B share ranges.
inline ParameterSnapshot unlinkCopyWrites(LinkGroup group, const std::function<float(const juce::String&)>& normalisedValueOf)
{
    ParameterSnapshot writes;
    for (const auto& suffix : linkGroupSuffixes(group))
        writes.emplace_back(sidePrefix(1) + suffix, normalisedValueOf(sidePrefix(0) + suffix));

    return writes;
}

// The sat type button toggles side A and writes the result to both sides.
inline int nextSatTypeIndex(int sideATypeIndex)
{
    return sideATypeIndex == 0 ? 1 : 0;
}
} // namespace bqt::editor
