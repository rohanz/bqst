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
            check(saturate(type, sample, 0.0f) == sample, "saturation is exact bypass at zero drive");

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

    if (failures == 0)
    {
        std::printf("All DSP tests passed.\n");
        return 0;
    }

    std::printf("%d DSP test(s) failed.\n", failures);
    return 1;
}
