#pragma once

#include <juce_core/juce_core.h>

namespace bqt
{
// Per-side parameter IDs are "a<Name>" (L/M) and "b<Name>" (R/S).
inline juce::String sidePrefix(int sideIndex)
{
    return sideIndex == 0 ? "a" : "b";
}
} // namespace bqt
