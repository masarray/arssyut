#include "core/audio/audio_source_timeline_mapper.hpp"

#include <cstdint>
#include <iostream>
#include <limits>

namespace {

using namespace arssyut::core::audio;

struct Test {
    int checks = 0;
    int failures = 0;

    void expect(bool value, const char *message)
    {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

AudioSourcePacket packet(
    std::int64_t start_qpc,
    AudioTimestampQuality quality,
    std::uint32_t frames = 480)
{
    AudioSourcePacket result;
    result.source = AudioSourceId::Microphone;
    result.native_format = canonical_audio_profile().program_format;
    result.frame_count = frames;
    result.timing.quality = quality;
    result.timing.packet_start_qpc_100ns = start_qpc;
    result.timing.host_observed_qpc_100ns =
        start_qpc >
                std::numeric_limits<
                    std::int64_t>::max() - 10
            ? std::numeric_limits<
                  std::int64_t>::max()
            : start_qpc + 10;
    result.timing.device_frame_position = 9'600;
    result.flags = AudioPacketFlag::Silent;
    result.pool_slot = kInvalidAudioPoolSlot;
    result.payload_bytes = 0;
    return result;
}

void test_alignment(Test &test)
{
    AudioSourceTimelineMapper mapper;
    constexpr std::int64_t zero = 10'000'000;
    mapper.reset(zero);

    auto on_zero =
        mapper.map(
            packet(
                zero,
                AudioTimestampQuality::
                    DeviceQpcTrusted));

    test.expect(
        on_zero.status ==
            AudioTimelineMapStatus::Mapped,
        "packet at session zero maps normally");
    test.expect(
        on_zero.media_start_100ns == 0 &&
        on_zero.canonical_start_frame == 0,
        "session-zero packet maps to frame zero");
    test.expect(
        on_zero.drift_eligible &&
        on_zero.drift_anchor.trusted,
        "trusted device/QPC packet exports drift anchor");

    auto host =
        mapper.map(
            packet(
                zero + 1'000'000,
                AudioTimestampQuality::
                    HostQpcFallback));

    test.expect(
        host.usable_for_timeline(),
        "host fallback remains usable for timeline continuity");
    test.expect(
        !host.drift_eligible,
        "host fallback never enters drift estimator");
    test.expect(
        host.canonical_start_frame == 4'800,
        "100 ms offset maps to exact canonical frame");
}

void test_pre_zero_trim(Test &test)
{
    AudioSourceTimelineMapper mapper;
    constexpr std::int64_t zero = 50'000'000;
    mapper.reset(zero);

    auto overlap =
        mapper.map(
            packet(
                zero - 50'000,
                AudioTimestampQuality::
                    ContinuityReconstructed,
                480));

    test.expect(
        overlap.status ==
            AudioTimelineMapStatus::
                OverlapsMediaZero,
        "packet crossing zero is retained");
    test.expect(
        overlap.source_frames_before_zero == 240,
        "5 ms before zero trims exactly 240 frames at 48 kHz");
    test.expect(
        overlap.canonical_start_frame == 0,
        "overlap resumes exactly at canonical frame zero");
    test.expect(
        !overlap.drift_eligible,
        "reconstructed continuity never becomes drift evidence");

    auto stale =
        mapper.map(
            packet(
                zero - 200'000,
                AudioTimestampQuality::
                    DeviceQpcTrusted,
                480));

    test.expect(
        stale.status ==
            AudioTimelineMapStatus::
                FullyBeforeMediaZero,
        "packet ending before media zero is fully trimmed");
    test.expect(
        stale.source_frames_before_zero == 480,
        "fully pre-zero packet trims all native frames");
}

void test_fractional_overlap(Test &test)
{
    AudioSourceTimelineMapper mapper;
    constexpr std::int64_t zero = 90'000'000;
    mapper.reset(zero);

    auto one_tick_before =
        packet(
            zero - 1,
            AudioTimestampQuality::
                DeviceQpcTrusted,
            480);
    one_tick_before.native_format.sample_rate =
        44'100;

    const auto mapped =
        mapper.map(one_tick_before);

    test.expect(
        mapped.status ==
            AudioTimelineMapStatus::
                OverlapsMediaZero,
        "fractional-rate packet crossing zero remains usable");
    test.expect(
        mapped.source_frames_before_zero == 1,
        "one tick before zero trims one 44.1 kHz source frame");
    test.expect(
        mapped.media_start_100ns > 0 &&
        mapped.canonical_start_frame == 1,
        "retained first frame keeps its post-zero offset");

    auto near_boundary =
        packet(
            zero - 472,
            AudioTimestampQuality::
                DeviceQpcTrusted,
            480);
    near_boundary.native_format.sample_rate =
        44'100;

    const auto boundary_mapped =
        mapper.map(near_boundary);

    test.expect(
        boundary_mapped.status ==
            AudioTimelineMapStatus::
                OverlapsMediaZero &&
        boundary_mapped.source_frames_before_zero == 3,
        "472-tick deficit trims exactly three 44.1 kHz frames");
    test.expect(
        boundary_mapped.canonical_start_frame == 0,
        "rational retained position stays before the first 48 kHz boundary");

    auto fully_trimmed =
        packet(
            zero - 226,
            AudioTimestampQuality::
                HostQpcFallback,
            1);
    fully_trimmed.native_format.sample_rate =
        44'100;

    const auto all_trimmed =
        mapper.map(fully_trimmed);

    test.expect(
        all_trimmed.status ==
            AudioTimelineMapStatus::
                FullyBeforeMediaZero &&
        all_trimmed.source_frames_before_zero == 1,
        "ceil duration cannot leave a zero-frame overlap classified usable");
}

void test_invalid_discontinuity_and_zero(Test &test)
{
    AudioSourceTimelineMapper mapper;
    mapper.reset(1'000);

    auto invalid =
        packet(
            0,
            AudioTimestampQuality::
                HostQpcFallback);
    invalid.flags |=
        AudioPacketFlag::Discontinuity;

    const auto mapped = mapper.map(invalid);

    test.expect(
        mapped.status ==
            AudioTimelineMapStatus::Invalid &&
        mapped.discontinuity,
        "rejected packet still preserves discontinuity evidence");

    mapper.reset(-1);
    const auto bad_zero =
        mapper.map(
            packet(
                std::numeric_limits<std::int64_t>::max(),
                AudioTimestampQuality::
                    DeviceQpcTrusted));

    test.expect(
        bad_zero.status ==
            AudioTimelineMapStatus::Invalid,
        "negative session-zero fails closed without signed subtraction");
}

void test_rejection(Test &test)
{
    AudioSourceTimelineMapper mapper;
    mapper.reset(1'000);

    auto invalid =
        packet(
            0,
            AudioTimestampQuality::
                HostQpcFallback);

    const auto mapped = mapper.map(invalid);

    test.expect(
        mapped.status ==
            AudioTimelineMapStatus::Invalid,
        "missing packet-start QPC fails closed");
    test.expect(
        mapper.rejected_packets() == 1,
        "invalid timing is counted");
}

} // namespace

int main()
{
    Test test;
    test_alignment(test);
    test_pre_zero_trim(test);
    test_fractional_overlap(test);
    test_invalid_discontinuity_and_zero(test);
    test_rejection(test);

    if (test.failures != 0)
        return 1;

    std::cout
        << "PASS: " << test.checks
        << " source timeline mapper checks\n";
    return 0;
}
