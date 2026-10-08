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
#include <string_view>
#include <thread>

namespace arssyut::windows {

enum class WasapiCaptureMode : std::uint8_t {
    Microphone = 0,
    Loopback,
};

enum class WasapiCaptureWaitResult : std::uint8_t {
    PacketReady = 0,
    StopRequested,
    Failed,
};

struct WasapiCapturePacketView {
    const std::byte *data = nullptr;
    std::uint32_t frame_count = 0;
    std::uint32_t flags = 0;
    std::uint64_t device_position = 0;
    std::uint64_t qpc_position_100ns = 0;
};

class IWasapiCaptureClient {
public:
    virtual ~IWasapiCaptureClient() = default;

    [[nodiscard]] virtual core::Status open(
        std::wstring_view endpoint_id,
        WasapiCaptureMode mode) noexcept = 0;

    [[nodiscard]] virtual core::audio::AudioFormat
    native_format() const noexcept = 0;

    [[nodiscard]] virtual std::uint32_t
    endpoint_buffer_frames() const noexcept = 0;

    [[nodiscard]] virtual core::Status start() noexcept = 0;

    [[nodiscard]] virtual WasapiCaptureWaitResult wait(
        HANDLE stop_event,
        HRESULT &failure_hr) noexcept = 0;

    [[nodiscard]] virtual HRESULT next_packet_size(
        std::uint32_t &frames) noexcept = 0;

    [[nodiscard]] virtual HRESULT get_packet(
        WasapiCapturePacketView &packet) noexcept = 0;

    [[nodiscard]] virtual HRESULT release_packet(
        std::uint32_t frames) noexcept = 0;

    virtual void stop() noexcept = 0;
};

enum class WasapiCaptureState : std::uint8_t {
    Idle = 0,
    Starting,
    Running,
    Stopping,
    Stopped,
    DeviceInvalidated,
    Failed,
};

struct WasapiCaptureOptions {
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

struct WasapiCaptureSnapshot {
    WasapiCaptureState state =
        WasapiCaptureState::Idle;
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

class WasapiCaptureSource final {
public:
    WasapiCaptureSource() = default;

    explicit WasapiCaptureSource(
        std::shared_ptr<IWasapiCaptureClient> client_override) noexcept
        : client_override_(std::move(client_override))
    {
    }

    ~WasapiCaptureSource();

    WasapiCaptureSource(
        const WasapiCaptureSource &) = delete;
    WasapiCaptureSource &operator=(
        const WasapiCaptureSource &) = delete;

    [[nodiscard]] core::Status start(
        std::wstring endpoint_id,
        core::audio::AudioSourceId source_id,
        WasapiCaptureMode mode,
        WasapiCaptureOptions options = {});

    void stop() noexcept;

    [[nodiscard]] bool try_pop(
        WasapiPacketLease &lease) noexcept;

    [[nodiscard]] WasapiCaptureSnapshot snapshot() const noexcept;

private:
    struct StartRequest {
        std::wstring endpoint_id;
        core::audio::AudioSourceId source_id =
            core::audio::AudioSourceId::Microphone;
        WasapiCaptureMode mode =
            WasapiCaptureMode::Microphone;
        WasapiCaptureOptions options{};
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

    std::atomic<WasapiCaptureState> state_{
        WasapiCaptureState::Idle};
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

    // Optional dependency injection for deterministic endpoint-client tests.
    // Production wrappers leave this null and use the native MMDevice/WASAPI
    // adapter created inside worker_main().
    std::shared_ptr<IWasapiCaptureClient> client_override_;
};

} // namespace arssyut::windows

#endif
