#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace bqt::editor
{
using ParameterSnapshot = std::vector<std::pair<juce::String, float>>;

// Normalised-value tolerance for treating a snapshot entry as unchanged.
constexpr float snapshotTolerance = 0.00001f;

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

}
