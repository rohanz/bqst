// Unit tests for the pure DSP helpers in BqtDsp.h.
//
// These cover the invariants the rest of the plugin (and AGENTS.md) rely on: saturation is an
// exact bypass at zero drive and ramps continuously from there, autogain is unity at zero drive
// and decreases monotonically, and nothing produces NaN/Inf or unbounded output for sane input.
//
// Deliberately dependency-free (BqtDsp.h only uses <cmath>/<array>) so the test target builds
// and runs in milliseconds without linking JUCE.

#include "../src/BqtDsp.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <limits>

namespace
{
int failures = 0;

void check(bool condition, const char* what)
{
    if (! condition)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

float saturate(bqt::SaturationType type, float sample, float drive01)
{
    return type == bqt::SaturationType::density ? bqt::densitySaturate(sample, drive01)
                                                : bqt::transformerSaturate(sample, drive01);
}

constexpr bqt::SaturationType bothTypes[] { bqt::SaturationType::density, bqt::SaturationType::transformer };

// Magnitude of a biquad {b0,b1,b2,a0,a1,a2} at a given frequency.
double biquadMagnitude(const std::array<float, 6>& c, double frequency, double sampleRate)
{
    const std::complex<double> z = std::polar(1.0, -2.0 * 3.14159265358979323846 * frequency / sampleRate);
    const auto num = static_cast<double>(c[0]) + static_cast<double>(c[1]) * z + static_cast<double>(c[2]) * z * z;
    const auto den = static_cast<double>(c[3]) + static_cast<double>(c[4]) * z + static_cast<double>(c[5]) * z * z;
    return std::abs(num / den);
}

constexpr double shelfQ = 0.38; // must track baxShelfQ in BqtProcessorDsp.cpp
constexpr double testRates[] { 44100.0, 48000.0, 88200.0, 96000.0 };
constexpr double testGainsDb[] { -6.0, -3.0, -1.0, 1.0, 3.0, 6.0 };
} // namespace

int main()
{
    // Autogain: exactly unity at zero drive, finite, in (0, 1], and monotonically non-increasing.
    for (auto type : bothTypes)
    {
        check(bqt::saturationAutoGain(0.0f, type) == 1.0f, "autogain is unity at zero drive");

        float previous = 1.0f;
        for (int i = 1; i <= 200; ++i)
        {
            const auto drive01 = static_cast<float>(i) / 200.0f;
            const auto gain = bqt::saturationAutoGain(drive01, type);
            check(std::isfinite(gain), "autogain is finite");
            check(gain > 0.0f && gain <= 1.0f, "autogain stays in (0, 1]");
            check(gain <= previous + 1.0e-6f, "autogain is monotonically non-increasing");
            previous = gain;
        }
    }

    // Saturation is an exact bypass at zero drive (so 0 dB drive is truly transparent).
    for (auto type : bothTypes)
        for (float sample = -2.0f; sample <= 2.0f; sample += 0.05f)
            check(std::abs(saturate(type, sample, 0.0f) - sample) == 0.0f, "saturation is exact bypass at zero drive");

    // Saturation output stays finite and bounded for finite input across the whole drive range.
    for (auto type : bothTypes)
        for (float drive01 = 0.0f; drive01 <= 1.0f; drive01 += 0.02f)
            for (float sample = -4.0f; sample <= 4.0f; sample += 0.02f)
            {
                const auto out = saturate(type, sample, drive01);
                check(std::isfinite(out), "saturation output is finite");
                check(std::abs(out) < 100.0f, "saturation output is bounded");
            }

    // Curves ramp continuously from zero drive: a tiny drive must stay close to the input
    // (no abrupt minimum saturation switching on, per AGENTS.md).
    for (auto type : bothTypes)
    {
        const auto sample = 0.5f;
        check(std::abs(saturate(type, sample, 0.001f) - sample) < 0.01f,
              "saturation ramps continuously from zero drive");
    }

    // NaN/Inf input must not crash and must not silently turn into a normal-looking number at
    // zero drive (it is a passthrough there).
    for (auto type : bothTypes)
    {
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        check(std::isnan(saturate(type, nan, 0.0f)), "zero-drive passthrough preserves NaN input");
    }

    // The decramped shelf must track the analog prototype at every host rate. This is the
    // property that makes the EQ curve independent of the oversampling setting: RBJ at the host
    // rate is off by up to 1.21 dB (the 18 kHz shelf at 44.1 kHz), which is exactly why the same
    // nominal curve used to measure differently at 2x than at 8x.
    {
        auto worstError = 0.0;
        auto worstUnity = 0.0;
        for (auto highShelf : { false, true })
        {
            const auto& positions = highShelf ? bqt::highShelfFrequenciesHz : bqt::lowShelfFrequenciesHz;
            for (auto centre : positions)
                for (auto rate : testRates)
                {
                    // A 0 dB shelf must be exactly transparent, at every position and rate.
                    const auto flat = bqt::makeMatchedShelf(rate, centre, shelfQ, 1.0, highShelf);
                    for (double f = 20.0; f < rate * 0.45; f *= 1.5)
                        worstUnity = std::fmax(worstUnity, std::abs(20.0 * std::log10(biquadMagnitude(flat, f, rate))));

                    for (auto gainDb : testGainsDb)
                    {
                        const auto gain = std::pow(10.0, gainDb / 20.0);
                        const auto coeffs = bqt::makeMatchedShelf(rate, centre, shelfQ, gain, highShelf);

                        for (auto value : coeffs)
                            check(std::isfinite(value), "shelf coefficients are finite");

                        // Poles strictly inside the unit circle.
                        const auto a1 = coeffs[4];
                        const auto a2 = coeffs[5];
                        check(std::abs(a2) < 1.0f && std::abs(a1) < 1.0f + a2, "shelf is stable");

                        for (double f = 20.0; f < std::fmin(19000.0, rate * 0.45); f *= 1.2)
                        {
                            const auto target = bqt::analogShelfMagnitude(f, centre, shelfQ, gain, highShelf);
                            const auto actual = biquadMagnitude(coeffs, f, rate);
                            worstError = std::fmax(worstError, std::abs(20.0 * std::log10(actual / target)));
                        }
                    }
                }
        }

        check(worstUnity < 1.0e-4, "0 dB shelf is exactly transparent");
        check(worstError < 0.30, "decramped shelf tracks the analog prototype within 0.3 dB");
        std::printf("shelf: worst deviation from analog target %.3f dB, worst 0 dB error %.2e dB\n",
                    worstError, worstUnity);
    }

    // M/S encode then decode is the identity (to float rounding) and mono input has no side.
    {
        float left[64], right[64], l0[64], r0[64];
        for (int i = 0; i < 64; ++i)
        {
            l0[i] = left[i] = std::sin(0.1f * static_cast<float>(i));
            r0[i] = right[i] = 0.5f * std::cos(0.07f * static_cast<float>(i));
        }
        bqt::encodeMidSide(left, right, 64);
        bqt::decodeMidSide(left, right, 64);
        auto worst = 0.0f;
        for (int i = 0; i < 64; ++i)
            worst = std::fmax(worst, std::fmax(std::abs(left[i] - l0[i]), std::abs(right[i] - r0[i])));
        check(worst < 1.0e-6f, "mid/side round trip is the identity");

        float ml[8] { 1, 2, 3, 4, 5, 6, 7, 8 }, mr[8] { 1, 2, 3, 4, 5, 6, 7, 8 };
        bqt::encodeMidSide(ml, mr, 8);
        for (auto s : mr)
            check(s == 0.0f, "mono input has exactly zero side");
    }

    if (failures == 0)
    {
        std::printf("All DSP tests passed.\n");
        return 0;
    }

    std::printf("%d DSP test(s) failed.\n", failures);
    return 1;
}
