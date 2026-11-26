#pragma once

#include "../src/BqtMetering.h"
#include <array>

inline const char* resumeLabel(bool bypassed)
{
    return bypassed ? "resumed callbacks continue the bypassed needle from where it was shown"
                    : "resumed callbacks continue the needle from where it was shown";
}

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
    // Bypass rest: resting pushes move the needle to the left stop with the same movement as
    // losing the signal, then hold it there exactly; releasing it rises like a needle from rest.
    {
        const double rate = 48000.0;
        const int block = 48; // 1 ms
        std::array<float, 48> loud {}, silent {};
        loud.fill(static_cast<float>(1.0 / (bqt::MeterCapture::sineCalibration * bqt::MeterCapture::zeroVuGain)));
        bqt::MeterCapture resting, silenced, fresh;
        for (auto* c : { &resting, &silenced, &fresh })
        {
            c->prepare(rate, block / rate);
            c->beginCallback(0.0, block / rate, true);
        }
        double t = 0.0;
        for (int i = 0; i < 1000; ++i) // 1 s at 0 VU
        {
            t += 0.001;
            resting.push(loud.data(), block, rate, t);
            silenced.push(loud.data(), block, rate, t);
        }
        check(resting.level(t) > 0.99f, "fed meter reads 0 VU before bypass");

        bool sameAsSilence = true, straightToStop = true;
        int restTick = -1;
        float previous = resting.level(t);
        for (int i = 0; i < 3000; ++i) // 3 s bypassed, loud signal still arriving
        {
            t += 0.001;
            resting.push(loud.data(), block, rate, t, true);
            silenced.push(silent.data(), block, rate, t);
            const auto r = resting.level(t);
            if (restTick < 0 && r == 0.0f && silenced.level(t) != 0.0f
                && std::abs(silenced.level(t)) < bqt::MeterCapture::restPosition)
                restTick = i;
            if (restTick < 0)
            {
                sameAsSilence = sameAsSilence && r == silenced.level(t);
                // May touch the stop at the 1.25% undershoot, never swing back up visibly.
                straightToStop = straightToStop && (r <= previous || r < 2.0e-4f);
            }
            else if (r != 0.0f)
            {
                check(false, "bypassed needle stays exactly at rest despite the signal");
                break;
            }
            previous = r;
        }
        check(sameAsSilence, "bypass fall uses exactly the movement of losing the signal");
        check(straightToStop, "bypass fall goes straight to the stop");
        check(restTick > 300 && restTick < 1500, "bypass fall reaches exact rest in about a second");

        // Release: from rest, the same movement as a fresh meter fed the same signal.
        bool sameAsFresh = true;
        for (int i = 0; i < 1000; ++i)
        {
            t += 0.001;
            resting.push(loud.data(), block, rate, t);
            fresh.push(loud.data(), block, rate, t);
            sameAsFresh = sameAsFresh && resting.level(t) == fresh.level(t);
        }
        check(sameAsFresh && resting.level(t) > 0.99f, "after bypass the needle rises from rest normally");

        // Rapid toggling mid-fall and mid-rise: never faster than the movement itself allows.
        float fastestStep = 0.0f, fastestToggledStep = 0.0f;
        {
            bqt::MeterCapture step;
            step.prepare(rate, block / rate);
            step.beginCallback(0.0, block / rate, true);
            float last = 0.0f;
            for (int i = 0; i < 2000; ++i)
            {
                step.push(i < 1000 ? loud.data() : silent.data(), block, rate, 0.001 * (i + 1));
                const auto r = step.level(0.001 * (i + 1));
                fastestStep = std::max(fastestStep, std::abs(r - last));
                last = r;
            }
        }
        float last = resting.level(t);
        for (int i = 0; i < 2000; ++i)
        {
            t += 0.001;
            const auto bypassed = (i / 37) % 2 == 0; // toggles every 37 ms
            resting.push(loud.data(), block, rate, t, bypassed);
            const auto r = resting.level(t);
            fastestToggledStep = std::max(fastestToggledStep, std::abs(r - last));
            last = r;
        }
        check(fastestToggledStep <= fastestStep * 1.0001f, "rapid bypass toggling moves no faster than a full step and release");
    }

    // Callbacks that stop and resume (the host stops processing), bypassed or not: the needle
    // carries on from where it was shown, released by the movement, and never jumps back.
    for (const bool bypassed : { false, true })
        for (const double pause : { 0.05, 0.3, 1.0, 5.0 })
        {
            const double rate = 48000.0;
            std::array<float, 48> loud {}, silent {};
            loud.fill(static_cast<float>(1.0 / (bqt::MeterCapture::sineCalibration * bqt::MeterCapture::zeroVuGain)));
            bqt::MeterCapture capture;
            capture.prepare(rate, 0.001);
            double t = 100.0;
            for (int i = 0; i < 1000; ++i)
            {
                t += 0.001;
                capture.beginCallback(t, 0.001, false);
                capture.push(loud.data(), 48, rate, t);
            }
            // A few bypassed callbacks start the fall, then the host stops calling.
            for (int i = 0; i < (bypassed ? 20 : 0); ++i)
            {
                t += 0.001;
                capture.beginCallback(t, 0.001, false);
                capture.push(loud.data(), 48, rate, t, true);
            }
            t += pause;
            const auto shown = capture.level(t);
            if (pause >= 5.0)
                check(shown == 0.0f, "a long stop releases the shown needle to rest");
            // A callback that pushes nothing, then a real one 1 ms later.
            capture.beginCallback(t, 0.0, false);
            capture.push(silent.data(), 0, rate, t);
            check(capture.level(t) == shown, "an empty callback after a stop leaves the shown needle where it was");
            t += 0.001;
            capture.beginCallback(t, 0.001, false);
            capture.push(bypassed ? loud.data() : silent.data(), 48, rate, t, bypassed);
            const auto resumed = capture.level(t);
            check(resumed <= shown + 1.0e-4f && resumed >= shown - 0.02f,
                  resumeLabel(bypassed));
        }
    // Realtime -> offline render: the first offline block never depends on wall time, however
    // long the pause before it.
    {
        const double rate = 48000.0;
        std::array<float, 48> loud {};
        loud.fill(static_cast<float>(1.0 / (bqt::MeterCapture::sineCalibration * bqt::MeterCapture::zeroVuGain)));
        float firstOffline[2] {};
        for (const double pause : { 0.0, 1.0 })
        {
            bqt::MeterCapture capture;
            capture.prepare(rate, 0.001);
            double t = 100.0;
            for (int i = 0; i < 1000; ++i)
            {
                t += 0.001;
                capture.beginCallback(t, 0.001, false);
                capture.push(loud.data(), 48, rate, t);
            }
            t += 0.001 + pause;
            capture.beginCallback(t, 0.001, true);
            capture.push(loud.data(), 48, rate, t);
            firstOffline[pause > 0.0 ? 1 : 0] = capture.level(t);
        }
        check(firstOffline[0] == firstOffline[1] && firstOffline[0] > 0.99f,
              "offline render after a realtime pause is independent of wall time");
    }

    // A stop, one resumed block, then another stop: the stop grace comes from the callback
    // spacing (here unknown again), never from the length of the first stop.
    {
        const double rate = 48000.0;
        std::array<float, 48> loud {};
        loud.fill(static_cast<float>(1.0 / (bqt::MeterCapture::sineCalibration * bqt::MeterCapture::zeroVuGain)));
        bqt::MeterCapture capture;
        capture.prepare(rate, 0.001);
        double t = 100.0;
        for (int i = 0; i < 1000; ++i)
        {
            t += 0.001;
            capture.beginCallback(t, 0.001, false);
            capture.push(loud.data(), 48, rate, t);
        }
        t += 0.150;
        capture.beginCallback(t, 0.001, false);
        capture.push(loud.data(), 48, rate, t, true);
        const auto resumed = capture.level(t);
        const auto later = capture.level(t + bqt::MeterCapture::unknownSpacingGrace + 0.02);
        check(resumed > 0.05f && later < resumed - 0.01f,
              "a second stop right after a resume releases after the normal grace, not twice the first stop");
    }
}
