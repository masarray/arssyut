#ifdef _WIN32

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <vector>

namespace {

constexpr int kOutputRate = 48'000;
constexpr int kChannels = 2;
constexpr double kPi = 3.14159265358979323846;

class SwrOwner final {
public:
    ~SwrOwner()
    {
        if (context_ != nullptr)
            swr_free(&context_);
    }

    SwrContext **address() noexcept
    {
        return &context_;
    }

    SwrContext *get() const noexcept
    {
        return context_;
    }

private:
    SwrContext *context_ = nullptr;
};

[[nodiscard]] double estimate_frequency_hz(
    std::span<const float> interleaved,
    int sample_rate)
{
    const std::size_t frames =
        interleaved.size() / kChannels;
    if (frames < 4'096)
        return 0.0;

    const std::size_t margin =
        std::min<std::size_t>(
            2'048,
            frames / 8);
    const std::size_t begin = margin;
    const std::size_t end = frames - margin;
    if (end <= begin + 1)
        return 0.0;

    bool have_first = false;
    double first_crossing = 0.0;
    double last_crossing = 0.0;
    std::uint64_t crossing_count = 0;

    float previous =
        interleaved[(begin - 1) * kChannels];

    for (std::size_t frame = begin;
         frame < end;
         ++frame) {
        const float current =
            interleaved[frame * kChannels];

        if (previous <= 0.0F &&
            current > 0.0F) {
            const double denominator =
                static_cast<double>(current) -
                static_cast<double>(previous);
            const double fraction =
                denominator > 0.0
                    ? -static_cast<double>(previous) /
                          denominator
                    : 0.0;
            const double crossing =
                static_cast<double>(frame - 1) +
                std::clamp(
                    fraction,
                    0.0,
                    1.0);

            if (!have_first) {
                first_crossing = crossing;
                have_first = true;
            }
            last_crossing = crossing;
            ++crossing_count;
        }

        previous = current;
    }

    if (crossing_count < 2 ||
        last_crossing <= first_crossing)
        return 0.0;

    return
        static_cast<double>(crossing_count - 1) *
        static_cast<double>(sample_rate) /
        (last_crossing - first_crossing);
}

[[nodiscard]] bool create_context(
    int input_rate,
    SwrOwner &owner)
{
    AVChannelLayout stereo =
        AV_CHANNEL_LAYOUT_STEREO;

    const int result =
        swr_alloc_set_opts2(
            owner.address(),
            &stereo,
            AV_SAMPLE_FMT_FLT,
            kOutputRate,
            &stereo,
            AV_SAMPLE_FMT_FLT,
            input_rate,
            0,
            nullptr);

    if (result < 0 ||
        owner.get() == nullptr) {
        std::cerr
            << "swr_alloc_set_opts2 failed: "
            << result << '\n';
        return false;
    }

    const int init_result =
        swr_init(owner.get());
    if (init_result < 0) {
        std::cerr
            << "swr_init failed: "
            << init_result << '\n';
        return false;
    }

    return true;
}

[[nodiscard]] bool convert_all(
    SwrContext *context,
    std::span<const float> input,
    int input_frames,
    std::vector<float> &output)
{
    if (context == nullptr ||
        input_frames < 0 ||
        input.size() !=
            static_cast<std::size_t>(input_frames) *
                kChannels)
        return false;

    constexpr int kChunkFrames = 1'024;

    for (int base = 0;
         base < input_frames;
         base += kChunkFrames) {
        const int frames =
            std::min(
                kChunkFrames,
                input_frames - base);

        const int out_capacity =
            swr_get_out_samples(
                context,
                frames);
        if (out_capacity <= 0)
            return false;

        std::vector<float> chunk_out(
            static_cast<std::size_t>(out_capacity) *
            kChannels);

        const uint8_t *input_planes[1]{
            reinterpret_cast<const uint8_t *>(
                input.data() +
                static_cast<std::size_t>(base) *
                    kChannels)};

        uint8_t *output_planes[1]{
            reinterpret_cast<uint8_t *>(
                chunk_out.data())};

        const int produced =
            swr_convert(
                context,
                output_planes,
                out_capacity,
                input_planes,
                frames);

        if (produced < 0)
            return false;

        output.insert(
            output.end(),
            chunk_out.begin(),
            chunk_out.begin() +
                static_cast<std::ptrdiff_t>(
                    produced * kChannels));
    }

    for (int iteration = 0;
         iteration < 64;
         ++iteration) {
        const std::int64_t delay =
            swr_get_delay(
                context,
                kOutputRate);
        if (delay <= 0)
            break;

        const int out_capacity =
            static_cast<int>(
                std::min<std::int64_t>(
                    delay + 64,
                    8'192));

        std::vector<float> chunk_out(
            static_cast<std::size_t>(out_capacity) *
            kChannels);

        uint8_t *output_planes[1]{
            reinterpret_cast<uint8_t *>(
                chunk_out.data())};

        const int produced =
            swr_convert(
                context,
                output_planes,
                out_capacity,
                nullptr,
                0);

        if (produced < 0)
            return false;
        if (produced == 0)
            break;

        output.insert(
            output.end(),
            chunk_out.begin(),
            chunk_out.begin() +
                static_cast<std::ptrdiff_t>(
                    produced * kChannels));
    }

    return true;
}

[[nodiscard]] bool static_src_case(
    int input_rate,
    double tone_hz)
{
    SwrOwner owner;
    if (!create_context(
            input_rate,
            owner))
        return false;

    const int input_frames =
        input_rate;

    std::vector<float> input(
        static_cast<std::size_t>(input_frames) *
        kChannels);

    for (int frame = 0;
         frame < input_frames;
         ++frame) {
        const double phase =
            2.0 * kPi * tone_hz *
            static_cast<double>(frame) /
            static_cast<double>(input_rate);
        const float value =
            static_cast<float>(
                0.25 * std::sin(phase));
        input[
            static_cast<std::size_t>(frame) *
            kChannels] = value;
        input[
            static_cast<std::size_t>(frame) *
            kChannels + 1] = value;
    }

    std::vector<float> output;
    output.reserve(
        static_cast<std::size_t>(
            kOutputRate + 4'096) *
        kChannels);

    if (!convert_all(
            owner.get(),
            input,
            input_frames,
            output))
        return false;

    const std::int64_t output_frames =
        static_cast<std::int64_t>(
            output.size() / kChannels);
    const std::int64_t nominal_frames =
        static_cast<std::int64_t>(
            (static_cast<std::int64_t>(
                 input_frames) *
             kOutputRate +
             input_rate / 2) /
            input_rate);
    const std::int64_t frame_delta =
        output_frames - nominal_frames;

    const double measured =
        estimate_frequency_hz(
            output,
            kOutputRate);
    const double error =
        std::abs(measured - tone_hz);

    std::cout
        << "SWRESAMPLE_STATIC"
        << " input_rate=" << input_rate
        << " output_rate=" << kOutputRate
        << " tone_hz=" << tone_hz
        << " measured_hz=" << measured
        << " tone_error_hz=" << error
        << " output_frames=" << output_frames
        << " nominal_frames=" << nominal_frames
        << " frame_delta=" << frame_delta
        << " final_delay="
        << swr_get_delay(
               owner.get(),
               kOutputRate)
        << '\n';

    return
        std::abs(frame_delta) <= 256 &&
        measured > 0.0 &&
        error <=
            std::max(
                0.25,
                tone_hz * 0.001);
}

[[nodiscard]] std::int64_t compensation_case(
    int ppm)
{
    SwrOwner owner;
    if (!create_context(
            kOutputRate,
            owner))
        return std::numeric_limits<std::int64_t>::min();

    constexpr int kSeconds = 10;
    constexpr int kFrames =
        kOutputRate * kSeconds;
    constexpr int kCompensationDistance =
        kFrames;

    const int sample_delta =
        static_cast<int>(
            std::llround(
                static_cast<double>(
                    kCompensationDistance) *
                static_cast<double>(ppm) /
                1'000'000.0));

    const int compensation_result =
        swr_set_compensation(
            owner.get(),
            sample_delta,
            kCompensationDistance);
    if (compensation_result < 0) {
        std::cerr
            << "swr_set_compensation failed"
            << " ppm=" << ppm
            << " result="
            << compensation_result
            << '\n';
        return std::numeric_limits<std::int64_t>::min();
    }

    std::vector<float> input(
        static_cast<std::size_t>(kFrames) *
            kChannels,
        0.0F);
    std::vector<float> output;
    output.reserve(
        static_cast<std::size_t>(
            kFrames + 4'096) *
        kChannels);

    if (!convert_all(
            owner.get(),
            input,
            kFrames,
            output))
        return std::numeric_limits<std::int64_t>::min();

    const std::int64_t output_frames =
        static_cast<std::int64_t>(
            output.size() / kChannels);
    const std::int64_t delta =
        output_frames - kFrames;

    std::cout
        << "SWRESAMPLE_COMPENSATION"
        << " ppm=" << ppm
        << " requested_sample_delta="
        << sample_delta
        << " distance="
        << kCompensationDistance
        << " output_frames="
        << output_frames
        << " observed_delta="
        << delta
        << " final_delay="
        << swr_get_delay(
               owner.get(),
               kOutputRate)
        << '\n';

    return delta;
}

} // namespace

int main()
{
    const bool static_ok =
        static_src_case(
            44'100,
            440.0) &&
        static_src_case(
            44'100,
            1'000.0) &&
        static_src_case(
            96'000,
            440.0) &&
        static_src_case(
            96'000,
            1'000.0);

    const std::int64_t plus_delta =
        compensation_case(+100);
    const std::int64_t minus_delta =
        compensation_case(-100);

    if (!static_ok ||
        plus_delta ==
            std::numeric_limits<std::int64_t>::min() ||
        minus_delta ==
            std::numeric_limits<std::int64_t>::min())
        return 2;

    const bool opposite =
        plus_delta != 0 &&
        minus_delta != 0 &&
        ((plus_delta > 0 &&
          minus_delta < 0) ||
         (plus_delta < 0 &&
          minus_delta > 0));

    const bool magnitude_plausible =
        std::abs(std::abs(plus_delta) - 48) <= 4 &&
        std::abs(std::abs(minus_delta) - 48) <= 4;

    if (!opposite ||
        !magnitude_plausible) {
        std::cerr
            << "Soft compensation did not produce"
            << " the expected bounded opposite frame deltas"
            << '\n';
        return 3;
    }

    std::cout
        << "SWRESAMPLE_BASELINE PASS"
        << " static_src=yes"
        << " soft_ppm=yes"
        << " plus100_delta=" << plus_delta
        << " minus100_delta=" << minus_delta
        << '\n';

    return 0;
}

#endif
