#include "tools/p8w1_webcam_probe_flags.hpp"

#include <iostream>

int main()
{
    using arssyut::webcam_probe::ReadEvent;
    using arssyut::webcam_probe::classify_read_event;
    int failures = 0;
    const auto check = [&](ReadEvent actual, ReadEvent expected, const char *name) {
        if (actual != expected) {
            ++failures;
            std::cerr << "FAIL: " << name << '\n';
        }
    };

    check(classify_read_event(true, 0), ReadEvent::Frame, "ordinary frame");
    check(classify_read_event(false, 0), ReadEvent::UnexpectedEmpty,
          "null sample without flags is not a harmless tick");
    check(classify_read_event(false, MF_SOURCE_READERF_STREAMTICK),
          ReadEvent::StreamGap, "gap event without frame");
    check(classify_read_event(true, MF_SOURCE_READERF_STREAMTICK),
          ReadEvent::UnexpectedEmpty, "sample with gap flag is unexpected");
    check(classify_read_event(false, MF_SOURCE_READERF_ENDOFSTREAM),
          ReadEvent::EndOfStream, "end of stream");
    check(classify_read_event(true, MF_SOURCE_READERF_ERROR),
          ReadEvent::ReaderError, "error takes precedence over sample");
    check(classify_read_event(false, MF_SOURCE_READERF_NEWSTREAM),
          ReadEvent::StreamConfigurationChanged, "new stream");
    check(classify_read_event(false, MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED),
          ReadEvent::StreamConfigurationChanged, "native type changed");
    check(classify_read_event(true, MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED),
          ReadEvent::StreamConfigurationChanged, "current type changed");
    check(classify_read_event(false, MF_SOURCE_READERF_ALLEFFECTSREMOVED),
          ReadEvent::StreamConfigurationChanged, "transform pipeline changed");
    check(classify_read_event(false,
          MF_SOURCE_READERF_STREAMTICK | MF_SOURCE_READERF_ERROR),
          ReadEvent::ReaderError, "error dominates tick");
    check(classify_read_event(false,
          MF_SOURCE_READERF_STREAMTICK | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED),
          ReadEvent::StreamConfigurationChanged, "format change dominates tick");
    check(classify_read_event(true, 0x40000000U),
          ReadEvent::UnexpectedEmpty, "unknown bits cannot be ignored");

    std::cout << "P8W1 Source Reader flag classification: " <<
        (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}
