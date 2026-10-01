#pragma once

// Cream saturation: a soft-knee clipper between a high-shelf emphasis pair, with a low-passed
// even-harmonic path and its own DC blocker. Constants come from the generated BqtCreamParams.h
// (tools/cream_model). Per channel, at the oversampled rate, double precision, no allocation.
// Must stay sample-identical to tools/cream_model density/bqst_export.py:bqst_render.

#include "BqtCreamParams.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace bqt
{
class CreamModel
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        setHighShelf(emphasis, cream::emphHz, cream::emphQ, cream::emphDb);
        setOnePoleLowpass(lowPath, cream::lf2Hz);
        setOnePoleHighpass(dcBlocker, cream::dcBlockerHz);
        applyKnob();
    }

    void reset()
    {
        emphasis.reset();
        deemphasis.reset();
        lowPath.reset();
        dcBlocker.reset();
    }

    void setKnob(double knobDb)
    {
        if (std::equal_to<double>{}(knobDb, knob)) // exact: only a real knob change recomputes
            return;
        knob = knobDb;
        applyKnob();
    }

    double process(double x)
    {
        if (drive <= 0.0)
            return x;

        const auto u = emphasis.process(x);
        const auto z = push * u;
        auto v = (clip(z + cream::bias) - biasOffset) / push;
        const auto low = lowPath.process(z);
        v += lowScale * low * low;
        v = deemphasis.process(v);
        // Blended in with colour like the emphasis tilt: a full 5 Hz high-pass at knob 0.1 dB
        // would break continuity with the exact bypass.
        return v + (dcBlocker.process(v) - v) * color;
    }

    static double driveForKnob(double knobDb)
    {
        const auto pos = std::clamp(knobDb / cream::knobStepDb, 0.0, static_cast<double>(cream::knobPoints - 1));
        const auto i = std::min(static_cast<int>(pos), cream::knobPoints - 2);
        const auto t = pos - i;
        return cream::knobDrive[i] * (1.0 - t) + cream::knobDrive[i + 1] * t;
    }

    // Same ramp as BQST's colorationRampScale: full strength at 6 dB of knob.
    static double colorForKnob(double knobDb) { return std::clamp(knobDb / 18.0 * 3.0, 0.0, 1.0); }

    static float autoGainForKnob(float knobDb)
    {
        if (knobDb <= 0.0f)
            return 1.0f;
        const auto pos = std::clamp(static_cast<double>(knobDb) / cream::autoGainStepDb, 0.0,
                                    static_cast<double>(cream::autoGainPoints - 1));
        const auto i = std::min(static_cast<int>(pos), cream::autoGainPoints - 2);
        const auto t = pos - i;
        const auto db = cream::autoGainDb[i] * (1.0 - t) + cream::autoGainDb[i + 1] * t;
        return static_cast<float>(std::pow(10.0, db / 20.0));
    }

private:
    // Transposed direct form II, matching scipy.signal.sosfilt.
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
        double process(double x)
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1 = z2 = 0.0; }
    };

    static constexpr double pi = 3.14159265358979323846;

    static double clip(double x)
    {
        return x / std::pow(1.0 + std::pow(std::fabs(x), cream::knee), 1.0 / cream::knee);
    }

    void setHighShelf(Biquad& f, double f0, double q, double gainDb) const
    {
        const auto A = std::pow(10.0, gainDb / 40.0);
        const auto w0 = 2.0 * pi * f0 / fs;
        const auto cw = std::cos(w0), sw = std::sin(w0);
        const auto sq = 2.0 * std::sqrt(A) * (sw / (2.0 * q));
        const auto a0 = (A + 1.0) - (A - 1.0) * cw + sq;
        f.b0 = A * ((A + 1.0) + (A - 1.0) * cw + sq) / a0;
        f.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0;
        f.b2 = A * ((A + 1.0) + (A - 1.0) * cw - sq) / a0;
        f.a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0;
        f.a2 = ((A + 1.0) - (A - 1.0) * cw - sq) / a0;
    }

    void setOnePoleLowpass(Biquad& f, double fc) const
    {
        const auto k = std::tan(pi * fc / fs);
        f.b0 = f.b1 = k / (1.0 + k);
        f.b2 = f.a2 = 0.0;
        f.a1 = (k - 1.0) / (k + 1.0);
    }

    void setOnePoleHighpass(Biquad& f, double fc) const
    {
        const auto k = std::tan(pi * fc / fs);
        f.b0 = 1.0 / (1.0 + k);
        f.b1 = -f.b0;
        f.b2 = f.a2 = 0.0;
        f.a1 = (k - 1.0) / (k + 1.0);
    }

    void applyKnob()
    {
        drive = driveForKnob(knob);
        push = drive * cream::inputLevel;
        biasOffset = clip(cream::bias);
        lowScale = push > 0.0 ? cream::lf2Gain * std::pow(drive, cream::lf2Slope) / push : 0.0;
        // The emphasis pair is not an exact inverse at full strength; blend the net tilt in with
        // drive so knob 0 -> 0.1 dB is continuous (de-emphasis = -pre at colour 0).
        color = colorForKnob(knob);
        const auto post = -cream::emphDb + (cream::deemphDb + cream::emphDb) * color;
        setHighShelf(deemphasis, cream::emphHz, cream::emphQ, post);
    }

    double fs = 176400.0;
    double knob = 0.0, drive = 0.0, color = 0.0, push = 0.0, biasOffset = 0.0, lowScale = 0.0;
    Biquad emphasis, deemphasis, lowPath, dcBlocker;
};
} // namespace bqt
