// Before/after harness for the real BQST processor, built once per source snapshot by
// grit_equivalence/run.py. Modes: <outdir> renders the automation matrix; --levels prints
// low-frequency Grit levels; --cpu times processBlock. No DAW or audio device.
#include "PluginProcessor.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <string>
namespace
{
constexpr int block = 128;
void set(BqtAudioProcessor& p, const char* id, float value)
{
    auto* q = p.state().getParameter(id);
    if (! q)
        std::abort();
    q->setValueNotifyingHost(q->convertTo0to1(value));
}
bool render(BqtAudioProcessor& p, FILE* file, double rate, int vintage, int os, int route, int blocks)
{
    juce::AudioBuffer<float> b(2, block);
    juce::MidiBuffer midi;
    bool finite = true;
    for (int j = 0; j < blocks; ++j)
    {
        if (j < 48)
        {
            set(p, "aDrive", .1f + 17.9f * j / 47.f);
            set(p, "bDrive", 18.f - 15.f * j / 47.f);
        }
        if (j == 48)
            set(p, "vintage", static_cast<float>(1 - vintage));
        if (j == 80)
            set(p, "osRealtime", static_cast<float>((os + 1) % 4));
        if (j == 104)
            set(p, "satMode", static_cast<float>(1 - route));
        for (int i = 0; i < block; ++i)
        {
            const double t = (j * block + i) / rate;
            b.setSample(0, i,
                        static_cast<float>(.21 * std::sin(2 * juce::MathConstants<double>::pi * 53 * t) +
                                           .09 * std::sin(2 * juce::MathConstants<double>::pi * 1703 * t)));
            b.setSample(1, i,
                        static_cast<float>(.13 * std::cos(2 * juce::MathConstants<double>::pi * 97 * t) +
                                           .07 * std::sin(2 * juce::MathConstants<double>::pi * 7111 * t)));
        }
        p.processBlock(b, midi);
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < block; ++i)
                finite = finite && std::isfinite(b.getSample(c, i));
            if (std::fwrite(b.getReadPointer(c), sizeof(float), block, file) != block)
                return false;
        }
    }
    return finite;
}
void configureGrit(BqtAudioProcessor& p, int revision, float drive, int os)
{
    set(p, "aSatType", 1);
    set(p, "bSatType", 1);
    set(p, "gritRevision", static_cast<float>(revision));
    set(p, "aDrive", drive);
    set(p, "bDrive", drive);
    set(p, "osRealtime", static_cast<float>(os));
}
// RMS of the last second of a 2 s, 0.25-peak sine at 48 kHz, defaults otherwise (autogain on).
int levels()
{
    constexpr double rate = 48000.;
    constexpr int size = 256;
    for (int revision : {2, 0})
        for (float drive : {6.f, 12.f, 18.f})
            for (int os = 0; os < 4; ++os)
                for (double hz : {20., 50., 100., 200.})
                {
                    BqtAudioProcessor p;
                    configureGrit(p, revision, drive, os);
                    p.setPlayConfigDetails(2, 2, rate, size);
                    p.prepareToPlay(rate, size);
                    juce::AudioBuffer<float> b(2, size);
                    juce::MidiBuffer midi;
                    double squared = 0;
                    long count = 0;
                    for (long offset = 0; offset < static_cast<long>(rate * 2); offset += size)
                    {
                        for (int i = 0; i < size; ++i)
                        {
                            const auto x = static_cast<float>(
                                .25 * std::sin(2 * juce::MathConstants<double>::pi * hz * (offset + i) / rate));
                            b.setSample(0, i, x);
                            b.setSample(1, i, x);
                        }
                        p.processBlock(b, midi);
                        if (offset >= static_cast<long>(rate))
                            for (int i = 0; i < size; ++i)
                            {
                                const double y = b.getSample(0, i);
                                squared += y * y;
                                ++count;
                            }
                    }
                    std::printf("LEVEL,%d,%.0f,%d,%.0f,%.12g\n", revision, drive, os, hz,
                                std::sqrt(squared / static_cast<double>(count)));
                }
    return 0;
}
// processBlock wall time as a percentage of real time: 48 kHz, 256-sample blocks, stereo
// programme, Grit at Drive 12. Median of five 10 s runs per case.
int cpu()
{
    constexpr double rate = 48000.;
    constexpr int size = 256;
    for (int revision : {2, 0})
        for (int os = 0; os < 4; ++os)
        {
            std::array<double, 5> runs {};
            for (auto& run : runs)
            {
                BqtAudioProcessor p;
                configureGrit(p, revision, 12.f, os);
                p.setPlayConfigDetails(2, 2, rate, size);
                p.prepareToPlay(rate, size);
                juce::AudioBuffer<float> b(2, size);
                juce::MidiBuffer midi;
                double seconds = 0;
                for (long offset = 0; offset < static_cast<long>(rate * 10); offset += size)
                {
                    for (int i = 0; i < size; ++i)
                    {
                        const double t = (offset + i) / rate;
                        b.setSample(0, i, static_cast<float>(.3 * std::sin(2 * juce::MathConstants<double>::pi * 61 * t)
                                                             + .1 * std::sin(2 * juce::MathConstants<double>::pi * 1709 * t)));
                        b.setSample(1, i, static_cast<float>(.25 * std::sin(2 * juce::MathConstants<double>::pi * 83 * t)));
                    }
                    const auto start = std::chrono::steady_clock::now();
                    p.processBlock(b, midi);
                    seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                }
                run = 100 * seconds / 10;
            }
            std::sort(runs.begin(), runs.end());
            std::printf("CPU,%d,%d,%.6f\n", revision, os, runs[2]);
        }
    return 0;
}
} // namespace
int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    juce::ScopedJuceInitialiser_GUI init;
    juce::ScopedNoDenormals noDenormals;
    if (std::string(argv[1]) == "--levels")
        return levels();
    if (std::string(argv[1]) == "--cpu")
        return cpu();
    if (! juce::File(juce::String(argv[1])).createDirectory().wasOk())
        return 2;
    int count = 0;
    for (double rate : {44100., 48000., 96000., 192000.})
        for (int os = 0; os < 4; ++os)
            for (int vintage = 0; vintage < 2; ++vintage)
                for (int revision = 0; revision < 3; ++revision)
                    for (int route = 0; route < 2; ++route)
                    {
                        BqtAudioProcessor p;
                        set(p, "aSatType", 1);
                        set(p, "bSatType", 1);
                        set(p, "satLink", 0);
                        set(p, "eqLink", 0);
                        set(p, "aMix", 100);
                        set(p, "bMix", 63);
                        set(p, "aOutputTrim", -1.5f);
                        set(p, "bOutputTrim", .75f);
                        set(p, "gritRevision", static_cast<float>(revision));
                        set(p, "vintage", static_cast<float>(vintage));
                        set(p, "osRealtime", static_cast<float>(os));
                        set(p, "satMode", static_cast<float>(route));
                        set(p, "autoGain", static_cast<float>(route));
                        const auto name = std::string(argv[1]) + "/rate" +
                                          std::to_string(static_cast<int>(rate)) + "_os" +
                                          std::to_string(os) + "_v" + std::to_string(vintage) + "_r" +
                                          std::to_string(revision) + "_ms" + std::to_string(route) + ".f32";
                        FILE* f = std::fopen(name.c_str(), "wb");
                        if (! f)
                            return 2;
                        p.setPlayConfigDetails(2, 2, rate, block);
                        p.prepareToPlay(rate, block);
                        bool ok = render(p, f, rate, vintage, os, route, 128);
                        // Same instance, retained unequal controls and histories, then explicit new prepare.
                        const double nextRate = rate == 192000. ? 44100. : 192000.;
                        p.setPlayConfigDetails(2, 2, nextRate, block);
                        p.prepareToPlay(nextRate, block);
                        ok = render(p, f, nextRate, 1 - vintage, (os + 1) % 4, 1 - route, 128) && ok;
                        ok = std::fclose(f) == 0 && ok;
                        if (! ok)
                        {
                            std::fprintf(stderr, "nonfinite/write failure: %s\n", name.c_str());
                            return 3;
                        }
                        ++count;
                    }
    // Lab's only DSP-related compile-time difference: pre-v2 missing-revision migration.
    for (int route = 0; route < 2; ++route)
    {
        BqtAudioProcessor p;
        set(p, "aSatType", 1);
        set(p, "bSatType", 1);
        set(p, "satLink", 0);
        set(p, "satMode", static_cast<float>(route));
        set(p, "vintage", 1);
        set(p, "osRealtime", 3);
        auto state = p.state().copyState();
        for (int i = state.getNumChildren(); --i >= 0;)
            if (state.getChild(i).getProperty("id").toString() == "gritRevision")
                state.removeChild(i, nullptr);
        state.removeProperty("bqstStateVersion", nullptr);
        auto xml = state.createXml();
        juce::MemoryBlock memory;
        juce::AudioProcessor::copyXmlToBinary(*xml, memory);
        p.setStateInformation(memory.getData(), static_cast<int>(memory.getSize()));
        const float expected = BQST_GRIT_LAB ? 1.f : 0.f;
        if (p.state().getRawParameterValue("gritRevision")->load() != expected)
            return 4;
        const auto name = std::string(argv[1]) + "/migration_ms" + std::to_string(route) + ".f32";
        FILE* f = std::fopen(name.c_str(), "wb");
        if (! f)
            return 2;
        p.setPlayConfigDetails(2, 2, 48000, block);
        p.prepareToPlay(48000, block);
        bool ok = render(p, f, 48000, 1, 3, route, 128);
        ok = std::fclose(f) == 0 && ok;
        if (! ok)
            return 3;
        ++count;
    }
    std::printf("rendered%d finite cases,LAB%d\n", count, BQST_GRIT_LAB);
    return 0;
}
