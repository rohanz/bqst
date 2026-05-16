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

inline float densitySaturateLegacy(float sample, float drive01)
{
    if (drive01 <= 0.0f)
        return sample;

    const auto push = drive01 * drive01;
    const auto maxPush = push * drive01;
    const auto asymmetry = drive01 * (0.012f + drive01 * 0.048f + push * 0.028f);
    const auto oddWeight = drive01 * (0.040f + drive01 * 0.135f + push * 0.105f + maxPush * 0.165f);
    const auto softKnee = 0.82f + drive01 * 0.38f + push * 0.36f + maxPush * 0.50f;
    const auto driven = sample * softKnee + oddWeight * sample * sample * sample + asymmetry;
    const auto shaped = (std::tanh(driven) - std::tanh(asymmetry)) * (1.0f + 0.09f * drive01 + 0.10f * maxPush);
    const auto blend = drive01 * 0.38f + push * 0.13f + maxPush * 0.12f;

    return sample * (1.0f - blend) + shaped * blend;
}

inline float densitySaturate(float sample, float drive01)
{
    if (drive01 <= 0.0f)
        return sample;

    const auto push = drive01 * drive01;
    const auto maxPush = push * drive01;
    const auto asymmetry = drive01 * (0.016f + drive01 * 0.045f + push * 0.040f);
    const auto oddWeight = drive01 * (0.032f + drive01 * 0.095f + push * 0.115f + maxPush * 0.135f);
    const auto softKnee = 0.80f + drive01 * 0.42f + push * 0.36f + maxPush * 0.60f;
    const auto driven = sample * softKnee + oddWeight * sample * sample * sample + asymmetry;
    const auto shaped = (std::tanh(driven) - std::tanh(asymmetry)) * (1.0f + 0.07f * drive01 + 0.13f * maxPush);
    const auto blend = drive01 * 0.39f + push * 0.16f + maxPush * 0.15f;

    return sample * (1.0f - blend) + shaped * blend;
}

inline float transformerSaturate(float sample, float drive01)
{
    if (drive01 <= 0.0f)
        return sample;

    const auto push = drive01 * drive01;
    const auto maxPush = push * drive01;
    const auto drive = 0.92f + drive01 * 1.55f + push * 0.82f + maxPush * 1.15f;
    const auto bias = 0.018f * drive01 + push * 0.010f + maxPush * 0.018f;
    const auto biased = sample * drive + bias;
    const auto shaped = std::tanh(biased * 0.86f) / std::tanh(0.86f) - std::tanh(bias * 0.86f) / std::tanh(0.86f);
    const auto rounded = shaped - (0.025f * drive01 + 0.014f * push + 0.020f * maxPush) * shaped * shaped * shaped;
    const auto blend = drive01 * 0.43f + push * 0.12f + maxPush * 0.14f;

    return sample * (1.0f - blend) + rounded * blend;
}

inline float saturationAutoGain(float drive01, SaturationType type)
{
    if (drive01 <= 0.0f)
        return 1.0f;

    const auto amount = type == SaturationType::density ? 2.04f : 2.71f;
    const auto exponent = type == SaturationType::density ? 1.67f : 1.48f;
    const auto shapedDrive = std::pow(drive01, exponent);
    return 1.0f / (1.0f + shapedDrive * amount);
}
} // namespace bqt
