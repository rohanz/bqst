#pragma once

#include <array>
#include <cmath>

namespace bqt
{
enum class ChannelMode
{
    lr = 0,
    midSide = 1
};

enum class SaturationType
{
    density = 0,
    transformer = 1
};

enum class OversamplingChoice
{
    off = 0,
    x2 = 1,
    x4 = 2,
    x8 = 3
};

inline constexpr std::array<float, 8> lowShelfFrequenciesHz {
    74.0f, 84.0f, 98.0f, 116.0f, 131.0f, 166.0f, 230.0f, 361.0f
};

inline constexpr std::array<float, 8> highShelfFrequenciesHz {
    1600.0f, 1800.0f, 2100.0f, 2500.0f, 3400.0f, 4800.0f, 7100.0f, 18000.0f
};

inline float densitySaturate(float sample, float drive01)
{
    if (drive01 <= 0.0f)
        return sample;

    const auto evenBias = 0.035f + drive01 * 0.11f;
    const auto oddWeight = 0.18f + drive01 * 0.42f;
    const auto softKnee = 1.05f + drive01 * 1.65f;
    const auto biased = sample + evenBias * sample * sample;
    const auto shaped = std::tanh(biased * softKnee + oddWeight * sample * sample * sample);
    const auto dcOffset = std::tanh(evenBias * 0.015f);
    const auto blend = 0.18f + drive01 * 0.62f;

    return sample * (1.0f - blend) + (shaped - dcOffset) * blend;
}

inline float transformerSaturate(float sample, float drive01)
{
    if (drive01 <= 0.0f)
        return sample;

    const auto drive = 1.0f + drive01 * 7.0f;
    const auto biased = sample * drive + 0.08f * drive01;
    const auto shaped = std::tanh(biased) - std::tanh(0.08f * drive01);
    const auto softened = shaped * (1.0f - 0.08f * drive01);

    return sample * (1.0f - drive01) + softened * drive01;
}

inline float saturationAutoGain(float drive01, SaturationType type)
{
    if (drive01 <= 0.0f)
        return 1.0f;

    const auto amount = type == SaturationType::density ? 0.38f : 0.30f;
    return 1.0f / (1.0f + drive01 * amount);
}
} // namespace bqt
