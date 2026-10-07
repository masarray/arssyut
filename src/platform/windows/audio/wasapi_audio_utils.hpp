#pragma once

#ifdef _WIN32

#include "core/audio/audio_buffer.hpp"
#include "core/audio/audio_format.hpp"
#include "core/audio/audio_packet.hpp"
#include "core/audio/audio_time.hpp"

#include <Windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmreg.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace arssyut::windows {

[[nodiscard]] inline std::optional<core::audio::AudioFormat>
audio_format_from_wave_format(
    const WAVEFORMATEX *wave) noexcept
{
    using namespace core::audio;

    if (wave == nullptr ||
        wave->nSamplesPerSec == 0 ||
        wave->nChannels == 0 ||
        wave->nBlockAlign == 0)
        return std::nullopt;

    AudioSampleType sample_type = AudioSampleType::Unknown;
    std::uint16_t valid_bits = wave->wBitsPerSample;
    std::uint32_t channel_mask = 0;

    if (wave->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (wave->cbSize <
            sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
            return std::nullopt;

        const auto *ext =
            reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(wave);
        valid_bits = ext->Samples.wValidBitsPerSample;
        channel_mask = ext->dwChannelMask;

        if (IsEqualGUID(
                ext->SubFormat,
                KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            if (wave->wBitsPerSample == 32 && valid_bits == 32)
                sample_type = AudioSampleType::Float32;
        } else if (IsEqualGUID(
                       ext->SubFormat,
                       KSDATAFORMAT_SUBTYPE_PCM)) {
            if (wave->wBitsPerSample == 16 && valid_bits == 16)
                sample_type = AudioSampleType::Pcm16;
            else if (wave->wBitsPerSample == 32 && valid_bits == 24)
                sample_type = AudioSampleType::Pcm24In32;
            else if (wave->wBitsPerSample == 32 && valid_bits == 32)
                sample_type = AudioSampleType::Pcm32;
        }
    } else if (
        wave->wFormatTag == WAVE_FORMAT_IEEE_FLOAT &&
        wave->wBitsPerSample == 32) {
        sample_type = AudioSampleType::Float32;
        valid_bits = 32;
    } else if (wave->wFormatTag == WAVE_FORMAT_PCM) {
        if (wave->wBitsPerSample == 16) {
            sample_type = AudioSampleType::Pcm16;
            valid_bits = 16;
        } else if (wave->wBitsPerSample == 32) {
            sample_type = AudioSampleType::Pcm32;
            valid_bits = 32;
        }
    }

    AudioFormat format{
        .sample_rate = wave->nSamplesPerSec,
        .sample_type = sample_type,
        .channels = wave->nChannels,
        .container_bits_per_sample = wave->wBitsPerSample,
        .valid_bits_per_sample = valid_bits,
        .channel_mask = channel_mask,
        .block_align = wave->nBlockAlign,
    };

    return format.valid()
        ? std::optional<AudioFormat>{format}
        : std::nullopt;
}

[[nodiscard]] inline core::audio::AudioPacketFlag
audio_packet_flags_from_wasapi(
    DWORD flags) noexcept
{
    using core::audio::AudioPacketFlag;

    AudioPacketFlag result = AudioPacketFlag::None;
    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0)
        result |= AudioPacketFlag::Silent;
    if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0)
        result |= AudioPacketFlag::Discontinuity;
    if ((flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) != 0)
        result |= AudioPacketFlag::TimestampError;
    return result;
}

class WasapiTimestampClassifier final {
public:
    explicit WasapiTimestampClassifier(
        std::uint32_t sample_rate = 0) noexcept
        : sample_rate_(sample_rate)
    {
    }

    void reset(
        std::uint32_t sample_rate) noexcept
    {
        sample_rate_ = sample_rate;
        have_expected_ = false;
        have_candidate_ = false;
        have_trusted_anchor_ = false;
        degraded_ = false;
        pending_reset_ = false;
        consecutive_good_after_degraded_ = 0;
        consecutive_bad_timestamps_ = 0;
        expected_device_frame_ = 0;
        expected_qpc_100ns_ = 0;
        last_candidate_device_frame_ = 0;
        last_candidate_qpc_100ns_ = 0;
        pending_reset_device_frame_ = 0;
        pending_reset_qpc_100ns_ = 0;
    }

    [[nodiscard]] core::audio::AudioTimestampEvidence observe(
        std::uint32_t frame_count,
        std::uint64_t reported_device_frame,
        std::uint64_t reported_qpc_100ns,
        std::int64_t host_observed_qpc_100ns,
        core::audio::AudioPacketFlag flags) noexcept
    {
        using namespace core::audio;

        AudioTimestampEvidence evidence{};
        evidence.host_observed_qpc_100ns =
            host_observed_qpc_100ns;

        const bool timestamp_error =
            has_flag(flags, AudioPacketFlag::TimestampError);
        const bool discontinuity =
            has_flag(flags, AudioPacketFlag::Discontinuity);

        const auto fallback_start =
            fallback_packet_start(
                frame_count,
                host_observed_qpc_100ns);

        if (sample_rate_ == 0 || frame_count == 0) {
            evidence.quality =
                AudioTimestampQuality::Discontinuous;
            evidence.packet_start_qpc_100ns =
                fallback_start;
            evidence.device_frame_position =
                reported_device_frame;
            degraded_ = true;
            have_trusted_anchor_ = false;
            consecutive_good_after_degraded_ = 0;
            ++consecutive_bad_timestamps_;
            return evidence;
        }

        if (discontinuity) {
            const bool qpc_matches_host =
                !timestamp_error &&
                reported_qpc_100ns != 0 &&
                candidate_matches_host_window(
                    frame_count,
                    reported_qpc_100ns,
                    host_observed_qpc_100ns);

            evidence.quality =
                AudioTimestampQuality::Discontinuous;
            evidence.packet_start_qpc_100ns =
                qpc_matches_host
                    ? static_cast<std::int64_t>(reported_qpc_100ns)
                    : fallback_start;
            evidence.device_frame_position =
                reported_device_frame;

            degraded_ = true;
            have_trusted_anchor_ = false;
            consecutive_good_after_degraded_ = 0;
            consecutive_bad_timestamps_ = 0;
            have_candidate_ = false;
            pending_reset_ = false;

            // A trustworthy QPC on the discontinuity packet may seed the new
            // epoch's plausibility baseline, but the packet itself never counts
            // as a good relock observation.
            if (qpc_matches_host) {
                remember_candidate(
                    reported_device_frame,
                    reported_qpc_100ns);
            }

            remember_expected(evidence, frame_count);
            return evidence;
        }

        // A single backward device-frame sample may be driver corruption, not
        // a real clock epoch reset. Keep the last trusted/candidate epoch
        // intact and treat the backward observation as tentative. Only a
        // follow-up packet that progresses plausibly from the tentative frame
        // and QPC commits a new epoch.
        if (pending_reset_) {
            const bool confirms_reset =
                !timestamp_error &&
                reported_qpc_100ns != 0 &&
                pair_progression_is_plausible(
                    pending_reset_device_frame_,
                    pending_reset_qpc_100ns_,
                    reported_device_frame,
                    reported_qpc_100ns,
                    frame_count,
                    host_observed_qpc_100ns);

            if (confirms_reset) {
                evidence.quality =
                    AudioTimestampQuality::Discontinuous;
                evidence.packet_start_qpc_100ns =
                    static_cast<std::int64_t>(
                        reported_qpc_100ns);
                evidence.device_frame_position =
                    reported_device_frame;

                degraded_ = true;
                have_trusted_anchor_ = false;
                consecutive_good_after_degraded_ = 0;
                consecutive_bad_timestamps_ = 0;
                have_candidate_ = false;
                pending_reset_ = false;

                remember_candidate(
                    reported_device_frame,
                    reported_qpc_100ns);
                remember_expected(
                    evidence,
                    frame_count);
                return evidence;
            }

            // If the old epoch resumes normally, the prior backward sample was
            // just corrupt. Discard the tentative reset and validate this
            // packet against the unchanged old candidate below.
            if (reported_device_frame >
                    last_candidate_device_frame_ &&
                reported_qpc_100ns >
                    last_candidate_qpc_100ns_) {
                pending_reset_ = false;
            }
        }

        const bool backward_candidate =
            !timestamp_error &&
            reported_qpc_100ns != 0 &&
            have_candidate_ &&
            reported_device_frame <
                last_candidate_device_frame_;

        if (backward_candidate &&
            candidate_matches_host_window(
                frame_count,
                reported_qpc_100ns,
                host_observed_qpc_100ns)) {
            pending_reset_ = true;
            pending_reset_device_frame_ =
                reported_device_frame;
            pending_reset_qpc_100ns_ =
                reported_qpc_100ns;

            ++consecutive_bad_timestamps_;

            if (have_trusted_anchor_ &&
                have_expected_) {
                evidence.quality =
                    AudioTimestampQuality::ContinuityReconstructed;
                evidence.packet_start_qpc_100ns =
                    expected_qpc_100ns_;
                evidence.device_frame_position =
                    expected_device_frame_;
            } else {
                evidence.quality =
                    AudioTimestampQuality::HostQpcFallback;
                evidence.packet_start_qpc_100ns =
                    fallback_start;
                evidence.device_frame_position =
                    have_expected_
                        ? expected_device_frame_
                        : reported_device_frame;
            }

            remember_expected(
                evidence,
                frame_count);
            return evidence;
        }

        const bool plausible =
            !timestamp_error &&
            reported_qpc_100ns != 0 &&
            candidate_is_plausible(
                frame_count,
                reported_device_frame,
                reported_qpc_100ns,
                host_observed_qpc_100ns);

        if (plausible) {
            consecutive_bad_timestamps_ = 0;
            remember_candidate(
                reported_device_frame,
                reported_qpc_100ns);

            if (!degraded_) {
                have_trusted_anchor_ = true;
                evidence.quality =
                    AudioTimestampQuality::DeviceQpcTrusted;
                evidence.packet_start_qpc_100ns =
                    static_cast<std::int64_t>(
                        reported_qpc_100ns);
                evidence.device_frame_position =
                    reported_device_frame;
                remember_expected(evidence, frame_count);
                return evidence;
            }

            ++consecutive_good_after_degraded_;
            if (consecutive_good_after_degraded_ >=
                kGoodPacketsToRelock) {
                degraded_ = false;
                have_trusted_anchor_ = true;
                consecutive_good_after_degraded_ = 0;
                evidence.quality =
                    AudioTimestampQuality::DeviceQpcTrusted;
                evidence.packet_start_qpc_100ns =
                    static_cast<std::int64_t>(
                        reported_qpc_100ns);
                evidence.device_frame_position =
                    reported_device_frame;
                remember_expected(evidence, frame_count);
                return evidence;
            }
        } else {
            degraded_ = true;
            consecutive_good_after_degraded_ = 0;
            ++consecutive_bad_timestamps_;
            if (consecutive_bad_timestamps_ > 1)
                have_trusted_anchor_ = false;
        }

        // Only one bad timing observation may extend the last trusted anchor.
        // Repeated bad timing must move to host-QPC fallback instead of
        // pretending reconstructed device timing remains authoritative.
        if (have_trusted_anchor_ &&
            have_expected_ &&
            consecutive_bad_timestamps_ <= 1) {
            evidence.quality =
                AudioTimestampQuality::ContinuityReconstructed;
            evidence.packet_start_qpc_100ns =
                expected_qpc_100ns_;
            evidence.device_frame_position =
                expected_device_frame_;
        } else {
            evidence.quality =
                AudioTimestampQuality::HostQpcFallback;
            evidence.packet_start_qpc_100ns =
                fallback_start;
            evidence.device_frame_position =
                have_expected_
                    ? expected_device_frame_
                    : reported_device_frame;
        }

        remember_expected(evidence, frame_count);
        return evidence;
    }

private:
    static constexpr std::uint32_t
        kGoodPacketsToRelock = 2;

    [[nodiscard]] std::int64_t fallback_packet_start(
        std::uint32_t frame_count,
        std::int64_t host_observed_qpc_100ns) const noexcept
    {
        const auto duration =
            core::audio::frames_to_ticks_floor(
                frame_count,
                sample_rate_);
        const auto duration_i64 =
            static_cast<std::int64_t>(
                std::min<std::uint64_t>(
                    duration,
                    static_cast<std::uint64_t>(
                        INT64_MAX)));

        return std::max<std::int64_t>(
            0,
            host_observed_qpc_100ns - duration_i64);
    }

    [[nodiscard]] bool candidate_matches_host_window(
        std::uint32_t frame_count,
        std::uint64_t qpc_100ns,
        std::int64_t host_observed_qpc_100ns) const noexcept
    {
        // WASAPI device QPC and the worker's host observation use the same
        // canonical QPC/100 ns domain. A fresh anchor therefore cannot be
        // arbitrarily far in the future or stale relative to the GetBuffer
        // observation. Keep the window deliberately generous enough for a
        // delayed event-service wake while rejecting gross driver timestamp
        // corruption before it can become a trusted anchor.
        if (host_observed_qpc_100ns <= 0 ||
            qpc_100ns >
                static_cast<std::uint64_t>(INT64_MAX))
            return false;

        const auto reported =
            static_cast<std::int64_t>(qpc_100ns);
        constexpr std::int64_t kFutureSlack100ns =
            500'000;   // 50 ms
        constexpr std::int64_t kStaleSlack100ns =
            5'000'000; // 500 ms beyond packet duration

        if (reported > host_observed_qpc_100ns) {
            return reported - host_observed_qpc_100ns <=
                kFutureSlack100ns;
        }

        const auto duration =
            core::audio::frames_to_ticks_floor(
                frame_count,
                sample_rate_);
        const auto duration_i64 =
            static_cast<std::int64_t>(
                std::min<std::uint64_t>(
                    duration,
                    static_cast<std::uint64_t>(
                        INT64_MAX)));

        const auto max_age =
            duration_i64 >
                    INT64_MAX - kStaleSlack100ns
                ? INT64_MAX
                : duration_i64 + kStaleSlack100ns;

        return host_observed_qpc_100ns - reported <=
            max_age;
    }

    [[nodiscard]] bool pair_progression_is_plausible(
        std::uint64_t first_device_frame,
        std::uint64_t first_qpc_100ns,
        std::uint64_t second_device_frame,
        std::uint64_t second_qpc_100ns,
        std::uint32_t frame_count,
        std::int64_t host_observed_qpc_100ns) const noexcept
    {
        if (!candidate_matches_host_window(
                frame_count,
                second_qpc_100ns,
                host_observed_qpc_100ns) ||
            second_device_frame <= first_device_frame ||
            second_qpc_100ns <= first_qpc_100ns)
            return false;

        const auto frame_delta =
            second_device_frame - first_device_frame;
        const auto qpc_delta =
            second_qpc_100ns - first_qpc_100ns;
        const auto expected =
            core::audio::frames_to_ticks_floor(
                frame_delta,
                sample_rate_);
        if (expected == 0)
            return false;

        const auto error =
            qpc_delta > expected
                ? qpc_delta - expected
                : expected - qpc_delta;
        const auto tolerance =
            std::max<std::uint64_t>(
                20'000ULL,
                expected / 20ULL);
        return error <= tolerance;
    }

    [[nodiscard]] bool candidate_is_plausible(
        std::uint32_t frame_count,
        std::uint64_t device_frame,
        std::uint64_t qpc_100ns,
        std::int64_t host_observed_qpc_100ns) const noexcept
    {
        if (!candidate_matches_host_window(
                frame_count,
                qpc_100ns,
                host_observed_qpc_100ns))
            return false;

        if (!have_candidate_)
            return true;

        if (device_frame <= last_candidate_device_frame_ ||
            qpc_100ns <= last_candidate_qpc_100ns_)
            return false;

        const std::uint64_t frame_delta =
            device_frame - last_candidate_device_frame_;
        const std::uint64_t qpc_delta =
            qpc_100ns - last_candidate_qpc_100ns_;
        const std::uint64_t expected =
            core::audio::frames_to_ticks_floor(
                frame_delta,
                sample_rate_);

        if (expected == 0)
            return false;

        const std::uint64_t error =
            qpc_delta > expected
                ? qpc_delta - expected
                : expected - qpc_delta;

        const std::uint64_t tolerance =
            std::max<std::uint64_t>(
                20'000ULL,
                expected / 20ULL);

        return error <= tolerance;
    }

    void remember_candidate(
        std::uint64_t device_frame,
        std::uint64_t qpc_100ns) noexcept
    {
        have_candidate_ = true;
        last_candidate_device_frame_ =
            device_frame;
        last_candidate_qpc_100ns_ =
            qpc_100ns;
    }

    void remember_expected(
        const core::audio::AudioTimestampEvidence &evidence,
        std::uint32_t frame_count) noexcept
    {
        const auto duration =
            core::audio::frames_to_ticks_floor(
                frame_count,
                sample_rate_);

        expected_device_frame_ =
            evidence.device_frame_position +
            static_cast<std::uint64_t>(frame_count);

        const auto duration_i64 =
            static_cast<std::int64_t>(
                std::min<std::uint64_t>(
                    duration,
                    static_cast<std::uint64_t>(
                        INT64_MAX)));

        expected_qpc_100ns_ =
            evidence.packet_start_qpc_100ns >
                    INT64_MAX - duration_i64
                ? INT64_MAX
                : evidence.packet_start_qpc_100ns +
                    duration_i64;
        have_expected_ = true;
    }

    std::uint32_t sample_rate_ = 0;
    bool have_expected_ = false;
    bool have_candidate_ = false;
    bool have_trusted_anchor_ = false;
    bool degraded_ = false;
    bool pending_reset_ = false;
    std::uint32_t consecutive_good_after_degraded_ = 0;
    std::uint32_t consecutive_bad_timestamps_ = 0;
    std::uint64_t expected_device_frame_ = 0;
    std::int64_t expected_qpc_100ns_ = 0;
    std::uint64_t last_candidate_device_frame_ = 0;
    std::uint64_t last_candidate_qpc_100ns_ = 0;
    std::uint64_t pending_reset_device_frame_ = 0;
    std::uint64_t pending_reset_qpc_100ns_ = 0;
};

enum class WasapiPacketPublishResult : std::uint8_t {
    Published = 0,
    InvalidPacket,
    PoolExhausted,
    QueueFull,
};

class WasapiPacketHandoff final {
public:
    WasapiPacketHandoff(
        std::size_t queue_capacity,
        std::size_t pool_capacity,
        std::size_t bytes_per_pool_slot)
        : queue_(queue_capacity),
          pool_(pool_capacity, bytes_per_pool_slot)
    {
    }

    WasapiPacketHandoff(
        const WasapiPacketHandoff &) = delete;
    WasapiPacketHandoff &operator=(
        const WasapiPacketHandoff &) = delete;

    [[nodiscard]] bool valid() const noexcept
    {
        return queue_.valid() && pool_.valid();
    }

    [[nodiscard]] WasapiPacketPublishResult publish(
        core::audio::AudioSourcePacket packet,
        std::span<const std::byte> payload) noexcept
    {
        using namespace core::audio;

        if (!packet.native_format.valid() ||
            packet.frame_count == 0)
            return WasapiPacketPublishResult::InvalidPacket;

        if (pending_discontinuity_)
            packet.flags |= AudioPacketFlag::Discontinuity;

        const bool silent =
            has_flag(packet.flags, AudioPacketFlag::Silent);

        if (silent) {
            if (!payload.empty())
                return WasapiPacketPublishResult::InvalidPacket;

            packet.pool_slot = kInvalidAudioPoolSlot;
            packet.payload_bytes = 0;
        } else {
            const auto expected =
                packet.native_format.bytes_for_frames(
                    packet.frame_count);
            if (payload.size() != expected)
                return WasapiPacketPublishResult::InvalidPacket;

            auto lease =
                pool_.try_acquire(expected);
            if (!lease.has_value()) {
                pool_exhaustions_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                pending_discontinuity_ = true;
                return WasapiPacketPublishResult::PoolExhausted;
            }

            std::memcpy(
                lease->bytes.data(),
                payload.data(),
                payload.size());

            packet.pool_slot = lease->slot;
            packet.payload_bytes =
                static_cast<std::uint32_t>(
                    payload.size());
        }

        if (!packet.metadata_valid()) {
            if (packet.pool_slot != kInvalidAudioPoolSlot)
                pool_.release(packet.pool_slot);
            return WasapiPacketPublishResult::InvalidPacket;
        }

        if (!queue_.try_push(packet)) {
            if (packet.pool_slot != kInvalidAudioPoolSlot)
                pool_.release(packet.pool_slot);
            ring_overflows_.fetch_add(
                1,
                std::memory_order_relaxed);
            pending_discontinuity_ = true;
            return WasapiPacketPublishResult::QueueFull;
        }

        pending_discontinuity_ = false;
        return WasapiPacketPublishResult::Published;
    }

    [[nodiscard]] bool try_pop(
        core::audio::AudioSourcePacket &packet) noexcept
    {
        return queue_.try_pop(packet);
    }

    [[nodiscard]] std::span<const std::byte> payload(
        const core::audio::AudioSourcePacket &packet) const noexcept
    {
        if (packet.pool_slot ==
            core::audio::kInvalidAudioPoolSlot)
            return {};

        return pool_.readable(
            packet.pool_slot,
            packet.payload_bytes);
    }

    [[nodiscard]] bool release(
        const core::audio::AudioSourcePacket &packet) noexcept
    {
        if (packet.pool_slot ==
            core::audio::kInvalidAudioPoolSlot)
            return core::audio::has_flag(
                       packet.flags,
                       core::audio::AudioPacketFlag::Silent) &&
                   packet.payload_bytes == 0;

        return pool_.release(packet.pool_slot);
    }

    std::size_t discard_pending() noexcept
    {
        std::size_t discarded = 0;
        core::audio::AudioSourcePacket packet;
        while (queue_.try_pop(packet)) {
            if (packet.pool_slot !=
                core::audio::kInvalidAudioPoolSlot)
                pool_.release(packet.pool_slot);
            ++discarded;
        }
        pending_discontinuity_ = false;
        return discarded;
    }

    [[nodiscard]] std::size_t outstanding_payload_leases() const noexcept
    {
        return pool_.in_use();
    }

    [[nodiscard]] std::size_t queue_depth_approx() const noexcept
    {
        return queue_.size_approx();
    }

    [[nodiscard]] std::size_t queue_high_water() const noexcept
    {
        return queue_.high_water();
    }

    [[nodiscard]] std::size_t pool_high_water() const noexcept
    {
        return pool_.high_water();
    }

    [[nodiscard]] std::uint64_t pool_exhaustions() const noexcept
    {
        return pool_exhaustions_.load(
            std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t ring_overflows() const noexcept
    {
        return ring_overflows_.load(
            std::memory_order_relaxed);
    }

private:
    core::audio::BoundedSpscQueue<
        core::audio::AudioSourcePacket> queue_;
    core::audio::FixedAudioPacketPool pool_;
    std::atomic<std::uint64_t> pool_exhaustions_{0};
    std::atomic<std::uint64_t> ring_overflows_{0};

    // Producer-owned: a dropped media packet makes the next successfully
    // published packet explicitly discontinuous.
    bool pending_discontinuity_ = false;
};

/*
 * Consumer-side packet lease.
 *
 * The lease pins the exact handoff/pool generation from which a packet was
 * popped. Restart may publish a new current handoff while an old packet is
 * still being inspected; payload/release therefore never re-resolve through a
 * mutable pointer and cannot target the wrong pool generation.
 */
class WasapiPacketLease final {
public:
    WasapiPacketLease() = default;

    WasapiPacketLease(
        std::shared_ptr<WasapiPacketHandoff> owner,
        core::audio::AudioSourcePacket packet) noexcept
        : owner_(std::move(owner)),
          packet_(packet),
          valid_(owner_ != nullptr)
    {
    }

    ~WasapiPacketLease()
    {
        reset();
    }

    WasapiPacketLease(
        const WasapiPacketLease &) = delete;
    WasapiPacketLease &operator=(
        const WasapiPacketLease &) = delete;

    WasapiPacketLease(
        WasapiPacketLease &&other) noexcept
        : owner_(std::move(other.owner_)),
          packet_(other.packet_),
          valid_(other.valid_)
    {
        other.packet_ = {};
        other.valid_ = false;
    }

    WasapiPacketLease &operator=(
        WasapiPacketLease &&other) noexcept
    {
        if (this == &other)
            return *this;

        reset();
        owner_ = std::move(other.owner_);
        packet_ = other.packet_;
        valid_ = other.valid_;
        other.packet_ = {};
        other.valid_ = false;
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return valid_ && owner_ != nullptr;
    }

    [[nodiscard]] const core::audio::AudioSourcePacket &
    packet() const noexcept
    {
        return packet_;
    }

    [[nodiscard]] std::span<const std::byte>
    payload() const noexcept
    {
        return valid()
            ? owner_->payload(packet_)
            : std::span<const std::byte>{};
    }

    [[nodiscard]] bool release() noexcept
    {
        if (!valid())
            return false;

        const bool released =
            owner_->release(packet_);
        owner_.reset();
        packet_ = {};
        valid_ = false;
        return released;
    }

    void reset() noexcept
    {
        if (valid())
            (void)owner_->release(packet_);

        owner_.reset();
        packet_ = {};
        valid_ = false;
    }

private:
    std::shared_ptr<WasapiPacketHandoff> owner_;
    core::audio::AudioSourcePacket packet_{};
    bool valid_ = false;
};

} // namespace arssyut::windows

#endif
