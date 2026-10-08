#ifdef _WIN32

#include "core/audio/audio_format.hpp"
#include "core/audio/audio_packet.hpp"
#include "platform/windows/audio/wasapi_audio_utils.hpp"
#include "platform/windows/audio/wasapi_microphone_source.hpp"

#include <Windows.h>
#include <ksmedia.h>
#include <mmreg.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

struct TestContext {
    int checks = 0;
    int failures = 0;

    void expect(
        bool condition,
        const char *message)
    {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr
                << "FAIL: "
                << message
                << '\n';
        }
    }
};

WAVEFORMATEXTENSIBLE make_float_stereo()
{
    WAVEFORMATEXTENSIBLE wave{};
    wave.Format.wFormatTag =
        WAVE_FORMAT_EXTENSIBLE;
    wave.Format.nChannels = 2;
    wave.Format.nSamplesPerSec = 48'000;
    wave.Format.wBitsPerSample = 32;
    wave.Format.nBlockAlign = 8;
    wave.Format.nAvgBytesPerSec =
        48'000 * 8;
    wave.Format.cbSize =
        sizeof(WAVEFORMATEXTENSIBLE) -
        sizeof(WAVEFORMATEX);
    wave.Samples.wValidBitsPerSample = 32;
    wave.dwChannelMask =
        arssyut::core::audio::kStereoChannelMask;
    wave.SubFormat =
        KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return wave;
}

void test_wave_format_mapping(
    TestContext &test)
{
    using namespace arssyut;

    auto wave =
        make_float_stereo();

    const auto format =
        windows::audio_format_from_wave_format(
            &wave.Format);
    test.expect(
        format.has_value(),
        "WASAPI float stereo mix format maps to canonical metadata");
    test.expect(
        format.has_value() &&
        format->sample_type ==
            core::audio::AudioSampleType::Float32 &&
        format->sample_rate == 48'000 &&
        format->channels == 2 &&
        format->block_align == 8,
        "Mapped float stereo format preserves negotiated rate/channels/alignment");

    wave.SubFormat =
        KSDATAFORMAT_SUBTYPE_PCM;
    wave.Samples.wValidBitsPerSample = 24;

    const auto pcm24 =
        windows::audio_format_from_wave_format(
            &wave.Format);
    test.expect(
        pcm24.has_value() &&
        pcm24->sample_type ==
            core::audio::AudioSampleType::Pcm24In32,
        "WASAPI 24-valid-bits-in-32 PCM is explicit");

    WAVEFORMATEX packed24{};
    packed24.wFormatTag = WAVE_FORMAT_PCM;
    packed24.nChannels = 2;
    packed24.nSamplesPerSec = 48'000;
    packed24.wBitsPerSample = 24;
    packed24.nBlockAlign = 6;
    packed24.nAvgBytesPerSec =
        48'000 * 6;

    test.expect(
        !windows::audio_format_from_wave_format(
             &packed24).has_value(),
        "Unsupported packed 24-bit PCM is rejected rather than reinterpreted");
}

void test_flag_mapping(
    TestContext &test)
{
    using namespace arssyut;

    const auto flags =
        windows::audio_packet_flags_from_wasapi(
            AUDCLNT_BUFFERFLAGS_SILENT |
            AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY |
            AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR);

    test.expect(
        core::audio::has_flag(
            flags,
            core::audio::AudioPacketFlag::Silent),
        "WASAPI silent flag survives source boundary");
    test.expect(
        core::audio::has_flag(
            flags,
            core::audio::AudioPacketFlag::Discontinuity),
        "WASAPI discontinuity flag survives source boundary");
    test.expect(
        core::audio::has_flag(
            flags,
            core::audio::AudioPacketFlag::TimestampError),
        "WASAPI timestamp-error flag survives source boundary");
}

void test_timestamp_quality(
    TestContext &test)
{
    using namespace arssyut;
    using namespace core::audio;

    windows::WasapiTimestampClassifier timing{
        48'000};

    const auto first =
        timing.observe(
            480,
            0,
            1'000'000,
            1'001'000,
            AudioPacketFlag::None);
    test.expect(
        first.quality ==
            AudioTimestampQuality::DeviceQpcTrusted,
        "First valid device/QPC timestamp becomes trusted");

    const auto second =
        timing.observe(
            480,
            480,
            1'100'000,
            1'101'000,
            AudioPacketFlag::None);
    test.expect(
        second.quality ==
            AudioTimestampQuality::DeviceQpcTrusted,
        "Monotonic plausible device/QPC timing remains trusted");

    const auto bad =
        timing.observe(
            480,
            999'999,
            0,
            1'201'000,
            AudioPacketFlag::TimestampError);
    test.expect(
        bad.quality ==
            AudioTimestampQuality::ContinuityReconstructed &&
        bad.packet_start_qpc_100ns ==
            1'200'000 &&
        bad.device_frame_position == 960,
        "Timestamp error reconstructs exact packet continuity from prior frame count");

    const auto repeated_bad =
        timing.observe(
            480,
            999'999,
            0,
            1'401'000,
            AudioPacketFlag::TimestampError);
    test.expect(
        repeated_bad.quality ==
            AudioTimestampQuality::HostQpcFallback &&
        repeated_bad.packet_start_qpc_100ns ==
            1'301'000 &&
        repeated_bad.device_frame_position == 1'440,
        "Repeated bad timing drops to host-QPC fallback while preserving frame continuity");

    const auto good_one =
        timing.observe(
            480,
            1'920,
            1'400'000,
            1'401'000,
            AudioPacketFlag::None);
    test.expect(
        good_one.quality ==
            AudioTimestampQuality::HostQpcFallback,
        "After repeated bad timing one good packet stays on host-QPC until relock");

    const auto good_two =
        timing.observe(
            480,
            2'400,
            1'500'000,
            1'501'000,
            AudioPacketFlag::None);
    test.expect(
        good_two.quality ==
            AudioTimestampQuality::DeviceQpcTrusted,
        "Two plausible good packets relock device timing explicitly");

    windows::WasapiTimestampClassifier fresh{
        48'000};
    const auto host =
        fresh.observe(
            480,
            0,
            0,
            2'000'000,
            AudioPacketFlag::TimestampError);
    test.expect(
        host.quality ==
            AudioTimestampQuality::HostQpcFallback &&
        host.packet_start_qpc_100ns ==
            1'900'000,
        "Without a trusted anchor timestamp error falls back to host QPC minus packet duration");

    const auto host_again =
        fresh.observe(
            480,
            999'999,
            0,
            2'101'000,
            AudioPacketFlag::TimestampError);
    test.expect(
        host_again.quality ==
            AudioTimestampQuality::HostQpcFallback &&
        host_again.packet_start_qpc_100ns ==
            2'001'000,
        "Repeated bad timestamps without a trusted anchor stay on host-QPC fallback");

    const auto first_good_without_anchor =
        fresh.observe(
            480,
            960,
            2'200'000,
            2'201'000,
            AudioPacketFlag::None);
    test.expect(
        first_good_without_anchor.quality ==
            AudioTimestampQuality::HostQpcFallback,
        "One plausible packet after host fallback is not enough to claim device timing");

    const auto relocked_without_anchor =
        fresh.observe(
            480,
            1'440,
            2'300'000,
            2'301'000,
            AudioPacketFlag::None);
    test.expect(
        relocked_without_anchor.quality ==
            AudioTimestampQuality::DeviceQpcTrusted,
        "Two plausible packets establish a new trusted device-QPC anchor");

    windows::WasapiTimestampClassifier guarded_anchor{
        48'000};

    const auto stale_anchor =
        guarded_anchor.observe(
            480,
            0,
            1'000'000,
            8'000'000,
            AudioPacketFlag::None);
    test.expect(
        stale_anchor.quality ==
            AudioTimestampQuality::HostQpcFallback,
        "Grossly stale fresh device QPC is rejected before it can become trusted");

    const auto future_anchor =
        guarded_anchor.observe(
            480,
            480,
            9'000'000,
            8'100'000,
            AudioPacketFlag::None);
    test.expect(
        future_anchor.quality ==
            AudioTimestampQuality::HostQpcFallback,
        "Grossly future fresh device QPC is rejected before it can become trusted");

    const auto guarded_good_one =
        guarded_anchor.observe(
            480,
            960,
            8'200'000,
            8'201'000,
            AudioPacketFlag::None);
    test.expect(
        guarded_good_one.quality ==
            AudioTimestampQuality::HostQpcFallback,
        "Fresh anchor guard requires one plausible candidate before relock");

    const auto guarded_good_two =
        guarded_anchor.observe(
            480,
            1'440,
            8'300'000,
            8'301'000,
            AudioPacketFlag::None);
    test.expect(
        guarded_good_two.quality ==
            AudioTimestampQuality::DeviceQpcTrusted,
        "Fresh anchor guard relocks after two host-plausible monotonic packets");

    windows::WasapiTimestampClassifier buffered_fallback{
        48'000};

    const auto buffered_one =
        buffered_fallback.observe(
            480,
            0,
            0,
            9'000'000,
            AudioPacketFlag::TimestampError);
    const auto buffered_two =
        buffered_fallback.observe(
            480,
            480,
            0,
            9'001'000,
            AudioPacketFlag::TimestampError);
    const auto buffered_three =
        buffered_fallback.observe(
            480,
            960,
            0,
            9'002'000,
            AudioPacketFlag::TimestampError);

    test.expect(
        buffered_one.quality ==
                AudioTimestampQuality::HostQpcFallback &&
        buffered_two.quality ==
                AudioTimestampQuality::HostQpcFallback &&
        buffered_three.quality ==
                AudioTimestampQuality::HostQpcFallback,
        "Buffered timestamp errors remain explicit host-QPC fallback");

    test.expect(
        buffered_one.packet_start_qpc_100ns == 8'900'000 &&
        buffered_two.packet_start_qpc_100ns == 9'000'000 &&
        buffered_three.packet_start_qpc_100ns == 9'002'000,
        "Host fallback packets stay ordered without inventing future packet starts");

    test.expect(
        buffered_one.packet_start_qpc_100ns <= 9'000'000 &&
        buffered_two.packet_start_qpc_100ns <= 9'001'000 &&
        buffered_three.packet_start_qpc_100ns <= 9'002'000,
        "Every degraded fallback packet start is bounded by its host observation");

    windows::WasapiTimestampClassifier reset_clock{
        48'000};

    const auto before_reset_one =
        reset_clock.observe(
            480,
            10'000,
            3'000'000,
            3'001'000,
            AudioPacketFlag::None);
    const auto before_reset_two =
        reset_clock.observe(
            480,
            10'480,
            3'100'000,
            3'101'000,
            AudioPacketFlag::None);
    test.expect(
        before_reset_one.quality ==
                AudioTimestampQuality::DeviceQpcTrusted &&
        before_reset_two.quality ==
                AudioTimestampQuality::DeviceQpcTrusted,
        "Pre-reset device clock establishes trusted timing");

    const auto reset_first =
        reset_clock.observe(
            480,
            0,
            3'200'000,
            3'201'000,
            AudioPacketFlag::None);
    test.expect(
        reset_first.quality ==
            AudioTimestampQuality::ContinuityReconstructed,
        "One backward frame sample is tentative and preserves the old clock epoch");

    const auto reset_second =
        reset_clock.observe(
            480,
            480,
            3'300'000,
            3'301'000,
            AudioPacketFlag::None);
    test.expect(
        reset_second.quality ==
                AudioTimestampQuality::Discontinuous &&
        reset_second.device_frame_position == 480,
        "A second progressing backward-epoch packet confirms and surfaces the reset");

    const auto reset_third =
        reset_clock.observe(
            480,
            960,
            3'400'000,
            3'401'000,
            AudioPacketFlag::None);
    test.expect(
        reset_third.quality ==
            AudioTimestampQuality::HostQpcFallback,
        "First packet after confirmed reset remains degraded while relocking");

    const auto reset_fourth =
        reset_clock.observe(
            480,
            1'440,
            3'500'000,
            3'501'000,
            AudioPacketFlag::None);
    test.expect(
        reset_fourth.quality ==
            AudioTimestampQuality::DeviceQpcTrusted,
        "Confirmed clock reset relocks after two new-epoch progression packets");

    windows::WasapiTimestampClassifier false_reset{
        48'000};
    (void)false_reset.observe(
        480,
        20'000,
        4'000'000,
        4'001'000,
        AudioPacketFlag::None);
    (void)false_reset.observe(
        480,
        20'480,
        4'100'000,
        4'101'000,
        AudioPacketFlag::None);

    const auto corrupt_backward =
        false_reset.observe(
            480,
            10,
            4'200'000,
            4'201'000,
            AudioPacketFlag::None);
    test.expect(
        corrupt_backward.quality ==
            AudioTimestampQuality::ContinuityReconstructed,
        "Single corrupted backward frame is not enough to rebase the device epoch");

    const auto resumed_old_epoch =
        false_reset.observe(
            480,
            20'960,
            4'200'000,
            4'201'000,
            AudioPacketFlag::None);
    test.expect(
        resumed_old_epoch.quality ==
            AudioTimestampQuality::DeviceQpcTrusted,
        "Old device epoch resumes normally after an unconfirmed backward sample");

    const auto discontinuity =
        timing.observe(
            480,
            2'880,
            1'600'000,
            1'601'000,
            AudioPacketFlag::Discontinuity);
    test.expect(
        discontinuity.quality ==
                AudioTimestampQuality::Discontinuous &&
        discontinuity.packet_start_qpc_100ns ==
                1'600'000,
        "Real WASAPI discontinuity keeps a host-plausible device QPC");

    windows::WasapiTimestampClassifier discontinuity_guard{
        48'000};
    const auto corrupt_discontinuity =
        discontinuity_guard.observe(
            480,
            0,
            1'000'000,
            8'000'000,
            AudioPacketFlag::Discontinuity);
    test.expect(
        corrupt_discontinuity.quality ==
                AudioTimestampQuality::Discontinuous &&
        corrupt_discontinuity.packet_start_qpc_100ns ==
                7'900'000,
        "Discontinuity packet rejects grossly stale device QPC and uses host fallback");
}

arssyut::core::audio::AudioSourcePacket
make_packet(
    std::uint32_t frames,
    arssyut::core::audio::AudioPacketFlag flags =
        arssyut::core::audio::AudioPacketFlag::None)
{
    using namespace arssyut::core::audio;

    return {
        .source = AudioSourceId::Microphone,
        .native_format = {
            .sample_rate = 48'000,
            .sample_type = AudioSampleType::Float32,
            .channels = 2,
            .container_bits_per_sample = 32,
            .valid_bits_per_sample = 32,
            .channel_mask = kStereoChannelMask,
            .block_align = 8,
        },
        .frame_count = frames,
        .timing = {
            .quality =
                AudioTimestampQuality::DeviceQpcTrusted,
            .packet_start_qpc_100ns = 1'000'000,
            .host_observed_qpc_100ns = 1'001'000,
            .device_frame_position = 0,
        },
        .flags = flags,
    };
}

void test_bounded_handoff(
    TestContext &test)
{
    using namespace arssyut;

    windows::WasapiPacketHandoff handoff{
        2,
        1,
        32};

    test.expect(
        handoff.valid(),
        "WASAPI packet handoff is preallocated and bounded");

    std::array<std::byte, 32> bytes{};
    auto packet =
        make_packet(4);

    test.expect(
        handoff.publish(
            packet,
            bytes) ==
            windows::WasapiPacketPublishResult::Published,
        "Non-silent packet copies into retained fixed pool");
    test.expect(
        handoff.queue_depth_approx() == 1,
        "Current bounded SPSC depth is observable separately from high-water");

    core::audio::AudioSourcePacket leased;
    test.expect(
        handoff.try_pop(leased) &&
        handoff.payload(leased).size() == 32,
        "Consumer sees retained packet after WASAPI buffer release boundary");
    test.expect(
        handoff.queue_depth_approx() == 0 &&
        handoff.queue_high_water() == 1,
        "Current queue depth falls after drain while historical high-water remains");

    test.expect(
        handoff.publish(
            packet,
            bytes) ==
            windows::WasapiPacketPublishResult::PoolExhausted,
        "Pool exhaustion drops media instead of allocating");

    test.expect(
        handoff.release(leased),
        "Consumer releases retained packet slot");

    test.expect(
        handoff.publish(
            packet,
            bytes) ==
            windows::WasapiPacketPublishResult::Published,
        "Publishing resumes after retained slot returns");

    core::audio::AudioSourcePacket after_drop;
    test.expect(
        handoff.try_pop(after_drop) &&
        core::audio::has_flag(
            after_drop.flags,
            core::audio::AudioPacketFlag::Discontinuity),
        "First packet after pool exhaustion carries explicit discontinuity");
    handoff.release(after_drop);

    windows::WasapiPacketHandoff silence_handoff{
        1,
        2,
        32};
    auto silence =
        make_packet(
            4,
            core::audio::AudioPacketFlag::Silent);

    test.expect(
        silence_handoff.publish(
            silence,
            {}) ==
            windows::WasapiPacketPublishResult::Published,
        "Silent timeline packet needs no pool allocation");
    test.expect(
        silence_handoff.publish(
            silence,
            {}) ==
            windows::WasapiPacketPublishResult::QueueFull,
        "Ring overflow is explicit and bounded");

    core::audio::AudioSourcePacket first_silence;
    test.expect(
        silence_handoff.try_pop(
            first_silence),
        "Consumer drains first silent packet");
    test.expect(
        silence_handoff.release(
            first_silence),
        "Allocation-free silent packet release is deterministic");

    test.expect(
        silence_handoff.publish(
            silence,
            {}) ==
            windows::WasapiPacketPublishResult::Published,
        "Queue resumes without growth after overflow");

    core::audio::AudioSourcePacket next_silence;
    test.expect(
        silence_handoff.try_pop(
            next_silence) &&
        core::audio::has_flag(
            next_silence.flags,
            core::audio::AudioPacketFlag::Discontinuity),
        "First packet after queue overflow carries explicit discontinuity");
    silence_handoff.release(next_silence);

    test.expect(
        handoff.pool_exhaustions() == 1 &&
        silence_handoff.ring_overflows() == 1,
        "Pool and ring pressure remain observable");
}

void test_generation_safe_packet_lease(
    TestContext &test)
{
    using namespace arssyut;

    auto old_generation =
        std::make_shared<windows::WasapiPacketHandoff>(
            2,
            1,
            32);
    auto old_packet =
        make_packet(4);
    std::array<std::byte, 32> old_bytes{};
    old_bytes[0] = std::byte{0x2a};

    test.expect(
        old_generation->publish(
            old_packet,
            old_bytes) ==
            windows::WasapiPacketPublishResult::Published,
        "Old handoff generation publishes retained packet");

    core::audio::AudioSourcePacket popped_old;
    test.expect(
        old_generation->try_pop(
            popped_old),
        "Old generation packet pops before restart");

    std::weak_ptr<windows::WasapiPacketHandoff>
        old_weak = old_generation;

    windows::WasapiPacketLease old_lease{
        old_generation,
        popped_old};

    auto new_generation =
        std::make_shared<windows::WasapiPacketHandoff>(
            2,
            1,
            32);
    auto new_packet =
        make_packet(4);
    std::array<std::byte, 32> new_bytes{};
    new_bytes[0] = std::byte{0x55};

    test.expect(
        new_generation->publish(
            new_packet,
            new_bytes) ==
            windows::WasapiPacketPublishResult::Published,
        "New handoff generation can start independently");

    old_generation.reset();

    const auto old_payload =
        old_lease.payload();
    test.expect(
        !old_weak.expired() &&
        old_payload.size() == 32 &&
        old_payload[0] == std::byte{0x2a},
        "Packet lease pins its exact old pool generation across restart");

    test.expect(
        new_generation->outstanding_payload_leases() == 1,
        "New generation owns only its own retained payload before old release");

    test.expect(
        old_lease.release(),
        "Old packet releases through its pinned old generation");
    test.expect(
        old_weak.expired(),
        "Old handoff retires immediately after its final packet lease releases");
    test.expect(
        new_generation->outstanding_payload_leases() == 1,
        "Old release cannot decrement or corrupt the new generation pool");

    core::audio::AudioSourcePacket popped_new;
    test.expect(
        new_generation->try_pop(
            popped_new),
        "New generation packet remains readable after old generation retires");
    test.expect(
        new_generation->payload(
            popped_new)[0] == std::byte{0x55},
        "New generation payload remains intact");
    test.expect(
        new_generation->release(
            popped_new),
        "New generation releases its own packet normally");
}

void test_source_lifetime_without_hardware(
    TestContext &test)
{
    using namespace arssyut;

    windows::WasapiMicrophoneSource source;
    test.expect(
        source.snapshot().state ==
            windows::WasapiMicrophoneState::Idle,
        "Microphone source starts idle");

    const auto invalid =
        source.start(L"");
    test.expect(
        !invalid.ok() &&
        invalid.code ==
            core::StatusCode::InvalidArgument,
        "Empty endpoint is rejected before worker creation");

    for (int cycle = 0;
         cycle < 8;
         ++cycle) {
        windows::WasapiMicrophoneSource attempt;
        const auto status =
            attempt.start(
                L"__arssyut_missing_microphone_endpoint__");
        test.expect(
            !status.ok(),
            "Missing endpoint fails as controlled startup error");
        attempt.stop();

        const auto state =
            attempt.snapshot().state;
        test.expect(
            state !=
                windows::WasapiMicrophoneState::Running &&
            state !=
                windows::WasapiMicrophoneState::Starting &&
            state !=
                windows::WasapiMicrophoneState::Stopping,
            "Failed startup leaves no live worker state");
    }
}

} // namespace

int main()
{
    TestContext test;

    test_wave_format_mapping(test);
    test_flag_mapping(test);
    test_timestamp_quality(test);
    test_bounded_handoff(test);
    test_generation_safe_packet_lease(test);
    test_source_lifetime_without_hardware(test);

    if (test.failures != 0) {
        std::cerr
            << test.failures
            << " of "
            << test.checks
            << " checks failed\n";
        return 1;
    }

    std::cout
        << "PASS: "
        << test.checks
        << " P7A2 WASAPI microphone checks\n";
    return 0;
}

#endif
