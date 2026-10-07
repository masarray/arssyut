#pragma once

#ifdef _WIN32

#include "core/audio/audio_format.hpp"
#include "core/audio/audio_packet.hpp"
#include "core/result/status.hpp"
#include "platform/windows/audio/wasapi_audio_utils.hpp"

#include <Windows.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>

namespace arssyut::windows {

enum class WasapiMicrophoneState : std::uint8_t {
    Idle = 0,
    Starting,
    Running,
    Stopping,
    Stopped,
    DeviceInvalidated,
    Failed,
};

struct WasapiMicrophoneOptions {
    std::size_t queue_capacity = 32;
    std::size_t packet_pool_capacity = 32;

    [[nodiscard]] bool valid() const noexcept
    {
        return queue_capacity >= 2 &&
               packet_pool_capacity >= 2 &&
               queue_capacity <= 4'096 &&
               packet_pool_capacity <= 4'096;
    }
};

struct WasapiMicrophoneSnapshot {
    WasapiMicrophoneState state =
        WasapiMicrophoneState::Idle;
    core::audio::AudioFormat native_format{};
    std::uint32_t endpoint_buffer_frames = 0;
    std::uint32_t last_hresult = 0;
    bool mmcss_active = false;

    std::uint64_t captured_packets = 0;
    std::uint64_t published_packets = 0;
    std::uint64_t silent_packets = 0;
    std::uint64_t discontinuity_packets = 0;
    std::uint64_t timestamp_error_packets = 0;
    std::uint64_t dropped_packets = 0;

    std::size_t queue_depth = 0;
    std::size_t queue_high_water = 0;
    std::size_t pool_high_water = 0;
    std::uint64_t pool_exhaustions = 0;
    std::uint64_t ring_overflows = 0;
};

class WasapiMicrophoneSource final {
public:
    WasapiMicrophoneSource() = default;
    ~WasapiMicrophoneSource();

    WasapiMicrophoneSource(
        const WasapiMicrophoneSource &) = delete;
    WasapiMicrophoneSource &operator=(
        const WasapiMicrophoneSource &) = delete;

    [[nodiscard]] core::Status start(
        std::wstring endpoint_id,
        WasapiMicrophoneOptions options = {});

    void stop() noexcept;

    [[nodiscard]] bool try_pop(
        WasapiPacketLease &lease) noexcept;

    [[nodiscard]] WasapiMicrophoneSnapshot snapshot() const noexcept;

private:
    struct StartRequest {
        std::wstring endpoint_id;
        WasapiMicrophoneOptions options{};
    };

    void worker_main(
        StartRequest request) noexcept;

    [[nodiscard]] bool signal_startup(
        core::Status status) noexcept;

    void request_stop() noexcept;

    [[nodiscard]] bool stop_requested() const noexcept;

    [[nodiscard]] core::Status prepare_for_start() noexcept;

    void retire_handoff() noexcept;

    void reset_terminal_handoff_telemetry() noexcept;

    void set_terminal_error(
        HRESULT hr,
        bool device_invalidated) noexcept;

    mutable std::mutex lifecycle_mutex_;
    bool start_in_progress_ = false;
    std::thread worker_;
    HANDLE stop_event_ = nullptr;

    std::mutex startup_mutex_;
    std::condition_variable startup_cv_;
    bool startup_done_ = false;
    core::Status startup_status_{};
    std::atomic<bool> startup_cancel_requested_{false};

    mutable std::mutex metadata_mutex_;
    core::audio::AudioFormat native_format_{};
    std::uint32_t endpoint_buffer_frames_ = 0;

    std::shared_ptr<WasapiPacketHandoff> handoff_;
    std::atomic<std::shared_ptr<WasapiPacketHandoff>> handoff_view_{};

    std::atomic<WasapiMicrophoneState> state_{
        WasapiMicrophoneState::Idle};
    std::atomic<std::uint32_t> last_hresult_{0};
    std::atomic<bool> mmcss_active_{false};

    std::atomic<std::uint64_t> captured_packets_{0};
    std::atomic<std::uint64_t> published_packets_{0};
    std::atomic<std::uint64_t> silent_packets_{0};
    std::atomic<std::uint64_t> discontinuity_packets_{0};
    std::atomic<std::uint64_t> timestamp_error_packets_{0};
    std::atomic<std::uint64_t> dropped_packets_{0};

    // Stop releases the fixed handoff/pool, but post-stop diagnostics still
    // need the pressure evidence accumulated by that recording generation.
    std::atomic<std::size_t> terminal_queue_high_water_{0};
    std::atomic<std::size_t> terminal_pool_high_water_{0};
    std::atomic<std::uint64_t> terminal_pool_exhaustions_{0};
    std::atomic<std::uint64_t> terminal_ring_overflows_{0};
};

} // namespace arssyut::windows

#endif
