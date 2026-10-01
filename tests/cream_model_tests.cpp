// bqt::CreamModel: the port must match the Python reference sample-for-sample, and keep BQST's
// invariants (exact bypass at 0, continuity, bounded output, rate independence).
#include "../src/BqtCreamModel.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <functional>
#include <vector>

namespace
{
int failures = 0;
void check(bool ok, const char* what)
{
    if (! ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// H3 relative to the fundamental (dB) of a sine through the model at `rate`.
double h3Db(double rate, double knob, double freq)
{
    bqt::CreamModel m;
    m.prepare(rate);
    m.setKnob(knob);
    const int n = 1 << 15;
    const int k = static_cast<int>(std::round(freq * n / rate));
    const double f = k * rate / n;
    std::vector<double> y(static_cast<size_t>(n));
    for (int i = 0; i < 4 * n; ++i)
    {
        const auto out = m.process(0.5 * std::sin(2.0 * 3.14159265358979323846 * f * i / rate));
        if (i >= 3 * n)
            y[static_cast<size_t>(i - 3 * n)] = out;
    }
    auto bin = [&](int b)
    {
        std::complex<double> acc = 0.0;
        for (int i = 0; i < n; ++i)
            acc += y[static_cast<size_t>(i)] * std::polar(1.0, -2.0 * 3.14159265358979323846 * b * i / n);
        return std::abs(acc);
    };
    return 20.0 * std::log10(bin(3 * k) / bin(k));
}
} // namespace

int main(int argc, char** argv)
{
    // Exact bypass at knob 0, and the table starts at 0.
    {
        bqt::CreamModel m;
        m.prepare(176400.0);
        m.setKnob(0.0);
        auto exact = true;
        for (int i = 0; i < 1000; ++i)
        {
            const auto x = std::sin(0.01 * i);
            exact = exact && std::equal_to<double>{}(m.process(x), x);
        }
        check(exact, "knob 0 is an exact bypass");
        check(bqt::CreamModel::driveForKnob(0.0) == 0.0, "taper starts at zero drive");
    }

    // Continuity: knob 0.1 dB stays within 0.002 of the input.
    {
        bqt::CreamModel m;
        m.prepare(176400.0);
        m.setKnob(0.1);
        auto worst = 0.0;
        for (int i = 0; i < 20000; ++i)
        {
            const auto x = 0.5 * std::sin(2.0 * 3.14159265358979323846 * 220.0 * i / 176400.0);
            worst = std::fmax(worst, std::fabs(m.process(x) - x));
        }
        check(worst < 0.002, "knob 0.1 dB is continuous with bypass");
    }

    // Bounded and finite for hot input at every supported internal rate.
    for (double rate : { 44100.0, 88200.0, 176400.0, 352800.0, 384000.0 })
    {
        bqt::CreamModel m;
        m.prepare(rate);
        m.setKnob(18.0);
        auto ok = true;
        for (int i = 0; i < 40000; ++i)
        {
            const auto y = m.process(10.0 * std::sin(2.0 * 3.14159265358979323846 * 60.0 * i / rate));
            ok = ok && std::isfinite(y) && std::fabs(y) < 50.0;
        }
        check(ok, "hot input stays finite and bounded at every rate");
    }

    // Rate independence: H3 at 1 kHz within 0.25 dB between 4x 44.1 kHz and 4x 48 kHz.
    check(std::fabs(h3Db(176400.0, 9.0, 1000.0) - h3Db(192000.0, 9.0, 1000.0)) < 0.25,
          "harmonics do not depend on the host rate");

    // Taper monotonic; autogain unity at 0, finite, bounded, smooth.
    {
        auto mono = true;
        for (double k = 0.25; k <= 18.0; k += 0.25)
            mono = mono && bqt::CreamModel::driveForKnob(k) > bqt::CreamModel::driveForKnob(k - 0.25);
        check(mono, "taper is strictly increasing");
        check(bqt::CreamModel::autoGainForKnob(0.0f) == 1.0f, "autogain is unity at zero");
        auto sane = true;
        for (float k = 0.0f; k <= 18.0f; k += 0.1f)
        {
            const auto g = bqt::CreamModel::autoGainForKnob(k);
            const auto gNext = bqt::CreamModel::autoGainForKnob(k + 0.1f);
            sane = sane && std::isfinite(g) && g > 0.25f && g < 4.0f
                && std::fabs(20.0f * std::log10(gNext / g)) < 0.25f;
        }
        check(sane, "autogain is finite, bounded and smooth");
    }

    // Port fixture: sample-identical to the Python reference (tools/cream_model bqst_export).
    if (argc > 1)
    {
        std::ifstream file(argv[1], std::ios::binary);
        std::vector<double> data;
        double v = 0.0;
        while (file.read(reinterpret_cast<char*>(&v), sizeof v))
            data.push_back(v);
        check(data.size() > 4, "fixture loads");
        if (data.size() > 4)
        {
            const auto rate = data[0];
            const auto n = static_cast<size_t>(data[1]);
            const auto cases = static_cast<int>(data[2]);
            const double* x = data.data() + 3;
            size_t cursor = 3 + n;
            for (int c = 0; c < cases; ++c)
            {
                bqt::CreamModel m;
                m.prepare(rate);
                m.setKnob(data[cursor]);
                const double* expected = data.data() + cursor + 1;
                cursor += 1 + n;
                auto worst = 0.0, energy = 0.0;
                for (size_t i = 0; i < n; ++i)
                {
                    worst = std::fmax(worst, std::fabs(m.process(x[i]) - expected[i]));
                    energy += expected[i] * expected[i];
                }
                check(worst < 1.0e-9 * std::fmax(std::sqrt(energy / static_cast<double>(n)), 1.0e-3),
                      "C++ model matches the Python reference");
            }
        }
    }
    else
    {
        check(false, "fixture path argument missing");
    }

    if (failures == 0)
        std::printf("All Cream model tests passed.\n");
    return failures == 0 ? 0 : 1;
}
