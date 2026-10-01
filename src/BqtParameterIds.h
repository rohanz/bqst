#pragma once

#include <juce_core/juce_core.h>

#include "BqtDsp.h"

namespace bqt
{
// Per-side parameter IDs are "a<Name>" (L/M) and "b<Name>" (R/S).
inline juce::String sidePrefix(int sideIndex)
{
    return sideIndex == 0 ? "a" : "b";
}

// Workflow state rather than sound: kept out of presets and the editor's undo stack.
inline const juce::StringArray& workflowParameterIds()
{
    static const juce::StringArray ids { "osRealtime", "osRender", "eqBypass", "satBypass", "bypass" };
    return ids;
}

// Choice names of the LowFreq/HighFreq parameters, one per entry of the shelf frequency tables.
template <std::size_t size>
juce::StringArray shelfFrequencyLabels(const std::array<float, size>& frequenciesHz)
{
    juce::StringArray labels;
    for (const auto hz : frequenciesHz)
        labels.add(shelfFrequencyLabel(hz));
    return labels;
}

inline juce::StringArray lowShelfFrequencyLabels()  { return shelfFrequencyLabels(lowShelfFrequenciesHz); }
inline juce::StringArray highShelfFrequencyLabels() { return shelfFrequencyLabels(highShelfFrequenciesHz); }
} // namespace bqt
