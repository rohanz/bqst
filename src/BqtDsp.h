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

    const auto push = drive01 * drive01;
    const auto evenBias = 0.018f + drive01 * 0.065f + push * 0.025f;
    const auto oddWeight = 0.07f + drive01 * 0.22f + push * 0.11f;
    const auto softKnee = 0.72f + drive01 * 0.78f + push * 0.35f;
    const auto biased = sample + evenBias * sample * sample;
    const auto shaped = std::tanh(biased * softKnee + oddWeight * sample * sample * sample);
    const auto dcOffset = std::tanh(evenBias * 0.015f);
    const auto blend = 0.08f + drive01 * 0.42f + push * 0.12f;

    return sample * (1.0f - blend) + (shaped - dcOffset) * blend;
}

inline float transformerSaturate(float sample, float drive01)
{
    if (drive01 <= 0.0f)
        return sample;

    const auto push = drive01 * drive01;
    const auto drive = 0.85f + drive01 * 2.1f + push * 0.65f;
    const auto bias = 0.025f * drive01 + push * 0.015f;
    const auto biased = sample * drive + bias;
    const auto shaped = std::tanh(biased) - std::tanh(bias);
    const auto rounded = shaped - (0.04f * drive01 + 0.025f * push) * shaped * shaped * shaped;
    const auto blend = 0.10f + drive01 * 0.45f + push * 0.10f;

    return sample * (1.0f - blend) + rounded * blend;
}

inline float saturationAutoGain(float drive01, SaturationType type)
{
    if (drive01 <= 0.0f)
        return 1.0f;

    const auto amount = type == SaturationType::density ? 0.16f : 0.14f;
    return 1.0f / (1.0f + drive01 * amount);
}
} // namespace bqt
