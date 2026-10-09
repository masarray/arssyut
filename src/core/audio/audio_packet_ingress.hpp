#pragma once

// P7A5 packet ingress for the accepted canonical program assembler.
// Exactly one RecorderSession owner calls this: WASAPI lease's metadata and
// bytes are borrowed only for process_packet(), never retained. One selected
// IAudioResampler owns its native state. No new queue, clock or worker.
#include "core/audio/audio_sample_normalizer.hpp"
#include "core/audio/audio_source_timeline_mapper.hpp"
#include "core/audio/audio_drift_controller.hpp"
#include "core/audio/audio_resampler.hpp"
#include "core/audio/audio_canonical_program_assembler.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace arssyut::core::audio {

enum class PacketIngressStatus : std::uint8_t {
    Applied, BeforeMediaZero, NoOutput, Invalid, TooLarge,
    UnsupportedFormat, TimelineRejected, ResamplerFailure,
    StageFull, StageGap, StageInactive
};

struct PacketIngressResult {
    PacketIngressStatus status = PacketIngressStatus::Invalid;
    std::uint32_t native_frames_consumed = 0;
    std::uint32_t canonical_frames_emitted = 0;
    std::int64_t first_canonical_frame = 0;
    bool discontinuity = false;
};

class CanonicalPacketIngress final {
public:
    static constexpr std::uint32_t kMaxNativeFrames = 1'024;
    static constexpr std::uint16_t kMaxNativeChannels = 8;
    static constexpr std::uint32_t kMaxCanonicalFrames = 8'192;

    [[nodiscard]] bool configure(
        AudioSourceId source, AudioFormat format,
        std::int64_t recorder_qpc_zero_100ns,
        IAudioResampler &resampler) noexcept
    {
        ready_ = false;
        source_ = source;
        input_format_ = format;
        resampler_ = &resampler;
        mapper_.reset(recorder_qpc_zero_100ns);
        drift_ = DriftEstimator(format.sample_rate);
        drift_controller_.reset();
        next_frame_ = 0;
        have_next_ = false;
        marked_discontinuity_ = false;
        if (recorder_qpc_zero_100ns <= 0 ||
            !format.valid() || format.channels > kMaxNativeChannels ||
            format.sample_rate < 8'000 ||
            format.sample_rate > 192'000) {
            return false;
        }
        const AudioResamplerConfig cfg{
            .input_sample_rate = format.sample_rate,
            .input_channels = format.channels,
            .input_channel_mask = format.channel_mask
        };
        ready_ = resampler.configure(cfg) == AudioResampleStatus::Ok;
        return ready_;
    }

    [[nodiscard]] PacketIngressResult process_packet(
        const AudioSourcePacket &packet,
        std::span<const std::byte> payload,
        CanonicalProgramAssembler &assembler) noexcept
    {
        PacketIngressResult result;
        if (!ready_ || packet.source != source_ ||
            !same_format(packet.native_format, input_format_) ||
            !packet.metadata_valid() ||
            (packet.payload_bytes != payload.size())) {
            return result;
        }
        if (packet.frame_count > kMaxNativeFrames) {
            result.status = PacketIngressStatus::TooLarge;
            return result; // Must split large leases upstream; never truncate.
        }
        const auto mapped = mapper_.map(packet);
        if (mapped.status == AudioTimelineMapStatus::Invalid) {
            result.status = PacketIngressStatus::TimelineRejected;
            return result;
        }
        if (mapped.status == AudioTimelineMapStatus::FullyBeforeMediaZero) {
            result.status = PacketIngressStatus::BeforeMediaZero;
            return result;
        }

        AudioNormalizationStats norms{};
        const auto native_count =
            static_cast<std::size_t>(packet.frame_count) *
                input_format_.channels;
        const auto normalized = normalize_audio_packet_to_float32(
            packet, payload,
            std::span<float>(normalized_.data(), native_count), &norms);
        if (normalized != AudioNormalizeStatus::Ok) {
            result.status = normalized == AudioNormalizeStatus::UnsupportedFormat
                ? PacketIngressStatus::UnsupportedFormat
                : PacketIngressStatus::Invalid;
            return result;
        }

        const auto trimmed = mapped.source_frames_before_zero;
        if (trimmed >= packet.frame_count) {
            result.status = PacketIngressStatus::BeforeMediaZero;
            return result;
        }
        const std::uint32_t remaining = packet.frame_count - trimmed;
        const auto first_sample =
            static_cast<std::size_t>(trimmed) * input_format_.channels;
        std::span<const float> native(
            normalized_.data() + first_sample,
            static_cast<std::size_t>(remaining) * input_format_.channels);

        bool boundary =
            mapped.discontinuity ||
            has_flag(packet.flags, AudioPacketFlag::TimestampError);
        // A real QPC jump is not permission to concatenate unrelated
        // device samples on the old SRC cursor. Keep the mapper authoritative
        // for the next anchor instead of hiding source stalls as continuity.
        constexpr std::int64_t kGapToleranceFrames = 960; // 20 ms
        if (!boundary && have_next_) {
            const auto distance =
                mapped.canonical_start_frame - next_frame_;
            if (distance > kGapToleranceFrames ||
                distance < -kGapToleranceFrames)
                boundary = true;
        }
        if (boundary) {
            resampler_->reset();
            drift_.reset();
            drift_controller_.reset_for_discontinuity();
            have_next_ = false;
            marked_discontinuity_ = true;
        }

        double ppm = drift_controller_.snapshot().requested_resampler_ppm;
        if (mapped.drift_eligible) {
            const auto estimate = drift_.observe(mapped.drift_anchor);
            if (estimate.valid)
                ppm = drift_controller_.update(
                    estimate, estimate.qpc_delta_100ns);
        }

        const std::uint64_t before_delay =
            resampler_->current_delay_100ns();
        const auto processed = resampler_->process(
            native, remaining, canonical_, ppm);
        if (!processed.ok() ||
            processed.input_frames_consumed != remaining ||
            processed.output_frames_produced > kMaxCanonicalFrames) {
            ready_ = false;
            result.status = PacketIngressStatus::ResamplerFailure;
            return result;  // Fail closed; never retry consumed SRC state.
        }
        result.native_frames_consumed = remaining;
        if (!have_next_) {
            const auto delay_frames = static_cast<std::int64_t>(
                ticks_to_frames_floor(before_delay, 48'000));
            // QPC mapper determines the initial absolute position. SRC's
            // queued filter delay is subtracted once at the reanchor point.
            next_frame_ = mapped.canonical_start_frame - delay_frames;
            have_next_ = true;
        }
        if (processed.output_frames_produced == 0) {
            result.status = PacketIngressStatus::NoOutput;
            return result;
        }
        const auto produced = processed.output_frames_produced;
        const auto negative_trim = next_frame_ < 0
            ? static_cast<std::uint32_t>(std::min<std::int64_t>(
                  -next_frame_, produced))
            : 0U;
        const auto emitted = produced - negative_trim;
        if (emitted == 0) {
            next_frame_ += produced;
            result.status = PacketIngressStatus::BeforeMediaZero;
            return result;
        }
        const std::int64_t first = next_frame_ + negative_trim;
        result.first_canonical_frame = first;
        result.discontinuity = marked_discontinuity_;
        const auto offer = assembler.offer(
            source_, first,
            std::span<const float>(
                canonical_.data() + static_cast<std::size_t>(negative_trim)*2,
                static_cast<std::size_t>(emitted)*2),
            emitted, marked_discontinuity_);
        switch (offer) {
        case CanonicalProgramOfferStatus::Applied:
            next_frame_ += produced;
            marked_discontinuity_ = false;
            result.status = PacketIngressStatus::Applied;
            result.canonical_frames_emitted = emitted;
            break;
        case CanonicalProgramOfferStatus::Overflow:
            result.status = PacketIngressStatus::StageFull;
            break;
        case CanonicalProgramOfferStatus::NonContiguous:
            result.status = PacketIngressStatus::StageGap;
            break;
        case CanonicalProgramOfferStatus::Inactive:
            result.status = PacketIngressStatus::StageInactive;
            break;
        case CanonicalProgramOfferStatus::Invalid:
        default:
            result.status = PacketIngressStatus::Invalid;
            break;
        }
        if (result.status != PacketIngressStatus::Applied) {
            // SRC state was already consumed; the caller must transition
            // this source to failed/restart, never retry this packet.
            ready_ = false;
        }
        return result;
    }

    // One bounded stop-drain step on the same owner. Caller must run it before
    // closing the canonical output interval, never after MF Finalize.
    [[nodiscard]] PacketIngressResult drain_one(
        CanonicalProgramAssembler &assembler) noexcept
    {
        PacketIngressResult result;
        if (!ready_ || !have_next_)
            return result;
        const auto drained = resampler_->drain(canonical_);
        if (!drained.ok() ||
            drained.output_frames_produced > kMaxCanonicalFrames) {
            ready_ = false;
            result.status = PacketIngressStatus::ResamplerFailure;
            return result;
        }
        if (drained.output_frames_produced == 0) {
            result.status = PacketIngressStatus::NoOutput;
            return result;
        }
        if (next_frame_ < 0) {
            ready_ = false;
            return result;
        }
        const auto count = drained.output_frames_produced;
        result.first_canonical_frame = next_frame_;
        result.discontinuity = marked_discontinuity_;
        const auto offer = assembler.offer(
            source_, next_frame_,
            std::span<const float>(canonical_.data(), count*2U),
            count, marked_discontinuity_);
        if (offer != CanonicalProgramOfferStatus::Applied) {
            ready_ = false;
            result.status = offer == CanonicalProgramOfferStatus::Overflow
                ? PacketIngressStatus::StageFull
                : PacketIngressStatus::StageGap;
            return result;
        }
        next_frame_ += count;
        marked_discontinuity_ = false;
        result.status = PacketIngressStatus::Applied;
        result.canonical_frames_emitted = count;
        return result;
    }

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] AudioDriftControllerSnapshot drift() const noexcept
    {
        return drift_controller_.snapshot();
    }

private:
    [[nodiscard]] static bool same_format(
        const AudioFormat &a, const AudioFormat &b) noexcept
    {
        return a.sample_rate == b.sample_rate &&
            a.channels == b.channels &&
            a.sample_type == b.sample_type &&
            a.container_bits_per_sample == b.container_bits_per_sample &&
            a.valid_bits_per_sample == b.valid_bits_per_sample &&
            a.channel_mask == b.channel_mask &&
            a.block_align == b.block_align;
    }
    std::array<float,
        static_cast<std::size_t>(kMaxNativeFrames)*kMaxNativeChannels>
        normalized_{};
    std::array<float,
        static_cast<std::size_t>(kMaxCanonicalFrames)*2> canonical_{};
    AudioSourceTimelineMapper mapper_{};
    DriftEstimator drift_{48'000};
    AudioDriftController drift_controller_{};
    IAudioResampler *resampler_ = nullptr;
    AudioFormat input_format_{};
    AudioSourceId source_ = AudioSourceId::Microphone;
    std::int64_t next_frame_ = 0;
    bool have_next_ = false;
    bool marked_discontinuity_ = false;
    bool ready_ = false;
};

} // namespace arssyut::core::audio
