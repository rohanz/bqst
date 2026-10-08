#include "../src/BqtGritLabModel.h"
#include "../src/BqtGritHybrid.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <functional>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;
void check(bool ok, const char* what)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
struct Measurement { double gainDb, thd; };
Measurement measure(double rate, double hz, double level, double knob)
{
    bqt::GritLabModel m;
    m.prepare(rate); m.setReferenceKnob(knob);
    const auto amplitude = std::pow(10.0, level / 20.0);
    double re[10] {}, im[10] {};
    const auto n = static_cast<int>(rate);
    for (int i = 0; i < 2 * n; ++i)
    {
        const auto phase = 2.0 * pi * hz * i / rate;
        const auto y = m.process(amplitude * std::sin(phase));
        check(std::isfinite(y) && std::abs(y) < 4.0, "finite bounded sine output");
        if (i >= n)
            for (int h = 1; h < 10; ++h)
            {
                re[h] += y * std::cos(h * phase);
                im[h] += y * std::sin(h * phase);
            }
    }
    const auto fundamental = std::hypot(re[1], im[1]);
    double harmonics = 0.0;
    for (int h = 2; h < 10; ++h) harmonics += re[h] * re[h] + im[h] * im[h];
    return {20.0 * std::log10(2.0 * fundamental / n / amplitude), std::sqrt(harmonics) / fundamental};
}
}
int main(int argc, char** argv)
{
    check(argc == 2, "fixture path supplied");
    std::ifstream fixture(argv[1], std::ios::binary);
    std::uint32_t count = 0, fixtureRate = 0;
    fixture.read(reinterpret_cast<char*>(&count), sizeof(count));
    fixture.read(reinterpret_cast<char*>(&fixtureRate), sizeof(fixtureRate));
    check(count == 32768 && fixtureRate == 48000, "fixture header");
    std::vector<double> input(count), expected(count);
    fixture.read(reinterpret_cast<char*>(input.data()), static_cast<std::streamsize>(count * sizeof(double)));
    for (const auto knob : {6.0, 18.0})
    {
        fixture.read(reinterpret_cast<char*>(expected.data()), static_cast<std::streamsize>(count * sizeof(double)));
        check(fixture.good(), "complete fixture");
        bqt::GritLabModel model;
        model.prepare(fixtureRate); model.setKnob(knob);
        double maximumError = 0.0;
        for (size_t i = 0; i < count; ++i)
            maximumError = std::max(maximumError, std::abs(model.process(input[i]) - expected[i]));
        std::printf("Python port at %.0f dB: max error %.9g\n", knob, maximumError);
        check(maximumError < 2e-6, "Python reference port within lookup interpolation tolerance");
    }

    check(bqt::gritlab::referenceKnob.front() == 0.0 && bqt::gritlab::referenceKnob.back() == 18.0, "taper preserves endpoints");
    check(bqt::GritLabModel::autoGain(0) == 1.0, "zero-drive compensation is unity");
    for (size_t i = 1; i < bqt::gritlab::referenceKnob.size(); ++i)
        check(bqt::gritlab::referenceKnob[i] >= bqt::gritlab::referenceKnob[i - 1], "monotonic drive taper");
    for (double k = 0; k <= 18; k += .1)
        check(std::isfinite(bqt::GritLabModel::autoGain(k)) && bqt::GritLabModel::autoGain(k) < 4, "finite bounded static makeup");

    bqt::GritLabModel a, b;
    a.prepare(48000); b.prepare(48000);
    a.setKnob(0.0);
    for (int i = 0; i < 1000; ++i)
    {
        const auto x = std::sin(i * 0.12) * 0.8;
        check(std::equal_to<double>{}(a.process(x), x), "zero drive is exact bypass");
    }
    a.reset(); b.reset(); a.setKnob(6); b.setKnob(6);
    for (int i = 0; i < 10000; ++i)
    {
        const auto x = std::sin(i * 0.023) * 0.7;
        check(std::abs(a.process(x) + b.process(-x)) < 1e-12, "odd symmetry, no bias path");
    }
    a.reset(); b.reset(); a.setKnob(.001); b.setKnob(0);
    for (int i = 0; i < 10000; ++i)
    {
        const auto x = std::sin(i * 0.009) * 0.4;
        check(std::abs(a.process(x) - b.process(x)) < .0002, "continuous engagement");
    }
    a.reset(); b.reset(); a.setKnob(18); b.setKnob(18);
    for (int i = 0; i < 10000; ++i)
    {
        const auto x = std::sin(i * 0.001) * 4.0;
        const auto y = a.process(x);
        check(std::isfinite(y) && std::abs(y) < 16.0, "bounded overload extrapolation");
        check(std::equal_to<double>{}(y, b.process(x)), "reset produces deterministic output");
    }
    for (const auto sampleRate : {8000.0, 16000.0, 32000.0, 768000.0})
    {
        a.prepare(sampleRate); a.reset(); a.setKnob(18);
        for (int i = 0; i < 20000; ++i)
        {
            const auto y = a.process(std::sin(i * .13));
            check(std::isfinite(y) && std::abs(y) < 16.0, "stable at low and extreme sample rates");
        }
    }
    // Independent captured targets at -18 dBFS, relative to relay bypass. This prototype
    // intentionally allows a broad THD tolerance; exact waveform replication is not claimed.
    for (const auto hz : {40, 80, 160, 1000})
    {
        const auto ref = measure(48000, hz, -18, 3);
        const auto gainTarget = hz == 40 ? -.466 : hz == 80 ? -.341 : hz == 160 ? -.235 : -.028;
        const auto thdTarget = hz == 40 ? .005875 : hz == 80 ? .003812 : hz == 160 ? .002483 : .0005965;
        std::printf("%.0f Hz: %.4f dB, THD %.5f%%\n", static_cast<double>(hz), ref.gainDb, ref.thd * 100);
        check(std::abs(ref.gainDb - gainTarget) < .13, "reference gain within 0.13 dB of capture");
        check(ref.thd > thdTarget * .4 && ref.thd < thdTarget * 1.4, "reference harmonic character retained");
        for (const auto rate : {44100.0, 96000.0, 192000.0, 384000.0})
        {
            const auto m = measure(rate, hz, -18, 3);
            check(std::abs(m.gainDb - ref.gainDb) < .04, "sample-rate gain consistency");
            check(std::abs(m.thd - ref.thd) < .0002, "sample-rate harmonic consistency");
        }
    }
    // Independent measured 1 kHz landmarks from the newly driven captures.
    // Compensated gain subtracts the measured small-signal makeup gain.
    for (const auto knob : {12.0, 18.0})
    {
        const auto level = knob == 12.0 ? -6.0 : -18.0;
        const auto m = measure(192000, 1000, level, knob);
        const auto targetGain = knob == 12.0 ? 5.3852 - 9.8465 : 16.6690 - 17.8381;
        const auto targetThd = knob == 12.0 ? .23579 : .10127;
        std::printf("Driven %.0f dB: gain %.4f dB, THD %.4f%%\n", knob, m.gainDb, m.thd * 100);
        check(std::abs(m.gainDb - targetGain) < .15, "driven gain within 0.15 dB of retained capture");
        check(std::abs(m.thd - targetThd) < .012, "driven THD within 1.2 percentage points of capture");
    }
    check(bqt::GritHybrid::autoGain(0) == 1.0f, "hybrid zero-drive compensation unity");
    check(std::abs(bqt::GritHybrid::capturedKnob(9) - 15) < 1e-12, "only captured control is remapped");
    check(std::abs(bqt::GritHybrid::capturedKnob(12) - 16) < 1e-12, "captured upper range expanded");
    check(std::abs(bqt::GritHybrid::capturedKnob(18) - 18) < 1e-12, "captured maximum unchanged");
    for (int i = 1; i <= 1800; ++i)
        check(bqt::GritHybrid::capturedKnob(i * .01) >= bqt::GritHybrid::capturedKnob((i - 1) * .01), "captured mapping monotonic");
    for (const auto join : {1.0, 9.0})
    {
        const auto left = (bqt::GritHybrid::capturedKnob(join) - bqt::GritHybrid::capturedKnob(join - .0001)) / .0001;
        const auto right = (bqt::GritHybrid::capturedKnob(join + .0001) - bqt::GritHybrid::capturedKnob(join)) / .0001;
        check(std::abs(left - right) < .001, "captured mapping smooth joins");
    }
    for (int step = 0; step <= 180; ++step)
    {
        const auto knob = static_cast<float>(step) / 10.0f;
        const auto gain = bqt::GritHybrid::autoGain(knob);
        const auto alignment = bqt::GritHybrid::capturedAlignment(knob);
        check(std::isfinite(gain) && gain > .01f && gain < 4.0f, "hybrid compensation bounded");
        check(std::isfinite(alignment) && alignment >= 1.0f && alignment < 20.0f, "captured branch alignment bounded");
    }
    std::puts("Grit Lab tests passed.");
}
