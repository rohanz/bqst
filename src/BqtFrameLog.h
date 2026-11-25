#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <locale>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace bqt
{
// Lifetime belongs to the processor, never to an editor. No audio-thread clocks or I/O.
struct FrameLogAudio
{
    std::atomic<unsigned> readers { 0 };
    std::atomic<uint64_t> blocks { 0 }, samples { 0 };
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    void record(unsigned count) noexcept
    {
        if (readers.load(std::memory_order_relaxed) != 0)
        {
            samples.fetch_add(count, std::memory_order_relaxed);
            blocks.fetch_add(1, std::memory_order_relaxed);
        }
    }
};

// Written by the needle clock's thread (one writer), read by the log's writer thread. Owned by the
// clock, which outlives the editor's log. All counters are cumulative except gapMaxMs, which the
// log's writer takes and resets each interval.
struct FrameLogNeedle
{
    std::atomic<uint64_t> presents { 0 };   // needle positions committed to the system compositor
    std::atomic<double> gapMaxMs { 0.0 };   // longest time between two commits while running
    std::atomic<double> cpuMs { 0.0 };      // clock-thread CPU time spent inside ticks
    std::atomic<double> refreshHz { 0.0 };  // the clock's display link refresh rate
    std::atomic<double> screenMaxFps { 0.0 }; // the screen's maximum refresh rate (message thread)
    static_assert(std::atomic<double>::is_always_lock_free);

    void recordPresent(double gapMs, double tickCpuMs) noexcept
    {
        presents.fetch_add(1, std::memory_order_relaxed);
        cpuMs.store(cpuMs.load(std::memory_order_relaxed) + tickCpuMs, std::memory_order_relaxed);
        auto seen = gapMaxMs.load(std::memory_order_relaxed);
        while (gapMs > seen && ! gapMaxMs.compare_exchange_weak(seen, gapMs, std::memory_order_relaxed)) {}
    }
};

// One producer (editor/message thread), one consumer (writer thread). The bounded
// queue drops diagnostics rather than ever delaying the editor behind a slow disk.
class FrameLog
{
public:
    static double nowMs()
    {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // Empty path is off. Manual mode is a headless test entry point: the caller
    // supplies timestamps and calls writeInterval, using exactly the production writer.
    // needleSource, if given, must outlive the log.
    FrameLog(const std::string& path, FrameLogAudio& source, bool background = true,
             double startMs = nowMs(), FrameLogNeedle* needleSource = nullptr)
        : audio(source), needle(needleSource), intervalStart(startMs)
    {
        if (path.empty()) return;
        output.open(path, std::ios::out | std::ios::app);
        if (! output) return;
        output.imbue(std::locale::classic());
        previousBlocks = audio.blocks.load(std::memory_order_relaxed);
        previousSamples = audio.samples.load(std::memory_order_relaxed);
        if (needle != nullptr)
        {
            previousPresents = needle->presents.load(std::memory_order_relaxed);
            previousNeedleCpuMs = needle->cpuMs.load(std::memory_order_relaxed);
            needle->gapMaxMs.store(0.0, std::memory_order_relaxed);
        }
        audio.readers.fetch_add(1, std::memory_order_relaxed);
        active = true;
        if (background)
            writer = std::thread([this]
            {
                std::unique_lock<std::mutex> lock(wakeMutex);
                while (! wake.wait_for(lock, std::chrono::seconds(1), [this] { return stopping; }))
                    writeInterval(nowMs());
            });
    }

    ~FrameLog()
    {
        if (! active) return;
        {
            std::lock_guard<std::mutex> lock(wakeMutex);
            stopping = true;
        }
        wake.notify_one();
        if (writer.joinable()) writer.join();
        audio.readers.fetch_sub(1, std::memory_order_relaxed);
    }

    bool enabled() const noexcept { return active; }
    void frame(double startMs, double durationMs, bool nativeA, bool nativeB) noexcept
    { push({ startMs, durationMs, true, nativeA, nativeB }); }
    void timer(double startMs, double durationMs) noexcept
    { push({ startMs, durationMs, false, false, false }); }

    // Consumer only; public to allow deterministic tests without an editor/event loop.
    void writeInterval(double endMs)
    {
        if (! active || endMs <= intervalStart) return;
        std::array<double, capacity> gaps {};
        size_t gapCount = 0, frames = 0, timers = 0, over25 = 0, over50 = 0;
        double frameMs = 0, timerMs = 0, frameMax = 0, timerMax = 0;
        size_t nativeFrames = 0, paintedFrames = 0, mixedFrames = 0;
        auto read = readIndex.load(std::memory_order_relaxed);
        const auto end = writeIndex.load(std::memory_order_acquire);
        while (read != end)
        {
            const auto e = events[read % capacity];
            ++read;
            if (e.isFrame)
            {
                ++frames;
                frameMs += e.duration;
                frameMax = std::max(frameMax, e.duration);
                if (hasPreviousFrame)
                {
                    const auto gap = e.start - previousFrame;
                    gaps[gapCount++] = gap;
                    over25 += gap > 25.0;
                    over50 += gap > 50.0;
                }
                previousFrame = e.start;
                hasPreviousFrame = true;
                if (e.nativeA && e.nativeB) ++nativeFrames;
                else if (! e.nativeA && ! e.nativeB) ++paintedFrames;
                else ++mixedFrames;
            }
            else
            {
                ++timers;
                timerMs += e.duration;
                timerMax = std::max(timerMax, e.duration);
            }
        }
        readIndex.store(read, std::memory_order_release);
        std::sort(gaps.begin(), gaps.begin() + static_cast<std::ptrdiff_t>(gapCount));
        const auto blocks = audio.blocks.load(std::memory_order_relaxed);
        const auto samples = audio.samples.load(std::memory_order_relaxed);
        const auto blockDelta = blocks - previousBlocks;
        const auto seconds = (endMs - intervalStart) / 1000.0;
        uint64_t presents = previousPresents;
        double needleCpuMs = previousNeedleCpuMs, needleGapMs = 0, refreshHz = 0, screenMaxFps = 0;
        if (needle != nullptr)
        {
            presents = needle->presents.load(std::memory_order_relaxed);
            needleCpuMs = needle->cpuMs.load(std::memory_order_relaxed);
            needleGapMs = needle->gapMaxMs.exchange(0.0, std::memory_order_relaxed);
            refreshHz = needle->refreshHz.load(std::memory_order_relaxed);
            screenMaxFps = needle->screenMaxFps.load(std::memory_order_relaxed);
        }
        output << std::fixed << std::setprecision(3)
               << "interval_ms=" << endMs - intervalStart
               << " frames=" << frames
               << " gap_max_ms=" << (gapCount ? gaps[gapCount - 1] : 0)
               << " gap_p95_ms=" << (gapCount ? gaps[static_cast<size_t>(std::ceil(0.95 * static_cast<double>(gapCount))) - 1] : 0)
               << " gaps_gt25=" << over25 << " gaps_gt50=" << over50
               << " callback_ms=" << frameMs << " callback_max_ms=" << frameMax
               << " timers=" << timers << " timer_ms=" << timerMs << " timer_max_ms=" << timerMax
               << " audio_blocks_per_s=" << static_cast<double>(blockDelta) / seconds
               << " samples_per_block=" << (blockDelta ? static_cast<double>(samples - previousSamples) / static_cast<double>(blockDelta) : 0)
               << " native_frames=" << nativeFrames << " painted_frames=" << paintedFrames
               << " mixed_frames=" << mixedFrames
               << " refresh_hz=" << refreshHz << " screen_max_fps=" << screenMaxFps
               << " needle_presents_per_s=" << static_cast<double>(presents - previousPresents) / seconds
               << " needle_gap_max_ms=" << needleGapMs
               << " needle_cpu_ms=" << needleCpuMs - previousNeedleCpuMs
               << " dropped=" << dropped.exchange(0, std::memory_order_relaxed) << '\n';
        output.flush();
        intervalStart = endMs;
        previousBlocks = blocks;
        previousSamples = samples;
        previousPresents = presents;
        previousNeedleCpuMs = needleCpuMs;
    }

    class Scope
    {
    public:
        Scope(FrameLog* target, bool display) : log(target), isFrame(display), start(log ? nowMs() : 0) {}
        ~Scope()
        {
            if (! log) return;
            const auto duration = nowMs() - start;
            if (isFrame) log->frame(start, duration, nativeA, nativeB);
            else log->timer(start, duration);
        }
        bool nativeA = false, nativeB = false;
    private:
        FrameLog* log;
        bool isFrame;
        double start;
    };

private:
    struct Event { double start, duration; bool isFrame, nativeA, nativeB; };
    static constexpr size_t capacity = 4096;
    void push(Event event) noexcept
    {
        if (! active) return;
        const auto write = writeIndex.load(std::memory_order_relaxed);
        if (write - readIndex.load(std::memory_order_acquire) == capacity)
        {
            dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        events[write % capacity] = event;
        writeIndex.store(write + 1, std::memory_order_release);
    }
    FrameLogAudio& audio;
    FrameLogNeedle* needle;
    std::ofstream output;
    bool active = false, stopping = false, hasPreviousFrame = false;
    double intervalStart, previousFrame = 0, previousNeedleCpuMs = 0;
    uint64_t previousBlocks = 0, previousSamples = 0, previousPresents = 0;
    std::array<Event, capacity> events {};
    std::atomic<size_t> writeIndex { 0 }, readIndex { 0 }, dropped { 0 };
    std::thread writer;
    std::mutex wakeMutex;
    std::condition_variable wake;
};
}
