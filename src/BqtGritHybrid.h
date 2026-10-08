#pragma once
#include "BqtGritHybridParams.h"
#include "BqtGritLabModel.h"
#include "BqtDsp.h"
#include <algorithm>
#include <cmath>

namespace bqt
{
// Two independently driven complete paths. The processor runs Legacy at the original
// knob value and Captured at capturedKnob(). Only their outputs are blended.
struct GritHybrid
{
    static constexpr float capturedMix = .25f;
    static double capturedKnob(double db)
    {
        db = std::clamp(db, 0.0, 18.0);
        if (db <= 1.0) return db;
        if (db >= 9.0) return 15.0 + (db - 9.0) / 3.0;
        const auto t = (db - 1.0) / 8.0;
        const auto t2 = t * t;
        const auto t3 = t2 * t;
        return (2.0 * t3 - 3.0 * t2 + 1.0)
             + (t3 - 2.0 * t2 + t) * 8.0
             + (-2.0 * t3 + 3.0 * t2) * 15.0
             + (t3 - t2) * (8.0 / 3.0);
    }
    static float capturedAlignment(double db)
    {
        // Place both paths on comparable nominal levels before mixing. Keep the
        // legacy raw output untouched; compensate for Captured's different gain law.
        return static_cast<float>(GritLabModel::autoGain(capturedKnob(db)))
             / saturationAutoGain(static_cast<float>(db / 18.0), SaturationType::transformer);
    }
    static float blend(float legacy, float captured, float alignment)
    { return (1.0f - capturedMix) * legacy + capturedMix * captured * alignment; }
    static float autoGain(float db)
    {
        return static_cast<float>(std::pow(10.0,
            GritLabModel::lookup(grithybrid::autoGainDb, db) / 20.0));
    }
};
}
