// Renders a fixed set of scenarios through the real processor and writes raw float32 output, so
// behaviour-preserving refactors can be checked bit-for-bit: build, render, refactor, re-render, cmp.
// Not a pass/fail test: the bytes only need to match the same compiler + build before and after.
#include "../src/PluginProcessor.h"

#include <cstdio>
#include <vector>

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;

void setParam(BqtAudioProcessor& p, const char* id, float plain)
{
    auto* parameter = p.state().getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
}

struct Scenario
{
    std::initializer_list<std::pair<const char*, float>> params;
};

const Scenario scenarios[] {
    { { { "aDrive", 6.0f }, { "bDrive", 6.0f } } },
    { { { "aDrive", 12.0f }, { "bDrive", 12.0f }, { "vintage", 1.0f } } },
    { { { "aDrive", 18.0f }, { "bDrive", 18.0f }, { "osRealtime", 0.0f } } },
    { { { "aDrive", 9.0f }, { "bDrive", 9.0f }, { "aSatType", 1.0f }, { "bSatType", 1.0f } } },
    { { { "aDrive", 9.0f }, { "bDrive", 4.0f }, { "satMode", 1.0f }, { "eqMode", 1.0f }, { "aHighGain", 3.0f } } },
    { { { "aDrive", 10.0f }, { "bDrive", 10.0f }, { "aMix", 50.0f }, { "bMix", 50.0f }, { "autoGain", 0.0f }, { "osRealtime", 3.0f } } },
    { { { "aLowGain", 4.0f }, { "bHighGain", -3.0f }, { "inputTrim", 6.0f } } },
};
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: BqstGoldenRender <out.f32>\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juce;
    std::FILE* out = std::fopen(argv[1], "wb");
    if (out == nullptr)
        return 2;

    for (const auto& scenario : scenarios)
    {
        BqtAudioProcessor processor;
        for (const auto& [id, value] : scenario.params)
            setParam(processor, id, value);
        processor.setPlayConfigDetails(2, 2, rate, block);
        processor.prepareToPlay(rate, block);

        juce::Random random(1234);
        juce::AudioBuffer<float> buffer(2, block);
        juce::MidiBuffer midi;
        double phase = 0.0;
        for (int b = 0; b < static_cast<int>(rate) / block; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                const auto sine = 0.6f * static_cast<float>(std::sin(phase));
                buffer.setSample(0, i, sine + 0.05f * (random.nextFloat() - 0.5f));
                buffer.setSample(1, i, 0.8f * sine + 0.05f * (random.nextFloat() - 0.5f));
                phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / rate;
            }
            processor.processBlock(buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                std::fwrite(buffer.getReadPointer(ch), sizeof(float), block, out);
        }
    }

    std::fclose(out);
    return 0;
}
