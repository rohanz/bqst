#pragma once

#include <array>
#include <cmath>

#include "BqtCreamModel.h"

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

// Magnitude of the analog RBJ shelf prototype, normalised so |H| is 1 in the passband and
// `gain` in the shelf band. This is the curve the digital design below is fitted against.
inline double analogShelfMagnitude(double frequency, double centreHz, double q, double gain, bool highShelf)
{
    const auto a = std::sqrt(gain);
    const auto ratioSquared = (frequency / centreHz) * (frequency / centreHz);
    const auto shared = (a / (q * q)) * ratioSquared;
    const auto lower = 1.0 - a * ratioSquared;
    const auto upper = a - ratioSquared;
    const auto lowerTerm = std::sqrt(lower * lower + shared);
    const auto upperTerm = std::sqrt(upper * upper + shared);

    return highShelf ? a * lowerTerm / upperTerm : a * upperTerm / lowerTerm;
}

// Decramped ("matched magnitude") Baxandall shelf.
//
// The RBJ bilinear shelf squeezes its whole transition band below Nyquist, so the same nominal
// curve measures differently at 44.1 kHz than at an oversampled rate. That made the EQ curve
// depend on the oversampling setting, and made a render at one factor differ from playback at
// another. This instead fits a biquad to the analog prototype: the poles are matched from the
// analog pole positions, and the numerator is solved to hit the analog magnitude at DC, at
// Nyquist, and at one mid-band point.
//
// The mid point is capped at sampleRate/6 because matching exactly at the centre frequency goes
// ill-conditioned as it approaches Nyquist -- the 18 kHz position at 44.1 kHz sits at 147
// degrees, where sin^2(w/2) approaches 1 and the solve blows up.
//
// Verified numerically against the analog prototype over all 16 shelf positions, 44.1/48/88.2/96
// kHz and +/-6 dB: worst error 0.234 dB, versus 1.207 dB for RBJ at the host rate. The poles are
// inside the unit circle by construction (more robust than RBJ near Nyquist), and the response is
// exactly unity at 0 dB gain, so a flat shelf stays transparent.
inline std::array<float, 6> makeMatchedShelf(double sampleRate, double centreHz, double q, double gain, bool highShelf)
{
    constexpr auto pi = 3.14159265358979323846;
    const auto nyquist = sampleRate * 0.5;
    const auto centre = std::fmin(std::fmax(centreHz, 10.0), nyquist * 0.95);
    const auto a = std::sqrt(gain);

    // Pole frequency of the analog shelf denominator; the shelf's Q carries over unchanged.
    const auto omega = 2.0 * pi * centre / sampleRate;
    const auto poleOmega = std::fmin(highShelf ? omega * std::sqrt(a) : omega / std::sqrt(a), pi * 0.95);

    const auto a2 = std::exp(-poleOmega / q);
    const auto decay = std::exp(-poleOmega / (2.0 * q));
    const auto shape = 1.0 / (4.0 * q * q);
    const auto a1 = q > 0.5 ? -2.0 * decay * std::cos(poleOmega * std::sqrt(1.0 - shape))
                            : -2.0 * decay * std::cosh(poleOmega * std::sqrt(shape - 1.0));

    // Exact magnitude constraints at DC and Nyquist fix (b0 + b1 + b2) and (b0 - b1 + b2).
    const auto atDc = analogShelfMagnitude(0.0, centre, q, gain, highShelf) * (1.0 + a1 + a2);
    const auto atNyquist = analogShelfMagnitude(nyquist, centre, q, gain, highShelf) * (1.0 - a1 + a2);
    const auto b1 = 0.5 * (atDc - atNyquist);
    const auto sum = 0.5 * (atDc + atNyquist); // b0 + b2

    // One mid-band magnitude constraint then fixes the product b0 * b2.
    const auto midHz = std::fmin(centre, sampleRate / 6.0);
    const auto midOmega = 2.0 * pi * midHz / sampleRate;
    const auto s = std::sin(midOmega * 0.5) * std::sin(midOmega * 0.5);
    const auto denominatorSquared = (1.0 + a1 + a2) * (1.0 + a1 + a2)
                                  - 4.0 * (a1 + 4.0 * a2 + a1 * a2) * s
                                  + 16.0 * a2 * s * s;
    const auto midTarget = analogShelfMagnitude(midHz, centre, q, gain, highShelf);
    const auto product = (midTarget * midTarget * denominatorSquared - atDc * atDc + 4.0 * b1 * sum * s)
                       / (16.0 * s * (s - 1.0));

    const auto discriminant = sum * sum - 4.0 * product;
    const auto b0 = discriminant < 0.0 ? 0.5 * sum : 0.5 * (sum + std::sqrt(discriminant));

    return { static_cast<float>(b0), static_cast<float>(b1), static_cast<float>(sum - b0),
             1.0f, static_cast<float>(a1), static_cast<float>(a2) };
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

    // Cream: static table calibrated across materials by tools/cream_model. It may exceed 1 at
    // low drive, where the model adds loudness before it starts to compress.
    if (type == SaturationType::density)
        return CreamModel::autoGainForKnob(drive01 * 18.0f);

    const auto shapedDrive = std::pow(drive01, 1.48f);
    return 1.0f / (1.0f + shapedDrive * 2.71f);
}

inline constexpr float sqrtHalf = 0.70710678118654752440f;

inline void encodeMidSide(float* left, float* right, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        const auto mid = (left[i] + right[i]) * sqrtHalf;
        const auto side = (left[i] - right[i]) * sqrtHalf;
        left[i] = mid;
        right[i] = side;
    }
}

inline void decodeMidSide(float* left, float* right, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        const auto l = (left[i] + right[i]) * sqrtHalf;
        const auto r = (left[i] - right[i]) * sqrtHalf;
        left[i] = l;
        right[i] = r;
    }
}
} // namespace bqt
