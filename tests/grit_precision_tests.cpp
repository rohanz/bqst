// Grit precision guarantees for the real BQST processor: the legacy tone filters and DC blocker
// realise their design at every supported rate and oversampling factor, long programme at the
// highest host rates with 8x (Vintage off/on) stays finite and settles to silence, and Grit's low-frequency level
// is the same at every oversampling setting. Part of BqstChainTests; no DAW or audio device.

#include "../src/PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <type_traits>
#include <vector>

int runGritPrecisionTests();

namespace
{
static_assert(std::is_same_v<BqtAudioProcessor::LegacyGrit::Value, double>,
              "BQST's legacy Grit path runs in double precision");
using Legacy = BqtAudioProcessor::LegacyGrit;
constexpr double pi = juce::MathConstants<double>::pi;
int failures = 0;

void check(bool ok, const char* text)
{
    if (! ok)
    {
        ++failures;
        std::printf("FAIL Grit precision: %s\n", text);
    }
}

void set(BqtAudioProcessor& p, const char* id, float plain)
{
    auto* q = p.state().getParameter(id);
    jassert(q != nullptr);
    q->setValueNotifyingHost(q->convertTo0to1(plain));
}

// Independent RBJ cookbook formulation of the same specs, evaluated in long double.
std::array<long double, 6> intent(int index, long double rate)
{
    struct Spec { bqt::gritTone::Kind kind; double hz, q, gainDb; };
    const auto legacy = Legacy::spec(static_cast<Legacy::Role>(index < 6 ? index : 0));
    const Spec spec = index < 6 ? Spec { legacy.kind, legacy.hz, legacy.q, legacy.gainDb }
                               : Spec { bqt::gritTone::Kind::highShelf, bqt::cream::vintageHz,
                                        bqt::cream::vintageQ, bqt::cream::vintageDb };
    const long double A = std::pow(10.L, static_cast<long double>(spec.gainDb) / 40.L);
    const long double w = 2 * std::acos(-1.L) * spec.hz / rate, c = std::cos(w), s = std::sin(w), q = spec.q;
    if (spec.kind == bqt::gritTone::Kind::peak)
    {
        const auto alpha = s / (2 * q);
        return { 1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A };
    }
    const auto beta = s * std::sqrt(A) / q, m = A - 1, p = A + 1;
    if (spec.kind == bqt::gritTone::Kind::highShelf)
        return { A * (p + m * c + beta), -2 * A * (m + p * c), A * (p + m * c - beta),
                 p - m * c + beta,       2 * (m - p * c),      p - m * c - beta };
    return { A * (p - m * c + beta), 2 * A * (m - p * c), A * (p - m * c - beta),
             p + m * c + beta,       -2 * (m + p * c),    p + m * c - beta };
}

template <class T>
std::complex<long double> response(const std::array<T, 6>& a, double hz, double rate)
{
    const auto z = std::exp(std::complex<long double>(0, -2 * std::acos(-1.L) * hz / rate));
    return (static_cast<long double>(a[0]) + static_cast<long double>(a[1]) * z
            + static_cast<long double>(a[2]) * z * z)
         / (static_cast<long double>(a[3]) + static_cast<long double>(a[4]) * z
            + static_cast<long double>(a[5]) * z * z);
}

// BQST configures the legacy filters without the top-shelf clamp.
void coefficientAudit()
{
    double radiusMax = 0, errorMax = 0, blockerMax = 0;
    for (double host : { 44100., 48000., 88200., 96000., 176400., 192000. })
        for (int factor : { 1, 2, 4, 8 })
        {
            const double rate = host * factor;
            Legacy engine;
            engine.configure(rate, false);
            const auto blocker = static_cast<double>(engine.dcCoefficient());
            const auto expected = std::exp(-2 * std::acos(-1.L) * 5 / static_cast<long double>(rate));
            check(std::isfinite(blocker) && blocker < 1 - 1e-5 && std::abs(blocker - expected) < 3e-16,
                  "DC blocker pole is stable and matches its design");
            blockerMax = std::max(blockerMax, blocker);

            for (int i = 0; i < 8; ++i)
            {
                // Normalise the realised coefficients the way JUCE does, then evaluate them.
                // Slots 6/7 represent gritVintage/hybridCapturedVintage, whose identical
                // specs are private. Audit their double design here and the installed paths
                // independently through vintageResponse() below; no production test seam.
                const auto designed = i < 6 ? Legacy::coefficients(i, rate, false)
                    : juce::dsp::IIR::ArrayCoefficients<double>::makeHighShelf(
                        rate, bqt::cream::vintageHz, bqt::cream::vintageQ,
                        juce::Decibels::decibelsToGain(bqt::cream::vintageDb));
                const auto reciprocal = 1 / designed[3];
                std::array<double, 6> a {};
                for (size_t k = 0; k < a.size(); ++k)
                    a[k] = static_cast<double>(designed[k] * reciprocal);
                const auto disc = std::sqrt(std::complex<double>(a[4] * a[4] - 4 * a[5], 0));
                const auto radius = std::max(std::abs((-a[4] + disc) * .5), std::abs((-a[4] - disc) * .5));
                check(std::isfinite(radius) && radius < 1 - 1e-5, "every Grit biquad pole is inside the unit circle");
                radiusMax = std::max(radiusMax, radius);
                for (double hz : { 0., 20., 50., 100., 200. })
                {
                    const auto error = std::abs(static_cast<double>(
                        20 * std::log10(std::abs(response(a, hz, rate) / response(intent(i, rate), hz, rate)))));
                    check(std::isfinite(error) && error < 1e-6, "realised DC/LF gain matches the design within 1e-6 dB");
                    errorMax = std::max(errorMax, error);
                }
            }
        }
    std::printf("grit precision: max biquad radius %.15g (margin %.6g), max DC/LF gain error %.3g dB, "
                "DC blocker margin %.6g\n",
                radiusMax, 1 - radiusMax, errorMax, 1 - blockerMax);
}

double programme(double t)
{
    return .48 * std::sin(2 * pi * 40 * t) + .23 * std::sin(2 * pi * 997 * t)
         + .12 * std::sin(2 * pi * 3413 * t) * (.7 + .3 * std::sin(2 * pi * 3 * t))
         + .17 * std::sin(2 * pi * 73 * t) * std::exp(-12 * std::fmod(t, .25));
}

void configureGrit(BqtAudioProcessor& p, int revision, float drive, int oversampling)
{
    set(p, "aSatType", 1);
    set(p, "bSatType", 1);
    set(p, "gritRevision", static_cast<float>(revision));
    set(p, "aDrive", drive);
    set(p, "bDrive", drive);
    set(p, "osRealtime", static_cast<float>(oversampling));
}

// 30 s of programme at -12 dBFS peak, then 5 s of exact silence, at the highest host rates with 8x.
void longProgrammeStability()
{
    constexpr int block = 512;
    for (int revision : { 0, 2 })
        for (double rate : { 176400., 192000. })
            for (float drive : { 6.f, 18.f })
              for (bool vintage : { false, true })
            {
                BqtAudioProcessor p;
                configureGrit(p, revision, drive, 3);
                set(p, "vintage", vintage ? 1.f : 0.f);
                p.setPlayConfigDetails(2, 2, rate, block);
                p.prepareToPlay(rate, block);

                std::vector<float> input(static_cast<size_t>(rate * 3));
                double maximum = 0;
                for (size_t i = 0; i < input.size(); ++i)
                {
                    input[i] = static_cast<float>(programme(static_cast<double>(i) / rate));
                    maximum = std::max(maximum, std::abs(static_cast<double>(input[i])));
                }
                for (auto& x : input)
                    x = static_cast<float>(x / maximum * std::pow(10., -.6));

                juce::AudioBuffer<float> buffer(2, block);
                juce::MidiBuffer midi;
                double peak = 0, tail = 0, sum = 0;
                long count = 0;
                bool finite = true;
                const auto total = static_cast<long>(rate * 35), programmeEnd = static_cast<long>(rate * 30);
                for (long offset = 0; offset < total; offset += block)
                {
                    const int n = static_cast<int>(std::min<long>(block, total - offset));
                    buffer.setSize(2, n, false, false, true);
                    for (int i = 0; i < n; ++i)
                        for (int c = 0; c < 2; ++c)
                            buffer.setSample(c, i, offset + i < programmeEnd
                                                       ? input[static_cast<size_t>(offset + i) % input.size()]
                                                       : 0.f);
                    p.processBlock(buffer, midi);
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < n; ++i)
                        {
                            const double x = buffer.getSample(c, i);
                            finite = finite && std::isfinite(x);
                            peak = std::max(peak, std::abs(x));
                            if (offset + i >= static_cast<long>(rate * 34))
                            {
                                tail = std::max(tail, std::abs(x));
                                sum += x;
                                ++count;
                            }
                        }
                }
                const auto tailMean = sum / static_cast<double>(count);
                check(finite && peak < 1., "Grit at 176.4/192 kHz with 8x stays finite and bounded");
                check(std::isfinite(tail) && tail < 1e-7 && std::abs(tailMean) < 1e-8,
                      "Grit settles to silence with no residual offset or limit cycle");
                std::printf("grit long run: revision %d, %.0f Hz x8, drive %.0f, vintage %d: peak %.9f, final-second peak %.3g, "
                            "mean %.3g\n",
                            revision, rate, drive, vintage, peak, tail, tailMean);
            }
}

double sineLevel(int revision, float drive, int oversampling, double hz, double rate,
                 double amplitude, bool vintage = false)
{
    constexpr int block = 256;
    BqtAudioProcessor p;
    configureGrit(p, revision, drive, oversampling);
    set(p, "vintage", vintage ? 1.f : 0.f);
    p.setPlayConfigDetails(2, 2, rate, block);
    p.prepareToPlay(rate, block);
    juce::AudioBuffer<float> buffer(2, block);
    juce::MidiBuffer midi;
    double squared = 0;
    long count = 0;
    // Measure exactly one second after one second of settling: integer-Hz tones have
    // whole cycles even at 44.1 kHz, without a block-rounded window bias.
    const auto total = static_cast<long>(rate * 2);
    for (long offset = 0; offset < total; offset += block)
    {
        const auto n = static_cast<int>(std::min<long>(block, total - offset));
        buffer.setSize(2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            const auto x = static_cast<float>(amplitude * std::sin(2 * pi * hz * static_cast<double>(offset + i) / rate));
            buffer.setSample(0, i, x);
            buffer.setSample(1, i, x);
        }
        p.processBlock(buffer, midi);
        for (int i = 0; i < n; ++i)
            if (offset + i >= static_cast<long>(rate))
            {
                const double y = buffer.getSample(0, i);
                squared += y * y;
                ++count;
            }
    }
    return std::sqrt(squared / static_cast<double>(count));
}

// At full colour and tiny input the Vintage shelf is a common linear factor of both
// Hybrid branches, including their DC blockers. The on/off ratio cancels their different
// gain/tone and the oversampler response. Tiny input avoids nonlinear harmonic RMS bias.
void vintageResponse()
{
    double worst = 0;
    for (double rate : { 44100., 48000., 88200., 96000., 176400., 192000. })
        for (int os = 0; os < 4; ++os)
            for (int revision : { 0, 2 })
                for (double hz : { 20., 200., 1000., 10000. })
                {
                    const auto off = sineLevel(revision, 18.f, os, hz, rate, 1e-4);
                    const auto on = sineLevel(revision, 18.f, os, hz, rate, 1e-4, true);
                    const auto internalRate = rate * (1 << os);
                    const auto expected = std::abs(response(intent(6, internalRate), hz, internalRate));
                    const auto error = std::abs(20 * std::log10(on / off / static_cast<double>(expected)));
                    worst = std::max(worst, error);
                    check(std::isfinite(error) && error < .001,
                          "installed Grit Vintage response matches independent shelf within 0.001 dB");
                }
    std::printf("grit Vintage: worst installed shelf response error %.6g dB\n", worst);
}

// The float tone filters lost low-frequency gain accuracy as the oversampled rate rose, so Grit's
// low end depended on the oversampling setting. Hold it to the same level at every factor.
void oversamplingConsistency()
{
    for (double rate : { 48000., 44100. })
      for (double amplitude : { .25, .99 })
    {
        double worst = 0;
        for (int revision : { 0, 2 })
            for (float drive : { 6.f, 12.f, 18.f })
                for (double hz : { 20., 50., 100., 200. })
                {
                    const auto reference = sineLevel(revision, drive, 0, hz, rate, amplitude);
                    for (int oversampling = 1; oversampling < 4; ++oversampling)
                    {
                        const auto spread = 20 * std::log10(sineLevel(revision, drive, oversampling, hz, rate, amplitude) / reference);
                        worst = std::max(worst, std::abs(spread));
                        check(std::isfinite(spread) && std::abs(spread) < .05,
                              "Grit low-frequency level matches across oversampling within 0.05 dB");
                    }
                }
        // Native arm64 Release measured 0.00258/0.00265 dB at 48 kHz and
        // 0.00282/0.00290 dB at 44.1 kHz (0.25/0.99 peak). A 0.005 dB
        // regression bound leaves numerical/toolchain headroom; 0.05 remains the hard limit.
        check(worst < .005, "Grit oversampling sweep stays within measured 0.005 dB regression bound");
        std::printf("grit precision: %.0f Hz, peak %.2f, worst 20-200 Hz level spread %.6f dB\n",
                    rate, amplitude, worst);
    }
}
} // namespace

int runGritPrecisionTests()
{
    juce::ScopedNoDenormals noDenormals;
    coefficientAudit();
    vintageResponse();
    oversamplingConsistency();
    longProgrammeStability();
    return failures;
}
