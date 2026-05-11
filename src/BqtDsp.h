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
    const auto maxPush = push * drive01;
    const auto evenBias = 0.014f + drive01 * 0.050f + push * 0.018f;
    const auto oddWeight = 0.045f + drive01 * 0.145f + push * 0.120f + maxPush * 0.180f;
    const auto softKnee = 0.62f + drive01 * 0.58f + push * 0.38f + maxPush * 0.55f;
    const auto biased = sample + evenBias * sample * sample;
    const auto driven = biased * softKnee + oddWeight * sample * sample * sample;
    const auto shaped = std::tanh(driven) * (1.0f + 0.10f * drive01 + 0.12f * maxPush);
    const auto dcOffset = std::tanh(evenBias * 0.015f);
    const auto blend = 0.07f + drive01 * 0.32f + push * 0.13f + maxPush * 0.12f;

    return sample * (1.0f - blend) + (shaped - dcOffset) * blend;
}

inline float transformerSaturate(float sample, float drive01)
{
    if (drive01 <= 0.0f)
        return sample;

    const auto push = drive01 * drive01;
    const auto maxPush = push * drive01;
    const auto drive = 0.82f + drive01 * 1.65f + push * 0.82f + maxPush * 1.15f;
    const auto bias = 0.018f * drive01 + push * 0.010f + maxPush * 0.018f;
    const auto biased = sample * drive + bias;
    const auto shaped = std::tanh(biased * 0.86f) / std::tanh(0.86f) - std::tanh(bias * 0.86f) / std::tanh(0.86f);
    const auto rounded = shaped - (0.025f * drive01 + 0.014f * push + 0.020f * maxPush) * shaped * shaped * shaped;
    const auto blend = 0.09f + drive01 * 0.34f + push * 0.12f + maxPush * 0.14f;

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
