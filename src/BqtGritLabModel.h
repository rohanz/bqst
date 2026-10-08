#pragma once

#include "BqtGritLabParams.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace bqt
{
// Empirical parallel Hammerstein model fitted to one LA500A's engaged/bypass captures.
// Not a circuit simulation. Seven filtered odd shapers model the level-dependent loss;
// four bounded flux-like paths model low-frequency curvature. All memory is per channel.
// Drive uses the generated perceptual taper; setReferenceKnob retains measured landmarks.
// A fitted output knee retains captured limiting; its physical origin is not identified.
class GritLabModel
{
public:
    void prepare(double sampleRate)
    {
        for (size_t i = 0; i < loss.size(); ++i)
            loss[i].prepare(sampleRate, gritlab::poles[i]);
        for (size_t i = 0; i < core.size(); ++i)
            core[i].prepare(sampleRate, gritlab::coreFrequencies[i]);
        dc.prepare(sampleRate, 1.0);
    }

    void reset()
    {
        for (auto& f : loss) f.reset();
        for (auto& f : core) f.reset();
        dc.reset();
    }

    void setReferenceKnob(double db)
    {
        db = std::clamp(db, 0.0, 18.0);
        colour = std::clamp(db / 3.0, 0.0, 1.0);
        double makeup = 0.0;
        for (size_t i = 1; i < gritlab::knobDb.size(); ++i)
        {
            const auto fraction = std::clamp((db - gritlab::knobDb[i - 1])
                / (gritlab::knobDb[i] - gritlab::knobDb[i - 1]), 0.0, 1.0);
            makeup += fraction * (gritlab::makeupDb[i] - gritlab::makeupDb[i - 1]);
        }
        push = std::pow(10.0, makeup / 20.0);
    }

    template <size_t N>
    static double lookup(const std::array<double, N>& table, double db)
    {
        const auto position = std::clamp(db, 0.0, 18.0) * (N - 1) / 18.0;
        const auto i = std::min(static_cast<size_t>(position), N - 2);
        return table[i] + (table[i + 1] - table[i]) * (position - static_cast<double>(i));
    }

    void setKnob(double db) { setReferenceKnob(lookup(gritlab::referenceKnob, db)); }
    static double autoGain(double db)
    {
        return std::pow(10.0, lookup(gritlab::autoGainDb, db) / 20.0);
    }

    double process(double input)
    {
        // Even at zero colour the state can run warm; the return is bit-exact bypass.
        const auto x = input * push;
        const auto magnitude = std::abs(x);
        const auto position = std::sqrt(std::min(magnitude, gritlab::tableMaximum)
                                        / gritlab::tableMaximum) * (gritlab::tableSize - 1);
        const auto index = std::min(static_cast<int>(position), gritlab::tableSize - 2);
        const auto step = gritlab::tableMaximum / ((gritlab::tableSize - 1.0) * (gritlab::tableSize - 1.0));
        const auto lowerX = step * index * index;
        const auto fraction = (std::min(magnitude, gritlab::tableMaximum) - lowerX) / (step * (2 * index + 1));
        auto y = gritlab::direct * x;
        for (size_t i = 0; i < loss.size(); ++i)
        {
            const auto& table = gritlab::tables[i];
            const auto j = static_cast<size_t>(index);
            auto shaped = static_cast<double>(table[j])
                        + (static_cast<double>(table[j + 1]) - table[j]) * fraction;
            // Beyond the fitted table, continue its finite linear asymptote.
            shaped += gritlab::linearSlopes[i] * std::max(0.0, magnitude - gritlab::tableMaximum);
            y += loss[i].process(x < 0.0 ? -shaped : shaped);
        }
        for (size_t i = 0; i < core.size(); ++i)
        {
            const auto ratio = 40.0 / gritlab::coreFrequencies[i];
            auto z = core[i].process(x) * std::sqrt(1.0 + ratio * ratio);
            z /= std::sqrt(1.0 + z * z);
            y += gritlab::coreWeights[i] * z * z * z;
        }
        // Evaluate the fitted soft knee with powers of numbers <= 1, even on overload.
        const auto ratio = std::abs(y) / gritlab::outputThreshold;
        const auto bounded = std::min(ratio, 1.0 / std::max(ratio, 1.0));
        const auto denominator = std::pow(1.0 + std::pow(bounded, gritlab::outputKnee),
                                          1.0 / gritlab::outputKnee);
        y = std::copysign(gritlab::outputThreshold * std::min(ratio, 1.0) / denominator, y);
        y -= dc.process(y);
        return input + (y / push - input) * colour;
    }

private:
    struct Lowpass
    {
        double b = 0.0, a = 0.0, state = 0.0;
        void prepare(double rate, double hz)
        {
            const auto k = std::tan(3.14159265358979323846 * std::min(hz, rate * 0.42) / rate);
            b = k / (1.0 + k);
            a = (k - 1.0) / (1.0 + k);
        }
        double process(double x)
        {
            const auto y = b * x + state;
            state = b * x - a * y;
            return y;
        }
        void reset() { state = 0.0; }
    };
    std::array<Lowpass, gritlab::poles.size()> loss;
    std::array<Lowpass, gritlab::coreFrequencies.size()> core;
    Lowpass dc;
    double colour = 0.0, push = 1.0;
};
} // namespace bqt
