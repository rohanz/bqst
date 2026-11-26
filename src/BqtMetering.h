#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>

namespace bqt
{
// A damped moving-coil movement: x'' + 2*a*x' + w*w*x = w*w*input.
// zeta = 0.8127169864 gives 1.25% overshoot; w = 13.51191256 rad/s
// places the first 99% crossing at 300 ms. State is amplitude relative to 0 VU.
class VuBallistics
{
public:
    static constexpr double stepSeconds = 0.001;
    static constexpr double decay = 10.981360858709374;
    static constexpr double frequency = 7.872832704200884;
    // After this long without callbacks the released movement is treated as exactly at rest.
    static constexpr double releasedSeconds = 4.0;

    void tick(double target)
    {
        // Exact constant-input transition over 1 ms, precomputed once, no transcendentals.
        const auto error = position - target;
        const auto next = target + 0.9999093801283596 * error + 0.000989068496783596 * velocity;
        velocity = -0.18057599708523703 * error + 0.9781867439740358 * velocity;
        position = next;
    }

    // Used only when callbacks stop: the same differential equation with zero input.
    // This is a pure snapshot projection, never a second display smoothing stage.
    static double released(double x, double v, double seconds)
    {
        if (seconds <= 0.0)
            return x;
        if (seconds >= releasedSeconds)
            return 0.0;
        return std::exp(-decay * seconds) * (x * std::cos(frequency * seconds)
            + (v + decay * x) / frequency * std::sin(frequency * seconds));
    }

    // The velocity of that same released motion (the time derivative of released()).
    static double releasedVelocity(double x, double v, double seconds)
    {
        if (seconds <= 0.0)
            return v;
        if (seconds >= releasedSeconds)
            return 0.0;
        return std::exp(-decay * seconds) * (v * std::cos(frequency * seconds)
            - (decay * v + (decay * decay + frequency * frequency) * x) / frequency * std::sin(frequency * seconds));
    }

    // Carries the movement through `seconds` of released motion, exactly as level() showed it.
    void release(double seconds)
    {
        if (seconds <= 0.0)
            return;
        const auto x = released(position, velocity, seconds);
        velocity = releasedVelocity(position, velocity, seconds);
        position = x;
    }

    double position = 0.0, velocity = 0.0;
};

// Audio-thread-owned full-wave rectifier and movement. A 1 ms area-preserving
// integrator splits samples at tick boundaries, independent of rate and host blocks.
// Only atomic snapshots cross threads; neither side locks or allocates.
class MeterCapture
{
public:
    static constexpr double sineCalibration = 1.1107207345395915;
    static constexpr double zeroVuGain = 7.943282347242816; // -18 dBFS RMS = 0 VU
    static constexpr double unknownSpacingGrace = 0.1;

    void prepare(double rate, double maximumBlockSeconds)
    {
        sampleRate = rate;
        // Initial stop-detection grace; beginCallback uses the actual host block and spacing.
        idleGrace = std::max(0.002, maximumBlockSeconds);
        movement = {};
        area = 0.0;
        remaining = VuBallistics::stepSeconds;
        lastCallbackTime = 0.0;
        publish(0.0);
    }

    // Once per host block, before internal splitting or oversampling. Wall time only
    // detects stopped realtime callbacks: within the grace (steady callbacks, any offline
    // render) it never touches the movement.
    void beginCallback(double now, double blockSeconds, bool nonRealtime)
    {
        // Callbacks that stopped and resumed: for the gap, level() showed the needle released by
        // the movement itself. Carry the movement through that same release first, so the needle
        // continues from where it was shown instead of jumping back to where audio left it.
        // An offline render never depends on wall time, so it never takes this release.
        const auto stoppedFor = nonRealtime ? 0.0 : now - publishedTime.load() - publishedGrace.load();
        movement.release(stoppedFor);
        // The grace follows the host's callback spacing. Until a second callback shows it, and
        // after a stop (whose gap is not a spacing), allow a generous one.
        const auto spacingKnown = lastCallbackTime > 0.0 && stoppedFor <= 0.0;
        idleGrace = nonRealtime ? std::numeric_limits<double>::infinity()
                                : std::max({ 0.002, 2.0 * blockSeconds,
                                             spacingKnown ? 2.0 * (now - lastCallbackTime) : unknownSpacingGrace });
        lastCallbackTime = now;
        // Published at once, so a callback that pushes nothing cannot have the gap released twice.
        if (stoppedFor > 0.0)
            publish(now);
    }

    // resting drives the movement to the left stop instead of the signal, at the same sample
    // pace (the plugin's bypass): the needle falls exactly as it does when the signal stops, and
    // once it is within an invisible distance of the stop it is set there exactly and stays.
    void push(const float* samples, int size, double rate, double now, bool resting = false)
    {
        // A rate/oversampling change changes the integration clock, not the coil's
        // physical position or velocity. Preserve the partial tick as well.
        if (! std::isfinite(rate) || rate <= 0.0 || size <= 0)
            return;
        sampleRate = rate;
        const auto sampleSeconds = 1.0 / sampleRate;
        for (int i = 0; i < size; ++i)
        {
            const auto rectified = ! resting && std::isfinite(samples[i]) ? std::abs(samples[i]) : 0.0f;
            auto duration = sampleSeconds;
            while (duration > 0.0)
            {
                const auto part = std::min(duration, remaining);
                area += rectified * part;
                duration -= part;
                remaining -= part;
                if (remaining < 1.0e-12)
                {
                    movement.tick(area / VuBallistics::stepSeconds * sineCalibration * zeroVuGain);
                    if (resting && std::abs(movement.position) < restPosition && std::abs(movement.velocity) < restVelocity)
                        movement = {};
                    area = 0.0;
                    remaining = VuBallistics::stepSeconds;
                }
            }
        }
        publish(now);
    }

    float level(double now) const
    {
        // Bounded reader: the audio writer never waits for the editor. Atomic fields
        // also make unsuccessful snapshot reads data-race-free.
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto before = sequence.load();
            if ((before & 1u) != 0)
                continue;
            const auto x = publishedPosition.load();
            const auto v = publishedVelocity.load();
            const auto time = publishedTime.load();
            const auto grace = publishedGrace.load();
            if (sequence.load() == before)
                return static_cast<float>(std::max(0.0, VuBallistics::released(x, v, now - time - grace)));
        }
        return static_cast<float>(std::max(0.0, publishedPosition.load()));
    }

    // Snap distance to the stop: 1e-4 of 0 VU is under a tenth of a pixel at the largest editor
    // size; the velocity bound means the needle would not cover that again in the next 10 ms.
    static constexpr double restPosition = 1.0e-4, restVelocity = 1.0e-2;

private:
    void publish(double now)
    {
        sequence.fetch_add(1);
        publishedPosition.store(movement.position);
        publishedVelocity.store(movement.velocity);
        publishedTime.store(now);
        publishedGrace.store(idleGrace);
        sequence.fetch_add(1);
    }

    static_assert(std::atomic<double>::is_always_lock_free && std::atomic<uint32_t>::is_always_lock_free);
    std::atomic<uint32_t> sequence { 0 };
    std::atomic<double> publishedPosition { 0.0 }, publishedVelocity { 0.0 };
    std::atomic<double> publishedTime { 0.0 }, publishedGrace { 0.002 };
    VuBallistics movement;
    double sampleRate = 44100.0, area = 0.0, remaining = VuBallistics::stepSeconds, idleGrace = 0.002, lastCallbackTime = 0.0;
};
}
