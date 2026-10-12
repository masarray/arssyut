#include "app/recording_pause_clock.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>

void require(bool ok, const char *message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    using arssyut::app::RecordingPauseClock;
    constexpr std::int64_t s = 1'000'000'000;
    constexpr std::int64_t second = 10'000'000;
    RecordingPauseClock c;
    c.reset(s);
    require(!c.paused() && c.elapsed(s) == 0, "Start at media zero");
    require(!c.resume(s), "Resume without pause rejected");
    require(c.pause(s+10*second), "Pause recording at 10s");
    require(c.media_now(s+15*second) == s+10*second,
            "Paused time freezes same virtual QPC for both streams");
    require(c.excluded_ticks(s+15*second) == 5*second,
            "Excluded time accumulates only while paused");
    require(!c.pause(s+15*second), "Nested Pause rejected");
    require(c.resume(s+15*second), "Resume 5 seconds later");
    require(c.media_now(s+25*second) == s+20*second,
            "10+5 paused+10 recorded = 20-second MP4 PTS");
    require(c.pause_count() == 1 && c.committed_excluded_ticks() == 5*second,
            "Single owner counts committed pauses");
    require(c.pause(s+28*second), "Second pause");
    require(c.elapsed(s+40*second) == 23*second,
            "Stop while paused keeps last encoded-media elapsed");
    require(c.resume(s+40*second), "Second resume");
    require(c.elapsed(s+42*second) == 25*second,
            "Multiple pauses never duplicate encoded-media clock");
    c.reset(s);
    require(c.excluded_ticks(s+second) == 0 && c.pause_count() == 0,
            "New recording clears prior pause state");
    std::cout << "PASS single-owner Pause/QPC time compression\n";
}
