#pragma once

#include "../src/BqtMetering.h"
#include <array>

template <typename Check>
void testMetering(Check check)
{
    bqt::VuBallistics step;
    double peak = 0.0;
    for (int tick = 1; tick <= 2000; ++tick)
    {
        step.tick(1.0);
        peak = std::max(peak, step.position);
        if (tick == 300)
            check(std::abs(step.position - 0.99) < 1.0e-9, "VU reaches 99% at 300 ms");
    }
    check(peak > 1.01 && peak < 1.015, "VU overshoot is between 1 and 1.5 percent");
    for (int tick = 0; tick < 2000; ++tick)
        step.tick(0.0);
    check(std::abs(step.position) < 1.0e-8, "physical movement returns to rest");

    std::array<float, 2048> samples {};
    // Identical samples must give identical movement despite slow render scheduling.
    for (const bool offline : { false, true })
    {
        bqt::MeterCapture fast, slow;
        fast.prepare(48000.0, 0.001);
        slow.prepare(48000.0, 0.001);
        samples.fill(static_cast<float>(1.0 / (bqt::MeterCapture::sineCalibration * bqt::MeterCapture::zeroVuGain)));
        for (int i = 1; i <= 2000; ++i)
        {
            fast.beginCallback(i * 0.001, 0.001, offline);
            slow.beginCallback(i * 0.010, 0.001, offline);
            fast.push(samples.data(), 48, 48000.0, i * 0.001);
            slow.push(samples.data(), 48, 48000.0, i * 0.010);
            check(fast.level(i * 0.001) == slow.level(i * 0.010), "wall-clock gaps never alter sample-time movement");
        }
        check(std::abs(slow.level(20.009) - 1.0f) < 1.0e-6f, "stop grace covers actual callback spacing");
        if (offline)
            check(slow.level(100.0) == slow.level(20.0), "offline render never projects wall-clock silence");
        else
            check(slow.level(25.0) == 0.0f, "realtime stop still returns displayed needle to rest");
        slow.beginCallback(21.0, 0.5, offline);
        slow.push(samples.data(), 48, 48000.0, 21.0);
        check(slow.level(21.49) == slow.level(21.0), "oversized host block grace survives internal chunking");
    }

    for (const double rate : { 44100.0, 96000.0 })
    {
        float referenceStep = 0.0f, referenceSine = 0.0f;
        for (const int blockSize : { 32, 512, 2048 })
        {
            bqt::MeterCapture capture;
            capture.prepare(rate, blockSize / rate);
            // A full-wave rectified step of exactly one calibrated VU.
            samples.fill(static_cast<float>(-1.0 / (bqt::MeterCapture::sineCalibration * bqt::MeterCapture::zeroVuGain)));
            int offset = 0;
            float maximum = 0.0f;
            for (const double endTime : { 0.3, 1.0 })
            {
                const auto end = static_cast<int>(std::round(rate * endTime));
                while (offset < end)
                {
                    const auto size = std::min(blockSize, end - offset);
                    offset += size;
                    capture.push(samples.data(), size, rate, offset / rate);
                    maximum = std::max(maximum, capture.level(offset / rate));
                }
                if (endTime < 1.0)
                {
                    const auto reading = capture.level(endTime);
                    check(std::abs(reading - 0.99f) < 1.0e-6f, "rectified step reaches 99% at 300 ms at every rate and block size");
                    if (blockSize == 32) referenceStep = reading;
                    check(std::abs(reading - referenceStep) < 1.0e-7f, "step deflection is block-size independent");
                }
            }
            check(maximum > 1.01f && maximum < 1.015f, "captured step overshoot meets VU standard");
            const auto grace = std::max(0.002, blockSize / rate);
            const auto stopped = capture.level(1.0 + grace + 0.3);
            check(stopped > 0.009f && stopped < 0.011f, "stopped callbacks release by the same 300 ms movement");
            check(capture.level(5.1) == 0.0f, "stopped callbacks fall fully to rest");

            samples.fill(0.0f);
            for (int i = 0; i < static_cast<int>(rate * 2);)
            {
                const auto size = std::min(blockSize, static_cast<int>(rate * 2) - i);
                i += size;
                capture.push(samples.data(), size, rate, 1.0 + i / rate);
            }
            check(capture.level(3.0) < 1.0e-7f, "silent callbacks return the audio movement to rest");

            capture.prepare(rate, blockSize / rate);
            // A -18 dBFS RMS sine must still read exactly 0 VU, including the
            // original average-responding sine calibration (not RMS detection).
            const auto amplitude = std::sqrt(2.0) / bqt::MeterCapture::zeroVuGain;
            for (int i = 0; i < static_cast<int>(rate * 2);)
            {
                const auto size = std::min(blockSize, static_cast<int>(rate * 2) - i);
                for (int j = 0; j < size; ++j)
                    samples[static_cast<size_t>(j)] = static_cast<float>(amplitude * std::sin(
                        2.0 * 3.14159265358979323846 * 1000.0 * (i + j) / rate));
                i += size;
                capture.push(samples.data(), size, rate, i / rate);
            }
            const auto sine = capture.level(2.0);
            if (blockSize == 32) referenceSine = sine;
            check(std::abs(sine - referenceSine) < 1.0e-7f, "steady sine calibration is identical across block sizes");
            check(std::abs(sine - 1.0f) < 0.0005f, "-18 dBFS RMS sine reads 0 VU at every rate");
            // Processing-rate changes (including oversampling/bypass) must preserve
            // the physical state rather than throw the needle back to its stop.
            samples.fill(static_cast<float>(1.0 / (bqt::MeterCapture::sineCalibration * bqt::MeterCapture::zeroVuGain)));
            capture.push(samples.data(), 48, 48000.0, 2.001);
            check(std::abs(capture.level(2.001) - 1.0f) < 0.0005f,
                  "processing-rate change preserves physical deflection");
            capture.prepare(rate, blockSize / rate);
            check(capture.level(2.0) == 0.0f, "prepare clears movement and partial rectifier integral");
        }
    }
}
