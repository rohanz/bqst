#pragma once
#include <juce_dsp/juce_dsp.h>
#include "BqtDsp.h"

namespace bqt
{
// Legacy Grit: six tone biquads, the transformer shaper and a DC blocker, as one path.
// Sample selects coefficient construction, recursive state and signal arithmetic together.
// Use double: in float the low shelves lose low-frequency gain accuracy as the processing rate
// rises, and at 176.4/192 kHz with 8x oversampling their poles become marginal or unstable.
template <typename Sample> class LegacyGrit
{
public:
    using Value = Sample;
    using Filter = juce::dsp::IIR::Filter<Sample>;
    enum class Role
    {
        guardPre,
        lowDrive,
        weight,
        lowRestore,
        top,
        guardPost,
        count
    };
    static constexpr gritTone::FilterSpec spec(Role role)
    {
        switch (role)
        {
        case Role::guardPre:
            return gritTone::lowGuardPre;
        case Role::lowDrive:
            return gritTone::lowDrive;
        case Role::weight:
            return gritTone::weight;
        case Role::lowRestore:
            return gritTone::lowRestore;
        case Role::top:
            return gritTone::top;
        case Role::guardPost:
            return gritTone::lowGuardPost;
        case Role::count:
            break;
        }
        jassertfalse;
        return gritTone::lowGuardPre;
    }
    static std::array<Sample, 6> coefficients(int index, double rate, bool clampTop = true)
    {
        using C = juce::dsp::IIR::ArrayCoefficients<Sample>;
        const auto spec = LegacyGrit::spec(static_cast<Role>(index));
        const auto hz = static_cast<Sample>(spec.kind == gritTone::Kind::highShelf && clampTop
                                                ? std::min(static_cast<double>(spec.hz), rate * .42)
                                                : spec.hz);
        const auto q = static_cast<Sample>(spec.q);
        const auto gain = juce::Decibels::decibelsToGain(static_cast<Sample>(spec.gainDb));
        if (spec.kind == gritTone::Kind::peak)
            return C::makePeakFilter(rate, hz, q, gain);
        if (spec.kind == gritTone::Kind::highShelf)
            return C::makeHighShelf(rate, hz, q, gain);
        return C::makeLowShelf(rate, hz, q, gain);
    }
    // Coefficients are assigned in place; caller prepares storage off the audio thread.
    // Do not reset history here: Vintage coefficient refresh must preserve BQST state.
    void configure(double rate, bool clampTop = true)
    {
        for (int i = 0; i < static_cast<int>(Role::count); ++i)
            *filters[static_cast<size_t>(i)].coefficients = coefficients(i, rate, clampTop);
        dc = std::exp(-Sample(2) * juce::MathConstants<Sample>::pi * Sample(5) / static_cast<Sample>(rate));
    }
    template <typename Fn> void forEachFilter(Fn&& fn)
    {
        for (auto& f : filters)
            fn(f);
    }
    Sample dcCoefficient() const noexcept
    {
        return dc;
    }
    void resetDC()
    {
        previousInput = previousOutput = Sample(0);
    }
    void reset()
    {
        for (auto& f : filters)
            f.reset();
        resetDC();
    }
    void setDrive(Sample drive)
    {
        controls = makeTransformerControlsFor(drive);
    }
    void copyDriveFrom(const LegacyGrit& other)
    {
        controls = other.controls;
    }
    template <typename AfterTone>
    Sample process(Sample input, Sample drive, Sample gain, AfterTone&& afterTone)
    {
        const auto color = std::min(Sample(1), drive * Sample(3));
        const auto colored = [color](Filter& f, Sample value)
        { return value + (f.processSample(value) - value) * color; };
        auto value = colored(filter(Role::guardPre), input);
        value = colored(filter(Role::lowDrive), value);
        value = colored(filter(Role::weight), value);
        value *= gain;
        value = transformerSaturate(value, controls);
        value = colored(filter(Role::lowRestore), value);
        value = colored(filter(Role::top), value);
        value = colored(filter(Role::guardPost), value);
        value = afterTone(value);
        const auto blocked = value - previousInput + dc * previousOutput;
        previousInput = value;
        previousOutput = blocked;
        value += (blocked - value) * color;
        return value;
    }
    Sample process(Sample input, Sample drive, Sample gain)
    {
        return process(input, drive, gain, [](Sample value) { return value; });
    }

private:
    Filter& filter(Role role)
    {
        return filters[static_cast<size_t>(role)];
    }
    std::array<Filter, static_cast<size_t>(Role::count)> filters;
    TransformerControlsT<Sample> controls;
    Sample dc = 0, previousInput = 0, previousOutput = 0;
};
} // namespace bqt
