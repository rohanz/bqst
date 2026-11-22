#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <cmath>

namespace bqt::editor
{
constexpr int designWidth = 865;
constexpr int designHeight = 874;
constexpr int minimumWidth = 649;
constexpr int maximumWidth = 1730;

inline int heightForWidth(int width)
{
    return juce::roundToInt(static_cast<double>(width) * designHeight / designWidth);
}

// Width is session-only metadata, never an automatable audio parameter.
inline int restoredWidth(const juce::ValueTree& state)
{
    if (state.hasProperty("editorWidth"))
    {
        // XML properties arrive as strings. Reject a valid numeric prefix followed by
        // corrupt text rather than silently accepting it as a saved window width.
        const auto text = state.getProperty("editorWidth").toString().trim();
        auto end = text.getCharPointer();
        const auto width = juce::CharacterFunctions::readDoubleValue(end);
        if (end.isEmpty() && std::isfinite(width) && width > 0.0)
            return juce::roundToInt(juce::jlimit(static_cast<double>(minimumWidth),
                                               static_cast<double>(maximumWidth), width));
    }
    const auto choice = static_cast<double>(state.getProperty("editorScale", 2));
    const double scale = choice == 1 ? 0.75 : choice == 3 ? 1.25 : choice == 4 ? 1.5 : 1.0;
    return juce::roundToInt(designWidth * scale);
}

inline void storeWidth(juce::ValueTree state, int width)
{
    state.setProperty("editorWidth", juce::jlimit(minimumWidth, maximumWidth, width), nullptr);
}
} // namespace bqt::editor
