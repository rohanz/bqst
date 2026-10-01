// Chain-level tests: these drive the real BqtAudioProcessor through processBlock.
//
// tests/dsp_tests.cpp covers the pure helpers in BqtDsp.h and deliberately does not link JUCE.
// That left the actual processing chain untested, which is how the drive -> 0 coloration step
// hid for so long: the bare curve function really does return its input at zero drive, so the
// "zero-drive bypass" assertion passed while the chain around it snapped roughly 1.9 dB of tilt
// into place the instant drive left zero. Everything here exercises the chain, not the helpers.

#include "../src/BqtPresetManager.h"
#include "../src/PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <vector>

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

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

void setParam(BqtAudioProcessor& processor, const juce::String& id, float plainValue)
{
    auto* parameter = processor.state().getParameter(id);
    jassert(parameter != nullptr);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

// Runs `seconds` of a sine through the processor and returns the output of the left channel.
std::vector<float> render(BqtAudioProcessor& processor, double frequency, float amplitude, double seconds)
{
    const auto total = static_cast<int>(sampleRate * seconds);
    std::vector<float> out;
    out.reserve(static_cast<size_t>(total));

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    auto phase = 0.0;
    const auto increment = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;

    for (int done = 0; done < total; done += blockSize)
    {
        const auto count = juce::jmin(blockSize, total - done);
        buffer.setSize(2, count, false, false, true);
        for (int i = 0; i < count; ++i)
        {
            const auto value = amplitude * static_cast<float>(std::sin(phase));
            buffer.setSample(0, i, value);
            buffer.setSample(1, i, value);
            phase += increment;
        }

        processor.processBlock(buffer, midi);
        for (int i = 0; i < count; ++i)
            out.push_back(buffer.getSample(0, i));
    }

    return out;
}

std::unique_ptr<BqtAudioProcessor> makeProcessor()
{
    auto processor = std::make_unique<BqtAudioProcessor>();
    processor->setPlayConfigDetails(2, 2, sampleRate, blockSize);
    processor->prepareToPlay(sampleRate, blockSize);
    return processor;
}

double rms(const std::vector<float>& samples, size_t from)
{
    auto sum = 0.0;
    for (auto i = from; i < samples.size(); ++i)
        sum += static_cast<double>(samples[i]) * samples[i];
    return std::sqrt(sum / static_cast<double>(samples.size() - from));
}

double mean(const std::vector<float>& samples, size_t from)
{
    auto sum = 0.0;
    for (auto i = from; i < samples.size(); ++i)
        sum += samples[i];
    return sum / static_cast<double>(samples.size() - from);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // 1. Continuity of the FULL chain as drive -> 0. This is the regression that motivated the
    //    whole test target: the coloration filters used to engage at full strength the instant
    //    drive left zero. Checked for both curves and with vintage both off and on.
    for (auto vintage : { false, true })
        for (const char* type : { "Cream", "Grit" })
        {
            auto silent = makeProcessor();
            setParam(*silent, "vintage", vintage ? 1.0f : 0.0f);
            setParam(*silent, "aSatType", type[0] == 'C' ? 0.0f : 1.0f);
            setParam(*silent, "bSatType", type[0] == 'C' ? 0.0f : 1.0f);
            setParam(*silent, "aDrive", 0.0f);
            setParam(*silent, "bDrive", 0.0f);
            const auto atZero = render(*silent, 220.0, 0.4f, 0.35);

            auto nudged = makeProcessor();
            setParam(*nudged, "vintage", vintage ? 1.0f : 0.0f);
            setParam(*nudged, "aSatType", type[0] == 'C' ? 0.0f : 1.0f);
            setParam(*nudged, "bSatType", type[0] == 'C' ? 0.0f : 1.0f);
            setParam(*nudged, "aDrive", 0.05f);
            setParam(*nudged, "bDrive", 0.05f);
            const auto atEpsilon = render(*nudged, 220.0, 0.4f, 0.35);

            auto worst = 0.0f;
            for (size_t i = atZero.size() / 2; i < atZero.size(); ++i)
                worst = std::fmax(worst, std::abs(atZero[i] - atEpsilon[i]));

            check(worst < 0.002f, "chain is continuous as drive approaches zero");
            if (worst >= 0.002f)
                std::printf("  (%s, vintage %s: peak difference %.5f)\n", type, vintage ? "on" : "off", worst);
        }

    // 2. Zero drive is a true bypass of the saturation stage: unity gain, no coloration.
    {
        auto processor = makeProcessor();
        setParam(*processor, "aDrive", 0.0f);
        setParam(*processor, "bDrive", 0.0f);
        const auto out = render(*processor, 1000.0, 0.5f, 0.3);
        check(std::abs(rms(out, out.size() / 2) - 0.5 / std::sqrt(2.0)) < 0.002,
              "zero drive passes the signal at unity");
    }

    // 3. The asymmetric curves must not leave a DC offset on the output (fixed by the wet-path
    //    DC blocker). Measured -0.059 at full Cream drive before the fix.
    for (const char* type : { "Cream", "Grit" })
    {
        auto processor = makeProcessor();
        setParam(*processor, "aSatType", type[0] == 'C' ? 0.0f : 1.0f);
        setParam(*processor, "bSatType", type[0] == 'C' ? 0.0f : 1.0f);
        setParam(*processor, "aDrive", 18.0f);
        setParam(*processor, "bDrive", 18.0f);
        const auto out = render(*processor, 110.0, 0.9f, 0.6);
        const auto offset = std::abs(mean(out, out.size() / 2));
        check(offset < 2.0e-3, "saturation leaves no DC offset");
        if (offset >= 2.0e-3)
            std::printf("  (%s: DC offset %.5f)\n", type, offset);
    }

    // 4. A parameter ramp must take the same wall-clock time at every oversampling factor.
    //    The smoothers are consumed per oversampled sample, so before the fix a 20 ms ramp
    //    finished in 10 ms at 2x and 2.5 ms at 8x.
    {
        std::vector<double> rampSamples;
        for (float os : { 0.0f, 1.0f, 3.0f })
        {
            auto processor = makeProcessor();
            setParam(*processor, "osRealtime", os);
            setParam(*processor, "aOutputTrim", 0.0f);
            setParam(*processor, "bOutputTrim", 0.0f);
            render(*processor, 1000.0, 0.5f, 0.2); // settle

            setParam(*processor, "aOutputTrim", -12.0f);
            setParam(*processor, "bOutputTrim", -12.0f);
            const auto out = render(*processor, 1000.0, 0.5f, 0.12);

            // Find where the envelope has fallen 90% of the way to the new level.
            const auto startLevel = 0.5, endLevel = 0.5 * std::pow(10.0, -12.0 / 20.0);
            const auto threshold = static_cast<float>(startLevel - 0.9 * (startLevel - endLevel));
            size_t crossing = 0;
            for (size_t i = 0; i + 64 < out.size(); ++i)
            {
                auto peak = 0.0f;
                for (size_t j = i; j < i + 64; ++j)
                    peak = std::fmax(peak, std::abs(out[j]));
                if (peak < threshold) { crossing = i; break; }
            }
            rampSamples.push_back(static_cast<double>(crossing) / sampleRate);
        }

        const auto spread = *std::max_element(rampSamples.begin(), rampSamples.end())
                          - *std::min_element(rampSamples.begin(), rampSamples.end());
        check(spread < 0.006, "parameter ramp length does not depend on the oversampling factor");
        std::printf("ramp to -12 dB: OS off %.1f ms, 2x %.1f ms, 8x %.1f ms\n",
                    rampSamples[0] * 1000.0, rampSamples[1] * 1000.0, rampSamples[2] * 1000.0);
    }

    // 5. M/S encode -> decode must round-trip to unity. Both stages in M/S, everything neutral.
    {
        auto processor = makeProcessor();
        setParam(*processor, "eqMode", 1.0f);
        setParam(*processor, "satMode", 1.0f);
        setParam(*processor, "aDrive", 0.0f);
        setParam(*processor, "bDrive", 0.0f);
        const auto out = render(*processor, 440.0, 0.5f, 0.3);
        check(std::abs(rms(out, out.size() / 2) - 0.5 / std::sqrt(2.0)) < 0.002,
              "M/S encode and decode round-trip to unity");
    }

    // 6. A host block larger than the one prepareToPlay was given must not corrupt memory.
    //    juce::dsp::Oversampling only jasserts its bounds, so this used to write out of range.
    {
        auto processor = makeProcessor();
        setParam(*processor, "osRealtime", 3.0f); // 8x, the largest internal buffer
        juce::AudioBuffer<float> oversized(2, blockSize * 4);
        juce::MidiBuffer midi;
        oversized.clear();
        for (int i = 0; i < oversized.getNumSamples(); ++i)
        {
            const auto v = 0.4f * static_cast<float>(std::sin(0.05 * i));
            oversized.setSample(0, i, v);
            oversized.setSample(1, i, v);
        }
        processor->processBlock(oversized, midi);

        auto finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < oversized.getNumSamples(); ++i)
                finite = finite && std::isfinite(oversized.getSample(ch, i));
        check(finite, "an oversized host block is handled without corruption");
    }

    // 7. A zero-length block must not poison the meters (0/0 used to latch NaN forever).
    {
        auto processor = makeProcessor();
        juce::AudioBuffer<float> empty(2, 0);
        juce::MidiBuffer midi;
        processor->processBlock(empty, midi);
        render(*processor, 1000.0, 0.5f, 0.1);
        check(std::isfinite(processor->getMeterLevel(0)) && std::isfinite(processor->getMeterLevel(1)),
              "a zero-length block does not latch NaN into the meters");
    }

    // 8. Flipping a discrete switch mid-signal must not produce a step discontinuity. Each of
    //    these used to jump coefficients or reroute channels with live filter state.
    {
        struct Switch { const char* id; float from; float to; };
        const Switch switches[] {
            { "eqMode",    0.0f, 1.0f },   // L/R -> M/S
            { "satMode",   0.0f, 1.0f },
            { "eqBypass",  0.0f, 1.0f },
            { "satBypass", 0.0f, 1.0f },
            { "vintage",   0.0f, 1.0f },
            { "aHighFreq", 0.0f, 7.0f },   // 1.6 kHz -> 18 kHz
            { "aLowFreq",  0.0f, 7.0f },
            { "aSatType",  0.0f, 1.0f },
        };

        for (const auto& item : switches)
        {
            auto processor = makeProcessor();
            setParam(*processor, "aDrive", 6.0f);
            setParam(*processor, "bDrive", 6.0f);
            setParam(*processor, "aHighGain", 4.0f);
            setParam(*processor, "aLowGain", 4.0f);
            setParam(*processor, item.id, item.from);

            juce::AudioBuffer<float> buffer(2, blockSize);
            juce::MidiBuffer midi;
            auto phase = 0.0;
            const auto increment = 2.0 * juce::MathConstants<double>::pi * 220.0 / sampleRate;
            std::vector<float> out;

            const auto totalBlocks = 60;
            for (int block = 0; block < totalBlocks; ++block)
            {
                if (block == totalBlocks / 3)
                    setParam(*processor, item.id, item.to);

                for (int i = 0; i < blockSize; ++i)
                {
                    const auto v = 0.5f * static_cast<float>(std::sin(phase));
                    buffer.setSample(0, i, v);
                    buffer.setSample(1, i, v);
                    phase += increment;
                }
                processor->processBlock(buffer, midi);
                for (int i = 0; i < blockSize; ++i)
                    out.push_back(buffer.getSample(0, i));
            }

            // Largest sample-to-sample jump. A 220 Hz sine at 0.5 moves at most ~0.015 per
            // sample, so anything much above that is a discontinuity rather than signal.
            auto worstStep = 0.0f;
            for (size_t i = 1; i < out.size(); ++i)
                worstStep = std::fmax(worstStep, std::abs(out[i] - out[i - 1]));

            check(worstStep < 0.05f, "discrete switch does not produce a step discontinuity");
            if (worstStep >= 0.05f)
                std::printf("  (%s: worst sample step %.4f)\n", item.id, worstStep);
        }
    }

    // Loading the Default factory preset restores every musical parameter to its layout default
    // and leaves workflow parameters (oversampling, bypass) untouched.
    {
        auto processor = makeProcessor();
        BqtPresetManager presets(processor->state());
        setParam(*processor, "aDrive", 11.0f);
        setParam(*processor, "vintage", 1.0f);
        setParam(*processor, "bMix", 30.0f);
        setParam(*processor, "osRealtime", 3.0f);
        presets.loadPreset(0);

        auto allDefault = true;
        for (auto* p : processor->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
            {
                const auto id = ranged->getParameterID();
                if (id == "osRealtime")
                    check(std::abs(ranged->convertFrom0to1(ranged->getValue()) - 3.0f) < 1.0e-4f,
                          "loading a preset leaves oversampling alone");
                else if (id != "osRender" && id != "eqBypass" && id != "satBypass" && id != "bypass")
                    allDefault = allDefault && std::abs(ranged->getValue() - ranged->getDefaultValue()) < 1.0e-6f;
            }
        check(allDefault, "Default preset restores every musical parameter to its layout default");
    }

    if (failures == 0)
    {
        std::printf("All chain tests passed.\n");
        return 0;
    }

    std::printf("%d chain test(s) failed.\n", failures);
    return 1;
}
