#pragma once

#include "core/audio/audio_drift.hpp"
#include "core/audio/audio_packet.hpp"
#include "core/audio/audio_time.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace arssyut::core::audio {

enum class AudioTimelineMapStatus : std::uint8_t {
    Mapped = 0,
    OverlapsMediaZero,
    FullyBeforeMediaZero,
    Invalid,
};

struct AudioTimelineMapResult {
    AudioTimelineMapStatus status =
        AudioTimelineMapStatus::Invalid;

    std::int64_t media_start_100ns = 0;
    std::int64_t canonical_start_frame = 0;
    std::uint32_t source_frames_before_zero = 0;
    bool discontinuity = false;
    bool drift_eligible = false;
    DriftAnchor drift_anchor{};

    [[nodiscard]] bool usable_for_timeline() const noexcept
    {
        return status == AudioTimelineMapStatus::Mapped ||
               status == AudioTimelineMapStatus::OverlapsMediaZero;
    }
};

/*
 * Maps one source packet into the RecorderSession media-zero domain.
 *
 * This class creates no clock. RecorderSession QPC zero remains authoritative.
 * Degraded/host timestamps may preserve timeline continuity, but only trusted
 * device/QPC evidence is exported as a drift anchor.
 */
class AudioSourceTimelineMapper final {
public:
    void reset(
        std::int64_t session_zero_qpc_100ns) noexcept
    {
        session_zero_qpc_100ns_ =
            session_zero_qpc_100ns;
        session_zero_valid_ =
            session_zero_qpc_100ns >= 0;
        mapped_packets_ = 0;
        rejected_packets_ = 0;
        pre_zero_frames_trimmed_ = 0;
    }

    [[nodiscard]] AudioTimelineMapResult map(
        const AudioSourcePacket &packet) noexcept
    {
        AudioTimelineMapResult result;

        result.discontinuity =
            has_flag(
                packet.flags,
                AudioPacketFlag::Discontinuity);

        if (!session_zero_valid_ ||
            !packet.metadata_valid() ||
            packet.timing.packet_start_qpc_100ns <= 0 ||
            packet.native_format.sample_rate == 0) {
            ++rejected_packets_;
            return result;
        }

        const auto packet_start =
            packet.timing.packet_start_qpc_100ns;

        const auto duration =
            frames_to_ticks_ceil(
                packet.frame_count,
                packet.native_format.sample_rate);

        const auto packet_end =
            saturating_add(
                packet_start,
                duration);

        if (packet_end <=
            session_zero_qpc_100ns_) {
            result.status =
                AudioTimelineMapStatus::
                    FullyBeforeMediaZero;
            result.source_frames_before_zero =
                packet.frame_count;
            pre_zero_frames_trimmed_ +=
                packet.frame_count;
            ++mapped_packets_;
            populate_drift(packet, result);
            return result;
        }

        if (packet_start <
            session_zero_qpc_100ns_) {
            const auto before_zero_ticks =
                static_cast<std::uint64_t>(
                    session_zero_qpc_100ns_ -
                    packet_start);

            result.source_frames_before_zero =
                std::min(
                    packet.frame_count,
                    ticks_to_frames_ceil(
                        before_zero_ticks,
                        packet.native_format.sample_rate));

            pre_zero_frames_trimmed_ +=
                result.source_frames_before_zero;

            if (result.source_frames_before_zero >=
                packet.frame_count) {
                result.status =
                    AudioTimelineMapStatus::
                        FullyBeforeMediaZero;
                result.source_frames_before_zero =
                    packet.frame_count;
                ++mapped_packets_;
                populate_drift(packet, result);
                return result;
            }

            const auto deficit_ticks =
                static_cast<std::uint64_t>(
                    session_zero_qpc_100ns_ -
                    packet_start);

            const auto retained_native_frames =
                static_cast<std::uint64_t>(
                    result.source_frames_before_zero);

            const auto source_rate =
                static_cast<std::uint64_t>(
                    packet.native_format.sample_rate);

            /*
             * Keep canonical placement rational. Converting the retained
             * native-frame timestamp through a ceil-rounded 100 ns tick can
             * cross a 48 kHz frame boundary even when the exact rational time
             * has not. Compute floor(A-B) directly where:
             *   A = trimmed_native_frames * 48000 / source_rate
             *   B = pre-zero_ticks * 48000 / 10,000,000.
             */
            const std::uint64_t source_scaled =
                retained_native_frames *
                CanonicalAudioBlock::
                    kSampleRate;
            const std::uint64_t source_whole =
                source_scaled /
                source_rate;
            const std::uint64_t source_remainder =
                source_scaled %
                source_rate;

            const std::uint64_t deficit_seconds =
                deficit_ticks /
                kMediaTicksPerSecond;
            const std::uint64_t deficit_tick_remainder =
                deficit_ticks %
                kMediaTicksPerSecond;

            const std::uint64_t deficit_fraction_scaled =
                deficit_tick_remainder *
                CanonicalAudioBlock::
                    kSampleRate;

            const std::uint64_t deficit_whole =
                deficit_seconds *
                    CanonicalAudioBlock::
                        kSampleRate +
                deficit_fraction_scaled /
                    kMediaTicksPerSecond;
            const std::uint64_t deficit_remainder =
                deficit_fraction_scaled %
                kMediaTicksPerSecond;

            const bool fractional_borrow =
                source_remainder *
                    kMediaTicksPerSecond <
                deficit_remainder *
                    source_rate;

            const std::uint64_t canonical_frame =
                source_whole >=
                        deficit_whole +
                            (fractional_borrow
                                 ? 1ULL
                                 : 0ULL)
                    ? source_whole -
                          deficit_whole -
                          (fractional_borrow
                               ? 1ULL
                               : 0ULL)
                    : 0ULL;

            const std::uint64_t retained_ticks_floor =
                (retained_native_frames *
                 kMediaTicksPerSecond) /
                    source_rate;

            const std::uint64_t media_start_floor =
                retained_ticks_floor >=
                        deficit_ticks
                    ? retained_ticks_floor -
                          deficit_ticks
                    : 0ULL;

            result.status =
                AudioTimelineMapStatus::
                    OverlapsMediaZero;
            result.media_start_100ns =
                media_start_floor >
                        static_cast<std::uint64_t>(
                            std::numeric_limits<
                                std::int64_t>::max())
                    ? std::numeric_limits<
                          std::int64_t>::max()
                    : static_cast<std::int64_t>(
                          media_start_floor);
            result.canonical_start_frame =
                canonical_frame >
                        static_cast<std::uint64_t>(
                            std::numeric_limits<
                                std::int64_t>::max())
                    ? std::numeric_limits<
                          std::int64_t>::max()
                    : static_cast<std::int64_t>(
                          canonical_frame);
        }
        else {
            const auto media_start =
                static_cast<std::uint64_t>(
                    packet_start -
                    session_zero_qpc_100ns_);

            result.status =
                AudioTimelineMapStatus::Mapped;
            result.media_start_100ns =
                media_start >
                        static_cast<std::uint64_t>(
                            std::numeric_limits<
                                std::int64_t>::max())
                    ? std::numeric_limits<
                          std::int64_t>::max()
                    : static_cast<std::int64_t>(
                          media_start);

            const auto canonical =
                ticks_to_frames_floor(
                    media_start,
                    CanonicalAudioBlock::
                        kSampleRate);

            result.canonical_start_frame =
                canonical >
                        static_cast<std::uint64_t>(
                            std::numeric_limits<
                                std::int64_t>::max())
                    ? std::numeric_limits<
                          std::int64_t>::max()
                    : static_cast<std::int64_t>(
                          canonical);
        }

        populate_drift(packet, result);
        ++mapped_packets_;
        return result;
    }

    [[nodiscard]] std::uint64_t
    mapped_packets() const noexcept
    {
        return mapped_packets_;
    }

    [[nodiscard]] std::uint64_t
    rejected_packets() const noexcept
    {
        return rejected_packets_;
    }

    [[nodiscard]] std::uint64_t
    pre_zero_frames_trimmed() const noexcept
    {
        return pre_zero_frames_trimmed_;
    }

private:
    [[nodiscard]] static constexpr std::uint32_t
    ticks_to_frames_ceil(
        std::uint64_t ticks,
        std::uint32_t sample_rate) noexcept
    {
        if (sample_rate == 0)
            return 0;

        const auto floor =
            ticks_to_frames_floor(
                ticks,
                sample_rate);

        const auto represented =
            frames_to_ticks_floor(
                floor,
                sample_rate);

        const auto value =
            represented < ticks
                ? floor + 1
                : floor;

        return value >
                std::numeric_limits<
                    std::uint32_t>::max()
            ? std::numeric_limits<
                  std::uint32_t>::max()
            : static_cast<std::uint32_t>(
                  value);
    }

    [[nodiscard]] static constexpr std::uint64_t
    frames_to_ticks_ceil(
        std::uint32_t frames,
        std::uint32_t sample_rate) noexcept
    {
        if (sample_rate == 0)
            return 0;

        const auto floor =
            frames_to_ticks_floor(
                frames,
                sample_rate);

        const auto represented =
            ticks_to_frames_floor(
                floor,
                sample_rate);

        return represented < frames &&
                       floor !=
                           std::numeric_limits<
                               std::uint64_t>::max()
            ? floor + 1
            : floor;
    }

    [[nodiscard]] static constexpr std::int64_t
    saturating_add(
        std::int64_t start,
        std::uint64_t delta) noexcept
    {
        if (delta >
            static_cast<std::uint64_t>(
                std::numeric_limits<
                    std::int64_t>::max()))
            return std::numeric_limits<
                std::int64_t>::max();

        const auto signed_delta =
            static_cast<std::int64_t>(
                delta);

        if (start >
            std::numeric_limits<
                std::int64_t>::max() -
                signed_delta)
            return std::numeric_limits<
                std::int64_t>::max();

        return start + signed_delta;
    }

    static void populate_drift(
        const AudioSourcePacket &packet,
        AudioTimelineMapResult &result) noexcept
    {
        result.drift_eligible =
            packet.eligible_for_drift();

        if (!result.drift_eligible)
            return;

        result.drift_anchor = {
            .device_frame_position =
                packet.timing.device_frame_position,
            .qpc_100ns =
                packet.timing.packet_start_qpc_100ns,
            .trusted = true,
        };
    }

    std::int64_t session_zero_qpc_100ns_ = 0;
    bool session_zero_valid_ = false;
    std::uint64_t mapped_packets_ = 0;
    std::uint64_t rejected_packets_ = 0;
    std::uint64_t pre_zero_frames_trimmed_ = 0;
};

} // namespace arssyut::core::audio
