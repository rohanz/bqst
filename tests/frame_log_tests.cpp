#include "../src/BqtFrameLog.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>

static void require(bool condition, const char* message)
{
    if (! condition) throw std::runtime_error(message);
}
static std::string read(const std::string& path)
{
    std::ifstream input(path);
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}
int main()
{
    const auto path = "bqst-frame-log-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt";
    try
    {
        bqt::FrameLogAudio audio;
        {
            bqt::FrameLog off("", audio, false, 0);
            require(! off.enabled(), "empty path must be off");
            audio.record(512);
            off.frame(10, 1, false, false);
            off.timer(10, 2);
            off.writeInterval(1000);
            require(audio.blocks.load() == 0 && audio.samples.load() == 0, "off counters changed");
            require(! std::ifstream(path).good(), "off created output");
        }
        {
            bqt::FrameLog log(path, audio, false, 0);
            require(log.enabled(), "log failed to open");
            for (unsigned i = 0; i < 100; ++i) audio.record(i % 2 ? 256 : 512);
            log.frame(10, 1, true, true);
            log.frame(26, 2, false, false);
            log.frame(52, 3, true, false);
            log.frame(112, 4, true, true);
            log.timer(20, 2);
            log.timer(40, 3);
            log.writeInterval(1000);
            auto lines = read(path);
            require(lines.find("frames=4 gap_max_ms=60.000 gap_p95_ms=60.000 gaps_gt25=2 gaps_gt50=1") != std::string::npos, "gap metrics wrong");
            require(lines.find("callback_ms=10.000 callback_max_ms=4.000 timers=2 timer_ms=5.000 timer_max_ms=3.000") != std::string::npos, "duration metrics wrong");
            require(lines.find("audio_blocks_per_s=100.000 samples_per_block=384.000 native_frames=2 painted_frames=1 mixed_frames=1 refresh_hz=0.000 screen_max_fps=0.000 needle_presents_per_s=0.000 needle_gap_max_ms=0.000 needle_cpu_ms=0.000 dropped=0") != std::string::npos, "audio/presentation metrics wrong");
            log.writeInterval(2000); // A completely stalled message thread still emits a line.
            log.frame(2112, 1, false, false);
            log.writeInterval(3000);
            lines = read(path);
            require(std::count(lines.begin(), lines.end(), '\n') == 3, "one line per interval");
            require(lines.find("frames=0 gap_max_ms=0.000") != std::string::npos, "empty interval missing");
            require(lines.find("frames=1 gap_max_ms=2000.000") != std::string::npos, "cross-interval stall lost");
            for (int i = 0; i < 5000; ++i) log.timer(3100, 1);
            log.writeInterval(4000);
            require(read(path).find("dropped=904") != std::string::npos, "bounded queue did not report overflow");
            for (int i = 0; i < 100; ++i) log.frame(4010 + i * 10, 0.5, false, false);
            log.writeInterval(5100);
            require(read(path).find("frames=100 gap_max_ms=1898.000 gap_p95_ms=10.000") != std::string::npos, "p95 incorrectly used maximum");
        }
        {
            // Needle clock counters: presents per second, longest gap (reset each interval), CPU,
            // and the refresh rates, next to the display-callback figures.
            const auto needlePath = path + ".needle";
            bqt::FrameLogNeedle needle;
            needle.recordPresent(0.0, 0.25); // before the log opens: not counted
            {
                bqt::FrameLog log(needlePath, audio, false, 0, &needle);
                needle.refreshHz.store(120.0);
                needle.screenMaxFps.store(120.0);
                for (int i = 0; i < 120; ++i)
                    needle.recordPresent(i == 60 ? 25.0 : 8.333, 0.01);
                log.frame(10, 1, true, true);
                log.writeInterval(1000);
                for (int i = 0; i < 30; ++i)
                    needle.recordPresent(8.333, 0.01);
                log.writeInterval(1500);
                log.writeInterval(2500);
            }
            const auto lines = read(needlePath);
            std::remove(needlePath.c_str());
            require(lines.find("refresh_hz=120.000 screen_max_fps=120.000 needle_presents_per_s=120.000 needle_gap_max_ms=25.000 needle_cpu_ms=1.200") != std::string::npos,
                    "needle metrics wrong");
            require(lines.find("needle_presents_per_s=60.000 needle_gap_max_ms=8.333 needle_cpu_ms=0.300") != std::string::npos,
                    "needle gap not reset per interval or rate not per actual interval");
            require(lines.find("needle_presents_per_s=0.000 needle_gap_max_ms=0.000 needle_cpu_ms=0.000") != std::string::npos,
                    "idle needle interval wrong");
        }
        require(audio.readers.load() == 0, "counter activation leaked after close");
        const auto before = read(path);
        { bqt::FrameLog off("", audio, false, 0); off.writeInterval(1000); }
        require(read(path) == before, "disabled logger wrote output");
        // Exercise actual background writer, without a GUI, mouse, focus or env var.
        {
            bqt::FrameLog log(path, audio);
            { bqt::FrameLog::Scope timing(&log, true); }
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        }
        require(read(path).size() > before.size(), "background writer did not flush");
        std::remove(path.c_str());
        std::cout << "Frame log tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::remove(path.c_str());
        std::cerr << error.what() << '\n';
        return 1;
    }
}
