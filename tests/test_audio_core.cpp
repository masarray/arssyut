#include "core/audio/audio_buffer.hpp"
#include "core/audio/audio_drift.hpp"
#include "core/audio/audio_format.hpp"
#include "core/audio/audio_mix.hpp"
#include "core/audio/audio_packet.hpp"
#include "core/audio/audio_program.hpp"
#include "core/audio/audio_resampler.hpp"
#include "core/audio/audio_time.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <iostream>

namespace {

class ContractProbeResampler final
    : public arssyut::core::audio::IAudioResampler {
public:
    [[nodiscard]] arssyut::core::audio::AudioResampleStatus configure(
        arssyut::core::audio::AudioResamplerConfig config) noexcept override
    {
        using namespace arssyut::core::audio;
        if (!config.valid()) {
            configured_ = false;
            reset();
            return AudioResampleStatus::InvalidArgument;
        }

        config_ = config;
        configured_ = true;
        reset();
        return AudioResampleStatus::Ok;
    }

    void reset() noexcept override
    {
        pending_drain_frames_ = 0;
        delay_100ns_ = 0;
        phase_remainder_numerator_ = 0;
        rate_state_ = {};
    }

    [[nodiscard]] arssyut::core::audio::AudioResampleResult process(
        std::span<const float> input_interleaved,
        std::uint32_t input_frames,
        std::span<float> output_interleaved,
        double rate_adjustment_ppm) noexcept override
    {
        using namespace arssyut::core::audio;
        if (!configured_)
            return {};

        const std::size_t required_input =
            static_cast<std::size_t>(input_frames) * config_.input_channels;
        if (input_interleaved.size() < required_input)
            return {.status = AudioResampleStatus::InvalidArgument};

        const auto capacity = output_interleaved.size() /
            AudioResamplerConfig::kOutputChannels;
        const auto produced = static_cast<std::uint32_t>(
            std::min<std::size_t>(input_frames, capacity));

        pending_drain_frames_ = kTailFrames;
        delay_100ns_ = frames_to_ticks_floor(
            kTailFrames,
            AudioResamplerConfig::kOutputSampleRate);

        // Contract probe deliberately models a backend that clamps and
        // quantizes drift correction. This proves callers can observe the
        // effective value instead of assuming requested ppm was applied.
        const double clamped_ppm =
            std::clamp(rate_adjustment_ppm, -250.0, 250.0);
        const double applied_ppm =
            std::round(clamped_ppm * 2.0) / 2.0;
        phase_remainder_numerator_ =
            (phase_remainder_numerator_ + 3u) % kPhaseDenominator;
        rate_state_ = {
            .requested_rate_adjustment_ppm = rate_adjustment_ppm,
            .applied_rate_adjustment_ppm = applied_ppm,
            .effective_output_per_input_ratio =
                (static_cast<double>(
                     AudioResamplerConfig::kOutputSampleRate) /
                 static_cast<double>(config_.input_sample_rate)) *
                (1.0 + applied_ppm / 1'000'000.0),
            .phase_remainder_numerator = phase_remainder_numerator_,
            .phase_remainder_denominator = kPhaseDenominator,
        };

        return {
            .status = produced == input_frames
                ? AudioResampleStatus::Ok
                : AudioResampleStatus::OutputFull,
            .input_frames_consumed = produced,
            .output_frames_produced = produced,
            .algorithmic_delay_100ns = delay_100ns_,
        };
    }

    [[nodiscard]] std::uint64_t current_delay_100ns() const noexcept override
    {
        return delay_100ns_;
    }

    [[nodiscard]] arssyut::core::audio::AudioResamplerRateState
    current_rate_state() const noexcept override
    {
        return rate_state_;
    }

    [[nodiscard]] std::uint32_t maximum_drain_frames() const noexcept override
    {
        return kTailFrames;
    }

    [[nodiscard]] arssyut::core::audio::AudioResampleDrainResult drain(
        std::span<float> output_interleaved) noexcept override
    {
        using namespace arssyut::core::audio;
        if (!configured_)
            return {};

        if (pending_drain_frames_ == 0) {
            return {
                .status = AudioResampleStatus::Ok,
                .complete = true,
            };
        }

        const auto capacity = output_interleaved.size() /
            AudioResamplerConfig::kOutputChannels;
        if (capacity == 0) {
            return {
                .status = AudioResampleStatus::OutputFull,
                .remaining_delay_100ns = delay_100ns_,
            };
        }

        const auto produced = static_cast<std::uint32_t>(
            std::min<std::size_t>(pending_drain_frames_, capacity));
        pending_drain_frames_ -= produced;
        delay_100ns_ = frames_to_ticks_floor(
            pending_drain_frames_,
            AudioResamplerConfig::kOutputSampleRate);

        return {
            .status = AudioResampleStatus::Ok,
            .output_frames_produced = produced,
            .remaining_delay_100ns = delay_100ns_,
            .complete = pending_drain_frames_ == 0,
        };
    }

private:
    static constexpr std::uint32_t kTailFrames = 3;
    static constexpr std::uint64_t kPhaseDenominator = 8;
    arssyut::core::audio::AudioResamplerConfig config_{};
    arssyut::core::audio::AudioResamplerRateState rate_state_{};
    bool configured_ = false;
    std::uint32_t pending_drain_frames_ = 0;
    std::uint64_t delay_100ns_ = 0;
    std::uint64_t phase_remainder_numerator_ = 0;
};

struct TestContext {
    int checks = 0;
    int failures = 0;

    void expect(bool condition, const char *message)
    {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

void test_format_profile(TestContext &test)
{
    using namespace arssyut::core::audio;

    const auto profile = canonical_audio_profile();
    test.expect(profile.valid(), "Canonical profile is valid");
    test.expect(
        profile.program_format.sample_rate == 48'000,
        "Canonical program rate is fixed at 48 kHz");
    test.expect(
        profile.program_format.channels == 2,
        "Canonical program bus is stereo");
    test.expect(
        profile.program_format.sample_type == AudioSampleType::Float32,
        "Canonical mix domain is float32");
    test.expect(
        profile.canonical_block_frames == 1'024,
        "Canonical output quantum is 1024 frames");

    AudioFormat pcm24{
        .sample_rate = 48'000,
        .sample_type = AudioSampleType::Pcm24In32,
        .channels = 2,
        .container_bits_per_sample = 32,
        .valid_bits_per_sample = 24,
        .channel_mask = kStereoChannelMask,
        .block_align = 8,
    };
    test.expect(pcm24.valid(), "24-bit-in-32 endpoint format is explicit");
    test.expect(
        pcm24.bytes_for_frames(256) == 2'048,
        "Frame byte count uses negotiated block alignment");

    auto invalid_pcm24 = pcm24;
    invalid_pcm24.valid_bits_per_sample = 32;
    test.expect(
        !invalid_pcm24.valid(),
        "PCM24-in-32 rejects contradictory 32 valid bits");

    AudioFormat invalid_pcm16{
        .sample_rate = 48'000,
        .sample_type = AudioSampleType::Pcm16,
        .channels = 1,
        .container_bits_per_sample = 16,
        .valid_bits_per_sample = 1,
        .channel_mask = 0,
        .block_align = 2,
    };
    test.expect(
        !invalid_pcm16.valid(),
        "PCM16 rejects contradictory valid-bit depth");

    auto invalid_layout = pcm24;
    invalid_layout.channel_mask = kSpeakerFrontLeft;
    test.expect(
        !invalid_layout.valid(),
        "Explicit channel mask must match channel count");
}

void test_rational_time(TestContext &test)
{
    using namespace arssyut::core::audio;

    FrameTimeAccumulator accumulator_44100{44'100};
    std::uint64_t ticks = 0;
    for (std::uint32_t frame = 0; frame < 44'100; ++frame)
        ticks += accumulator_44100.advance(1);

    test.expect(
        ticks == kMediaTicksPerSecond,
        "44.1 kHz one-frame accumulation lands exactly at one second");
    test.expect(
        accumulator_44100.remainder() == 0,
        "44.1 kHz carried remainder closes exactly at one second");

    FrameTimeAccumulator accumulator_48000{48'000};
    ticks = 0;
    for (std::uint32_t block = 0; block < 46'875; ++block)
        ticks += accumulator_48000.advance(1'024);

    test.expect(
        ticks == 1'000ull * kMediaTicksPerSecond,
        "1024-frame 48 kHz blocks remain exact over 1000 seconds");

    const std::uint64_t one_hour_frames = 48'000ull * 3'600ull;
    const auto one_hour_ticks =
        frames_to_ticks_floor(one_hour_frames, 48'000);
    test.expect(
        one_hour_ticks == 3'600ull * kMediaTicksPerSecond,
        "Long 48 kHz frame conversion is exact");
    test.expect(
        ticks_to_frames_floor(one_hour_ticks, 48'000) ==
            one_hour_frames,
        "100 ns to frame conversion round-trips exact hour boundary");
}

void test_program_clock_and_alignment(TestContext &test)
{
    using namespace arssyut::core::audio;

    AudioProgramClock clock{250'000};

    CanonicalAudioBlock block_zero;
    CanonicalAudioBlock block_one;
    CanonicalAudioBlock block_many;

    test.expect(
        clock.begin_block(
            0,
            block_zero) &&
        block_zero.media_start_100ns ==
            250'000 &&
        block_zero.frame_count == 1'024,
        "Program clock starts block zero exactly at RecorderSession media zero");

    test.expect(
        clock.begin_block(
            1,
            block_one),
        "Program clock emits block one");

    const auto expected_block_one =
        250'000 +
        static_cast<std::int64_t>(
            frames_to_ticks_floor(
                1'024,
                48'000));

    test.expect(
        block_one.media_start_100ns ==
            expected_block_one,
        "Program block timestamp derives from exact canonical frame offset");

    constexpr std::uint64_t kLongBlockIndex =
        46'875;
    test.expect(
        clock.begin_block(
            kLongBlockIndex,
            block_many),
        "Program clock handles long block index");

    const auto expected_long =
        250'000 +
        static_cast<std::int64_t>(
            frames_to_ticks_floor(
                kLongBlockIndex *
                    1'024ull,
                48'000));

    test.expect(
        block_many.media_start_100ns ==
            expected_long,
        "Program clock does not accumulate block-by-block rounding drift");

    std::array<float, 512 * 2> late_samples{};
    late_samples.fill(0.25F);

    AudioSourceMixTelemetry late_telemetry{};
    const CanonicalSourceSpan late_span{
        .source = AudioSourceId::Microphone,
        .media_start_100ns =
            block_one.media_start_100ns -
            static_cast<std::int64_t>(
                frames_to_ticks_floor(
                    512,
                    48'000)),
        .frame_count = 512,
        .discontinuity = false,
        .samples = late_samples,
    };

    test.expect(
        mix_canonical_source_span(
            block_one,
            late_span,
            {},
            &late_telemetry),
        "Entirely stale source span is handled without time-shifting");

    test.expect(
        late_telemetry.stale_frames == 512 &&
        late_telemetry.mixed_frames == 0,
        "Closed-interval media is counted stale and never mixed into the future");

    bool still_silent = true;
    for (float sample : block_one.interleaved())
        still_silent =
            still_silent &&
            sample == 0.0F;

    test.expect(
        still_silent,
        "Missing source interval remains deterministic silence");

    CanonicalAudioBlock aligned;
    test.expect(
        clock.begin_block(
            2,
            aligned),
        "Program clock emits aligned fixture block");

    std::array<float, 400 * 2> source_samples{};
    for (std::size_t frame = 0;
         frame < 400;
         ++frame) {
        source_samples[frame * 2] = 0.5F;
        source_samples[frame * 2 + 1] = -0.25F;
    }

    const auto offset_ticks =
        static_cast<std::int64_t>(
            frames_to_ticks_floor(
                256,
                48'000));

    AudioSourceMixTelemetry aligned_telemetry{};
    const CanonicalSourceSpan aligned_span{
        .source = AudioSourceId::SystemAudio,
        .media_start_100ns =
            aligned.media_start_100ns +
            offset_ticks,
        .frame_count = 400,
        .discontinuity = true,
        .samples = source_samples,
    };

    test.expect(
        mix_canonical_source_span(
            aligned,
            aligned_span,
            {.gain = 0.5F},
            &aligned_telemetry),
        "Canonical source span mixes into its master-clock interval");

    test.expect(
        aligned_telemetry.mixed_frames == 400 &&
        aligned_telemetry.stale_frames == 0 &&
        aligned_telemetry.deferred_frames == 0,
        "Aligned source span preserves exact frame ownership");

    test.expect(
        aligned.samples[255 * 2] == 0.0F &&
        aligned.samples[256 * 2] == 0.25F &&
        aligned.samples[256 * 2 + 1] == -0.125F,
        "Leading gap stays silent and gain applies only to overlapping media");

    test.expect(
        (aligned.source_presence_mask &
         source_presence_bit(
             AudioSourceId::SystemAudio)) != 0 &&
        (aligned.discontinuity_mask &
         source_presence_bit(
             AudioSourceId::SystemAudio)) != 0,
        "Program block records source presence and discontinuity evidence");

    test.expect(
        std::fabs(
            aligned_telemetry.peak_absolute -
            0.25F) < 0.0001F &&
        aligned_telemetry.rms > 0.0,
        "Mixer telemetry is derived from samples already touched by the mixer");

    CanonicalAudioBlock muted;
    test.expect(
        clock.begin_block(
            3,
            muted),
        "Program clock emits mute fixture block");

    AudioSourceMixTelemetry muted_telemetry{};
    const CanonicalSourceSpan muted_span{
        .source = AudioSourceId::Microphone,
        .media_start_100ns =
            muted.media_start_100ns,
        .frame_count = 400,
        .samples = source_samples,
    };

    test.expect(
        mix_canonical_source_span(
            muted,
            muted_span,
            {
                .gain = 1.0F,
                .muted = true,
            },
            &muted_telemetry),
        "Mute is applied in the mixer without restarting source capture");

    test.expect(
        muted_telemetry.muted_frames == 400 &&
        muted_telemetry.mixed_frames == 400,
        "Muted source still advances through the same timeline interval");

    bool muted_is_silent = true;
    for (float sample : muted.interleaved())
        muted_is_silent =
            muted_is_silent &&
            sample == 0.0F;

    test.expect(
        muted_is_silent,
        "Mute preserves timeline while contributing exact silence");

    CanonicalAudioBlock clipping;
    test.expect(
        clock.begin_block(
            4,
            clipping),
        "Program clock emits clipping fixture block");

    std::array<float, 16 * 2> loud_samples{};
    loud_samples.fill(0.8F);

    const CanonicalSourceSpan loud_microphone{
        .source = AudioSourceId::Microphone,
        .media_start_100ns =
            clipping.media_start_100ns,
        .frame_count = 16,
        .samples = loud_samples,
    };
    const CanonicalSourceSpan loud_system{
        .source = AudioSourceId::SystemAudio,
        .media_start_100ns =
            clipping.media_start_100ns,
        .frame_count = 16,
        .samples = loud_samples,
    };

    test.expect(
        mix_canonical_source_span(
            clipping,
            loud_microphone,
            {}) &&
        mix_canonical_source_span(
            clipping,
            loud_system,
            {}),
        "Dual sources accumulate before final program limiting");

    const auto finalized =
        finalize_program_block(
            clipping);

    test.expect(
        finalized.clipped_samples == 32 &&
        finalized.peak_before_clip > 1.0F,
        "Final clipping policy is explicit and observable");

    bool bounded = true;
    for (std::size_t index = 0;
         index < 32;
         ++index) {
        bounded =
            bounded &&
            clipping.samples[index] == 1.0F;
    }

    test.expect(
        bounded,
        "Finalized program samples are hard-bounded to PCM-safe full scale");
}

void test_bounded_queue(TestContext &test)
{
    using arssyut::core::audio::BoundedSpscQueue;

    BoundedSpscQueue<int> queue{3};
    test.expect(queue.valid(), "Runtime SPSC queue initializes");
    test.expect(queue.capacity() == 3, "Runtime SPSC capacity is immutable");
    test.expect(queue.try_push(10), "Runtime SPSC accepts first item");
    test.expect(queue.try_push(20), "Runtime SPSC accepts second item");
    test.expect(queue.try_push(30), "Runtime SPSC accepts third item");
    test.expect(!queue.try_push(40), "Runtime SPSC rejects overflow");
    test.expect(queue.high_water() == 3, "Runtime SPSC records high-water");

    int value = 0;
    test.expect(
        queue.try_pop(value) && value == 10,
        "Runtime SPSC preserves FIFO item 1");
    test.expect(
        queue.try_pop(value) && value == 20,
        "Runtime SPSC preserves FIFO item 2");
    test.expect(queue.try_push(40), "Runtime SPSC wraps without growth");
    test.expect(
        queue.try_pop(value) && value == 30,
        "Runtime SPSC preserves FIFO item 3 after wrap");
    test.expect(
        queue.try_pop(value) && value == 40,
        "Runtime SPSC preserves wrapped item");
    test.expect(queue.empty(), "Runtime SPSC returns to empty");
}

void test_packet_pool(TestContext &test)
{
    using arssyut::core::audio::FixedAudioPacketPool;

    FixedAudioPacketPool pool{3, 64};
    test.expect(pool.valid(), "Fixed packet pool initializes");
    test.expect(pool.retained_bytes() == 192, "Packet pool memory is fixed");

    auto a = pool.try_acquire(32);
    auto b = pool.try_acquire(64);
    auto c = pool.try_acquire(8);
    test.expect(a.has_value(), "Packet pool acquires slot A");
    test.expect(b.has_value(), "Packet pool acquires slot B");
    test.expect(c.has_value(), "Packet pool acquires slot C");
    test.expect(!pool.try_acquire(1).has_value(), "Packet pool rejects exhaustion");
    test.expect(pool.high_water() == 3, "Packet pool records full high-water");

    a->bytes[0] = std::byte{0x5a};
    const auto readback = pool.readable(a->slot, 32);
    test.expect(
        readback.size() == 32 && readback[0] == std::byte{0x5a},
        "Packet pool retains payload after producer release boundary");

    test.expect(pool.release(b->slot), "Packet pool releases slot");
    auto reused = pool.try_acquire(16);
    test.expect(reused.has_value(), "Packet pool reuses released storage");
    test.expect(
        pool.retained_bytes() == 192,
        "Packet pool capacity never grows after reuse");

    test.expect(pool.release(a->slot), "Packet pool releases slot A");
    test.expect(pool.release(c->slot), "Packet pool releases slot C");
    test.expect(pool.release(reused->slot), "Packet pool releases reused slot");
    test.expect(pool.in_use() == 0, "Packet pool returns to zero leases");
}

void test_packet_contract(TestContext &test)
{
    using namespace arssyut::core::audio;

    AudioSourcePacket packet{
        .source = AudioSourceId::SystemAudio,
        .native_format = {
            .sample_rate = 48'000,
            .sample_type = AudioSampleType::Float32,
            .channels = 2,
            .container_bits_per_sample = 32,
            .valid_bits_per_sample = 32,
            .channel_mask = kStereoChannelMask,
            .block_align = 8,
        },
        .frame_count = 128,
        .timing = {
            .quality = AudioTimestampQuality::DeviceQpcTrusted,
            .packet_start_qpc_100ns = 100'000,
            .host_observed_qpc_100ns = 100'100,
            .device_frame_position = 1'024,
        },
        .flags = AudioPacketFlag::None,
        .pool_slot = 2,
        .payload_bytes = 1'024,
    };
    test.expect(packet.metadata_valid(), "Audio packet metadata is self-describing");
    test.expect(
        packet.eligible_for_drift(),
        "Trusted device-QPC packet is canonically eligible for drift evidence");

    packet.flags |= AudioPacketFlag::TimestampError;
    test.expect(
        has_flag(packet.flags, AudioPacketFlag::TimestampError),
        "Timestamp error flag survives packet contract");
    test.expect(
        !packet.eligible_for_drift(),
        "Timestamp-error packet cannot enter drift estimation");

    AudioSourcePacket silence = packet;
    silence.flags = AudioPacketFlag::Silent;
    silence.pool_slot = kInvalidAudioPoolSlot;
    silence.payload_bytes = 0;
    test.expect(
        silence.metadata_valid(),
        "Silent packet may preserve timeline without payload allocation");

    AudioSourcePacket retained_silence = packet;
    retained_silence.flags = AudioPacketFlag::Silent;
    retained_silence.payload_bytes =
        retained_silence.native_format.bytes_for_frames(
            retained_silence.frame_count);
    test.expect(
        retained_silence.metadata_valid(),
        "Retained silent payload requires exact frame byte count");

    retained_silence.payload_bytes = 1;
    test.expect(
        !retained_silence.metadata_valid(),
        "Silent retained payload rejects short or contradictory byte count");

    AudioSourcePacket reconstructed = packet;
    reconstructed.flags = AudioPacketFlag::None;
    reconstructed.timing.quality =
        AudioTimestampQuality::ContinuityReconstructed;
    test.expect(
        !reconstructed.eligible_for_drift(),
        "Continuity-reconstructed timing cannot become trusted drift evidence");

    CanonicalAudioBlock block;
    block.clear(1'024);
    test.expect(block.valid(), "Canonical block enforces bounded frame capacity");
    test.expect(
        block.interleaved().size() == 2'048,
        "Canonical block always exposes stereo interleaved storage");
}

void test_mix_primitives(TestContext &test)
{
    using namespace arssyut::core::audio;

    const std::array<float, 3> mono{0.25f, -0.5f, 1.0f};
    std::array<float, 6> stereo{};
    test.expect(
        copy_or_map_to_stereo(mono, 1, stereo),
        "Mono maps to stereo");
    test.expect(
        stereo[0] == stereo[1] &&
        stereo[2] == stereo[3] &&
        stereo[4] == stereo[5],
        "Mono mapping is centered, not one-sided");

    const std::array<float, 4> source{
        0.25f, -0.25f, 0.5f, -0.5f};
    std::array<float, 4> destination{};
    test.expect(
        copy_or_map_to_stereo(source, 2, destination),
        "Stereo mapping preserves L/R");
    test.expect(
        destination == source,
        "Stereo channel order remains unchanged");

    const std::array<float, 2> loud{0.8f, 0.8f};
    std::array<float, 4> mix{0.5f, 0.5f, 0.5f, 0.5f};
    MixStats stats{};
    test.expect(
        accumulate_mono_to_stereo(loud, mix, 1.0f, &stats),
        "Mono accumulation succeeds");
    test.expect(
        stats.over_range_samples == 4,
        "Mix primitive reports over-range instead of hiding clipping");
}

void test_drift_measurement(TestContext &test)
{
    using namespace arssyut::core::audio;

    const DriftAnchor zero{
        .device_frame_position = 0,
        .qpc_100ns = 0,
        .trusted = true,
    };
    const DriftAnchor plus_100_ppm{
        .device_frame_position = 480'048,
        .qpc_100ns = 100'000'000,
        .trusted = true,
    };
    const auto plus =
        estimate_rate_error(zero, plus_100_ppm, 48'000);
    test.expect(plus.valid, "Positive drift fixture produces estimate");
    test.expect(
        std::fabs(plus.rate_error_ppm - 100.0) < 0.001,
        "Positive drift estimate resolves +100 ppm");

    const DriftAnchor minus_100_ppm{
        .device_frame_position = 479'952,
        .qpc_100ns = 100'000'000,
        .trusted = true,
    };
    const auto minus =
        estimate_rate_error(zero, minus_100_ppm, 48'000);
    test.expect(minus.valid, "Negative drift fixture produces estimate");
    test.expect(
        std::fabs(minus.rate_error_ppm + 100.0) < 0.001,
        "Negative drift estimate resolves -100 ppm");

    DriftEstimator estimator{48'000, 48'000};
    test.expect(
        !estimator.observe(zero).valid,
        "Drift estimator first trusted observation anchors only");
    const auto measured = estimator.observe(plus_100_ppm);
    test.expect(
        measured.valid &&
        std::fabs(measured.rate_error_ppm - 100.0) < 0.001,
        "Fixed-state drift estimator measures trusted interval");

    const auto rejected = estimator.observe({
        .device_frame_position = 960'000,
        .qpc_100ns = 200'000'000,
        .trusted = false,
    });
    test.expect(
        !rejected.valid && estimator.rejected_observations() == 1,
        "Untrusted timing never enters drift estimate");
}

void test_resampler_contract(TestContext &test)
{
    using namespace arssyut::core::audio;

    AudioResamplerConfig config{
        .input_sample_rate = 44'100,
        .input_channels = 2,
        .input_channel_mask = kStereoChannelMask,
    };
    test.expect(config.valid(), "Resampler contract accepts 44.1 kHz stereo source");
    test.expect(
        AudioResamplerConfig::kOutputSampleRate == 48'000 &&
        AudioResamplerConfig::kOutputChannels == 2,
        "Resampler output is structurally fixed to canonical 48 kHz stereo");

    AudioResamplerConfig surround{
        .input_sample_rate = 48'000,
        .input_channels = 6,
        .input_channel_mask = kSurround51ChannelMask,
    };
    test.expect(
        surround.valid(),
        "Resampler contract carries explicit 5.1 speaker layout");

    surround.input_channel_mask = 0;
    test.expect(
        !surround.valid(),
        "Multichannel resampling cannot guess an absent speaker layout");

    surround.input_channel_mask = kStereoChannelMask;
    test.expect(
        !surround.valid(),
        "Multichannel resampling rejects a layout/count mismatch");

    AudioResampleResult pending{};
    test.expect(
        pending.status == AudioResampleStatus::NotConfigured,
        "P7A1 defines seam without selecting implementation");

    ContractProbeResampler probe;
    test.expect(
        probe.configure(config) == AudioResampleStatus::Ok,
        "Stateful resampler seam configures through canonical contract");

    const std::array<float, 8> input{};
    std::array<float, 8> output{};
    const auto processed = probe.process(input, 4, output, 87.24);
    test.expect(
        processed.ok() &&
        processed.input_frames_consumed == 4 &&
        processed.output_frames_produced == 4,
        "Resampler result exposes consumed and produced frame counts");
    test.expect(
        processed.algorithmic_delay_100ns > 0 &&
        processed.algorithmic_delay_100ns == probe.current_delay_100ns(),
        "Resampler delay is observable in RecorderSession 100 ns time");

    const auto rate_state = probe.current_rate_state();
    test.expect(
        rate_state.observable(),
        "Resampler exposes observable effective ratio and exact phase state");
    test.expect(
        std::fabs(rate_state.requested_rate_adjustment_ppm - 87.24) < 0.0001 &&
        std::fabs(rate_state.applied_rate_adjustment_ppm - 87.0) < 0.0001,
        "Requested ppm remains distinct from backend-quantized applied ppm");
    const double expected_ratio =
        (48'000.0 / 44'100.0) * (1.0 + 87.0 / 1'000'000.0);
    test.expect(
        std::fabs(
            rate_state.effective_output_per_input_ratio -
            expected_ratio) < 1e-12,
        "Effective output/input ratio is directly observable");
    test.expect(
        rate_state.phase_remainder_numerator == 3 &&
        rate_state.phase_remainder_denominator == 8,
        "Fractional phase remainder is explicit and exact");
    test.expect(
        probe.maximum_drain_frames() == 3,
        "Drain contract exposes a fixed tail-frame upper bound");

    std::array<float, 2> one_frame{};
    std::uint32_t drained_frames = 0;
    bool complete = false;
    for (std::uint32_t call = 0;
         call < probe.maximum_drain_frames() && !complete;
         ++call) {
        const auto drained = probe.drain(one_frame);
        test.expect(drained.ok(), "Bounded drain call succeeds");
        drained_frames += drained.output_frames_produced;
        complete = drained.complete;
    }

    test.expect(
        complete &&
        drained_frames <= probe.maximum_drain_frames() &&
        probe.current_delay_100ns() == 0,
        "Drain completes within its published bound and clears delay");

    const auto drained_again = probe.drain(one_frame);
    test.expect(
        drained_again.ok() &&
        drained_again.complete &&
        drained_again.output_frames_produced == 0,
        "Completed drain is idempotent");

    probe.process(input, 4, output, 0.0);
    probe.reset();
    test.expect(
        probe.current_delay_100ns() == 0,
        "Reset discards retained state without tail emission");
    test.expect(
        !probe.current_rate_state().observable(),
        "Reset clears retained resampler ratio and phase telemetry");
}

} // namespace

int main()
{
    TestContext test;

    test_format_profile(test);
    test_rational_time(test);
    test_program_clock_and_alignment(test);
    test_bounded_queue(test);
    test_packet_pool(test);
    test_packet_contract(test);
    test_mix_primitives(test);
    test_drift_measurement(test);
    test_resampler_contract(test);

    if (test.failures != 0) {
        std::cerr << test.failures << " of " << test.checks
                  << " checks failed\n";
        return 1;
    }

    std::cout << "PASS: " << test.checks
              << " P7A/P7A5 deterministic audio checks\n";
    return 0;
}
