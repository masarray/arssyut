#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Psapi.h>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/version.h>
#include <libswresample/swresample.h>
#include <libswresample/version.h>
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr int kRate = 48'000;
constexpr int kChannels = 2;
constexpr int kChunkFrames = 1'024;
constexpr int kScratchFrames = 4'096;

class SwrOwner final {
public:
    ~SwrOwner()
    {
        if (context_ != nullptr)
            swr_free(&context_);
    }

    SwrContext **address() noexcept { return &context_; }
    SwrContext *get() const noexcept { return context_; }

private:
    SwrContext *context_ = nullptr;
};

[[nodiscard]] bool create_context(
    int input_rate,
    SwrOwner &owner)
{
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    const int result =
        swr_alloc_set_opts2(
            owner.address(),
            &stereo,
            AV_SAMPLE_FMT_FLT,
            kRate,
            &stereo,
            AV_SAMPLE_FMT_FLT,
            input_rate,
            0,
            nullptr);
    return result >= 0 &&
           owner.get() != nullptr &&
           swr_init(owner.get()) >= 0;
}

[[nodiscard]] std::uint64_t private_bytes() noexcept
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(
                &counters),
            sizeof(counters)))
        return 0;
    return static_cast<std::uint64_t>(
        counters.PrivateUsage);
}

struct StreamingScratch {
    std::vector<float> input;
    std::vector<float> output;

    StreamingScratch()
        : input(
              static_cast<std::size_t>(kChunkFrames) *
                  kChannels,
              0.0F),
          output(
              static_cast<std::size_t>(kScratchFrames) *
                  kChannels,
              0.0F)
    {
    }
};

[[nodiscard]] bool convert_zero_frames(
    SwrContext *context,
    std::int64_t input_frames,
    StreamingScratch &scratch,
    std::int64_t &produced_total) noexcept
{
    while (input_frames > 0) {
        const int frames =
            static_cast<int>(
                std::min<std::int64_t>(
                    input_frames,
                    kChunkFrames));

        const uint8_t *input_planes[1]{
            reinterpret_cast<const uint8_t *>(
                scratch.input.data())};
        uint8_t *output_planes[1]{
            reinterpret_cast<uint8_t *>(
                scratch.output.data())};

        const int produced =
            swr_convert(
                context,
                output_planes,
                kScratchFrames,
                input_planes,
                frames);
        if (produced < 0)
            return false;

        produced_total += produced;
        input_frames -= frames;
    }

    return true;
}

[[nodiscard]] bool drain_count(
    SwrContext *context,
    StreamingScratch &scratch,
    std::int64_t &produced_total,
    std::int64_t &tail_frames) noexcept
{
    tail_frames = 0;

    for (int iteration = 0;
         iteration < 128;
         ++iteration) {
        uint8_t *output_planes[1]{
            reinterpret_cast<uint8_t *>(
                scratch.output.data())};

        const int produced =
            swr_convert(
                context,
                output_planes,
                kScratchFrames,
                nullptr,
                0);
        if (produced < 0)
            return false;

        if (produced == 0)
            return true;

        produced_total += produced;
        tail_frames += produced;
    }

    return false;
}

[[nodiscard]] bool hours_equivalent_case(int ppm)
{
    SwrOwner owner;
    if (!create_context(kRate, owner))
        return false;

    StreamingScratch scratch;

    constexpr int kWindowSeconds = 10;
    constexpr int kWindowFrames =
        kRate * kWindowSeconds;
    constexpr int kWindows = 360; // one hour, 360 correction refreshes
    constexpr std::int64_t kTotalFrames =
        static_cast<std::int64_t>(kWindowFrames) *
        kWindows;

    std::int64_t produced_total = 0;
    const auto start =
        std::chrono::steady_clock::now();

    const std::uint64_t private_before =
        private_bytes();
    const auto input_capacity =
        scratch.input.capacity();
    const auto output_capacity =
        scratch.output.capacity();

    for (int window = 0;
         window < kWindows;
         ++window) {
        const int sample_delta =
            static_cast<int>(
                std::llround(
                    static_cast<double>(kWindowFrames) *
                    static_cast<double>(ppm) /
                    1'000'000.0));

        if (swr_set_compensation(
                owner.get(),
                sample_delta,
                kWindowFrames) < 0)
            return false;

        if (!convert_zero_frames(
                owner.get(),
                kWindowFrames,
                scratch,
                produced_total))
            return false;

        if (scratch.input.capacity() != input_capacity ||
            scratch.output.capacity() != output_capacity)
            return false;
    }

    std::int64_t tail_frames = 0;
    if (!drain_count(
            owner.get(),
            scratch,
            produced_total,
            tail_frames))
        return false;

    const auto end =
        std::chrono::steady_clock::now();
    const std::uint64_t private_after =
        private_bytes();

    const std::int64_t observed_delta =
        produced_total - kTotalFrames;
    const std::int64_t expected_delta =
        static_cast<std::int64_t>(
            std::llround(
                static_cast<double>(kTotalFrames) *
                static_cast<double>(ppm) /
                1'000'000.0));

    const std::int64_t error_frames =
        observed_delta - expected_delta;

    const double observed_ppm =
        static_cast<double>(observed_delta) *
        1'000'000.0 /
        static_cast<double>(kTotalFrames);
    const double effective_ppm_error =
        std::abs(
            observed_ppm -
            static_cast<double>(ppm));

    const auto elapsed_us =
        std::chrono::duration_cast<
            std::chrono::microseconds>(
            end - start)
            .count();

    const std::int64_t private_growth =
        static_cast<std::int64_t>(private_after) -
        static_cast<std::int64_t>(private_before);

    std::cout
        << "SWRESAMPLE_HOURS"
        << " ppm=" << ppm
        << " windows=" << kWindows
        << " total_input_frames=" << kTotalFrames
        << " expected_delta=" << expected_delta
        << " observed_delta=" << observed_delta
        << " error_frames=" << error_frames
        << " observed_ppm=" << observed_ppm
        << " effective_ppm_error="
        << effective_ppm_error
        << " tail_frames=" << tail_frames
        << " elapsed_us=" << elapsed_us
        << " private_growth_bytes=" << private_growth
        << " scratch_reallocations=0"
        << '\n';

    // The P7A7 drift budget is <=20 ms added over 60 minutes. A 1 ppm
    // resampler-rate error contributes only 3.6 ms over one hour, leaving
    // substantial budget for capture timestamp and writer scheduling error.
    constexpr double kMaximumEffectivePpmError = 1.0;

    return effective_ppm_error <=
               kMaximumEffectivePpmError &&
           std::abs(private_growth) <=
               4 * 1024 * 1024;
}

struct ImpulseEvidence {
    bool valid = false;
    std::size_t peak_frame = 0;
    std::size_t last_significant_frame = 0;
    std::size_t output_frames = 0;
    std::int64_t pre_drain_delay = 0;
    std::int64_t post_drain_delay = 0;
};

[[nodiscard]] ImpulseEvidence impulse_case(
    int input_rate,
    bool impulse_at_end)
{
    ImpulseEvidence evidence;
    SwrOwner owner;
    if (!create_context(input_rate, owner))
        return evidence;

    constexpr int kInputFrames = 8'192;
    std::vector<float> input(
        static_cast<std::size_t>(kInputFrames) *
        kChannels,
        0.0F);

    const int impulse_frame =
        impulse_at_end
            ? kInputFrames - 1
            : 0;
    input[
        static_cast<std::size_t>(impulse_frame) *
        kChannels] = 1.0F;
    input[
        static_cast<std::size_t>(impulse_frame) *
        kChannels + 1] = 1.0F;

    std::vector<float> scratch(
        static_cast<std::size_t>(kScratchFrames) *
        kChannels);
    std::vector<float> output;
    output.reserve(16'384 * kChannels);

    for (int base = 0;
         base < kInputFrames;
         base += kChunkFrames) {
        const int frames =
            std::min(
                kChunkFrames,
                kInputFrames - base);

        const uint8_t *input_planes[1]{
            reinterpret_cast<const uint8_t *>(
                input.data() +
                static_cast<std::size_t>(base) *
                    kChannels)};
        uint8_t *output_planes[1]{
            reinterpret_cast<uint8_t *>(
                scratch.data())};

        const int produced =
            swr_convert(
                owner.get(),
                output_planes,
                kScratchFrames,
                input_planes,
                frames);
        if (produced < 0)
            return evidence;

        output.insert(
            output.end(),
            scratch.begin(),
            scratch.begin() +
                static_cast<std::ptrdiff_t>(
                    produced * kChannels));
    }

    const std::size_t pre_drain_output_frames =
        output.size() / kChannels;

    evidence.pre_drain_delay =
        swr_get_delay(
            owner.get(),
            kRate);

    for (int iteration = 0;
         iteration < 128;
         ++iteration) {
        uint8_t *output_planes[1]{
            reinterpret_cast<uint8_t *>(
                scratch.data())};

        const int produced =
            swr_convert(
                owner.get(),
                output_planes,
                kScratchFrames,
                nullptr,
                0);
        if (produced < 0)
            return evidence;
        if (produced == 0)
            break;

        output.insert(
            output.end(),
            scratch.begin(),
            scratch.begin() +
                static_cast<std::ptrdiff_t>(
                    produced * kChannels));
    }

    evidence.post_drain_delay =
        swr_get_delay(
            owner.get(),
            kRate);
    evidence.output_frames =
        output.size() / kChannels;

    float peak = 0.0F;
    constexpr float kSignificant = 1.0e-5F;
    bool significant_in_drain = false;

    for (std::size_t frame = 0;
         frame < evidence.output_frames;
         ++frame) {
        const float magnitude =
            std::abs(
                output[frame * kChannels]);
        if (magnitude > peak) {
            peak = magnitude;
            evidence.peak_frame = frame;
        }
        if (magnitude >= kSignificant) {
            evidence.last_significant_frame = frame;
            if (frame >= pre_drain_output_frames)
                significant_in_drain = true;
        }
    }

    const std::uint64_t peak_delay_100ns =
        impulse_at_end
            ? 0
            : (10'000'000ULL *
               evidence.peak_frame) /
                  kRate;

    constexpr std::int64_t kMaximumBufferedDelayFrames = 64;
    const bool pre_drain_delay_bounded =
        evidence.pre_drain_delay >= 0 &&
        evidence.pre_drain_delay <=
            kMaximumBufferedDelayFrames;

    const bool eos_tail_ok =
        !impulse_at_end ||
        significant_in_drain;

    std::cout
        << "SWRESAMPLE_IMPULSE"
        << " input_rate=" << input_rate
        << " output_rate=" << kRate
        << " impulse_at_end="
        << (impulse_at_end ? 1 : 0)
        << " pre_drain_output_frames="
        << pre_drain_output_frames
        << " output_frames=" << evidence.output_frames
        << " peak_frame=" << evidence.peak_frame
        << " peak_delay_100ns=" << peak_delay_100ns
        << " last_significant_frame="
        << evidence.last_significant_frame
        << " significant_in_drain="
        << (significant_in_drain ? 1 : 0)
        << " pre_drain_delay="
        << evidence.pre_drain_delay
        << " post_drain_delay="
        << evidence.post_drain_delay
        << " peak=" << peak
        << '\n';

    constexpr std::uint64_t kMaxStartImpulseDelay100ns =
        1'000'000ULL;

    evidence.valid =
        peak > 0.01F &&
        pre_drain_delay_bounded &&
        evidence.post_drain_delay <= 32 &&
        eos_tail_ok &&
        (impulse_at_end ||
         peak_delay_100ns <=
             kMaxStartImpulseDelay100ns);

    return evidence;
}

} // namespace

int main()
{
    std::cout
        << "SWRESAMPLE_VERSION"
        << " swresample=" << swresample_version()
        << " avutil=" << avutil_version()
        << '\n';

    const bool impulse_ok =
        impulse_case(44'100, false).valid &&
        impulse_case(44'100, true).valid &&
        impulse_case(96'000, false).valid &&
        impulse_case(96'000, true).valid;

    const bool hour_plus =
        hours_equivalent_case(+100);
    const bool hour_minus =
        hours_equivalent_case(-100);

    if (!impulse_ok ||
        !hour_plus ||
        !hour_minus)
        return 2;

    std::cout
        << "SWRESAMPLE_HARDENING PASS"
        << " hours_equivalent=yes"
        << " impulse_tail=yes"
        << " reusable_scratch=yes"
        << '\n';
    return 0;
}

#endif
