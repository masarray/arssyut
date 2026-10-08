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
        owner.get() == nullptr)
        return false;

    return swr_init(owner.get()) >= 0;
}

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

[[nodiscard]] double sine_fit_rms_error(
    std::span<const float> interleaved,
    double frequency_hz,
    int sample_rate)
{
    const std::size_t frames =
        interleaved.size() / kChannels;
    if (frames < 8'192 ||
        frequency_hz <= 0.0)
        return std::numeric_limits<double>::infinity();

    const std::size_t margin =
        std::min<std::size_t>(
            4'096,
            frames / 10);
    const std::size_t begin = margin;
    const std::size_t end = frames - margin;
    if (end <= begin + 1)
        return std::numeric_limits<double>::infinity();

    double ss = 0.0;
    double cc = 0.0;
    double sc = 0.0;
    double ys = 0.0;
    double yc = 0.0;

    for (std::size_t frame = begin;
         frame < end;
         ++frame) {
        const double phase =
            2.0 * kPi * frequency_hz *
            static_cast<double>(frame) /
            static_cast<double>(sample_rate);
        const double s = std::sin(phase);
        const double co = std::cos(phase);
        const double y =
            static_cast<double>(
                interleaved[frame * kChannels]);

        ss += s * s;
        cc += co * co;
        sc += s * co;
        ys += y * s;
        yc += y * co;
    }

    const double determinant =
        ss * cc - sc * sc;
    if (std::abs(determinant) < 1.0e-12)
        return std::numeric_limits<double>::infinity();

    const double a =
        (ys * cc - yc * sc) /
        determinant;
    const double b =
        (yc * ss - ys * sc) /
        determinant;

    double squared_error = 0.0;
    std::uint64_t count = 0;

    for (std::size_t frame = begin;
         frame < end;
         ++frame) {
        const double phase =
            2.0 * kPi * frequency_hz *
            static_cast<double>(frame) /
            static_cast<double>(sample_rate);
        const double predicted =
            a * std::sin(phase) +
            b * std::cos(phase);
        const double actual =
            static_cast<double>(
                interleaved[frame * kChannels]);
        const double error =
            actual - predicted;
        squared_error += error * error;
        ++count;
    }

    return count != 0
        ? std::sqrt(
              squared_error /
              static_cast<double>(count))
        : std::numeric_limits<double>::infinity();
}

[[nodiscard]] double multitone_fit_rms_error(
    std::span<const float> interleaved,
    double frequency_one_hz,
    double frequency_two_hz,
    int sample_rate)
{
    const std::size_t frames =
        interleaved.size() / kChannels;
    if (frames < 8'192 ||
        frequency_one_hz <= 0.0 ||
        frequency_two_hz <= 0.0 ||
        sample_rate <= 0)
        return std::numeric_limits<double>::infinity();

    const std::size_t margin =
        std::min<std::size_t>(
            4'096,
            frames / 10);
    const std::size_t begin = margin;
    const std::size_t end = frames - margin;
    if (end <= begin + 4)
        return std::numeric_limits<double>::infinity();

    double normal[4][5]{};

    for (std::size_t frame = begin;
         frame < end;
         ++frame) {
        const double t =
            static_cast<double>(frame) /
            static_cast<double>(sample_rate);

        const double basis[4]{
            std::sin(2.0 * kPi * frequency_one_hz * t),
            std::cos(2.0 * kPi * frequency_one_hz * t),
            std::sin(2.0 * kPi * frequency_two_hz * t),
            std::cos(2.0 * kPi * frequency_two_hz * t),
        };

        const double y =
            static_cast<double>(
                interleaved[frame * kChannels]);

        for (int row = 0; row < 4; ++row) {
            for (int column = 0;
                 column < 4;
                 ++column) {
                normal[row][column] +=
                    basis[row] *
                    basis[column];
            }
            normal[row][4] +=
                basis[row] * y;
        }
    }

    // Solve the 4x4 normal system with partial-pivot Gaussian elimination.
    for (int pivot = 0;
         pivot < 4;
         ++pivot) {
        int best = pivot;
        for (int row = pivot + 1;
             row < 4;
             ++row) {
            if (std::abs(
                    normal[row][pivot]) >
                std::abs(
                    normal[best][pivot])) {
                best = row;
            }
        }

        if (std::abs(
                normal[best][pivot]) <
            1.0e-12)
            return std::numeric_limits<double>::infinity();

        if (best != pivot) {
            for (int column = pivot;
                 column < 5;
                 ++column) {
                std::swap(
                    normal[pivot][column],
                    normal[best][column]);
            }
        }

        const double divisor =
            normal[pivot][pivot];
        for (int column = pivot;
             column < 5;
             ++column) {
            normal[pivot][column] /=
                divisor;
        }

        for (int row = 0;
             row < 4;
             ++row) {
            if (row == pivot)
                continue;

            const double factor =
                normal[row][pivot];
            for (int column = pivot;
                 column < 5;
                 ++column) {
                normal[row][column] -=
                    factor *
                    normal[pivot][column];
            }
        }
    }

    const double coefficient[4]{
        normal[0][4],
        normal[1][4],
        normal[2][4],
        normal[3][4],
    };

    double squared_error = 0.0;
    std::uint64_t count = 0;

    for (std::size_t frame = begin;
         frame < end;
         ++frame) {
        const double t =
            static_cast<double>(frame) /
            static_cast<double>(sample_rate);

        const double predicted =
            coefficient[0] *
                std::sin(
                    2.0 * kPi *
                    frequency_one_hz * t) +
            coefficient[1] *
                std::cos(
                    2.0 * kPi *
                    frequency_one_hz * t) +
            coefficient[2] *
                std::sin(
                    2.0 * kPi *
                    frequency_two_hz * t) +
            coefficient[3] *
                std::cos(
                    2.0 * kPi *
                    frequency_two_hz * t);

        const double actual =
            static_cast<double>(
                interleaved[frame * kChannels]);

        const double error =
            actual - predicted;
        squared_error +=
            error * error;
        ++count;
    }

    return count != 0
        ? std::sqrt(
              squared_error /
              static_cast<double>(count))
        : std::numeric_limits<double>::infinity();
}

[[nodiscard]] double max_second_difference(
    std::span<const float> interleaved)
{
    const std::size_t frames =
        interleaved.size() / kChannels;
    if (frames < 8'192)
        return std::numeric_limits<double>::infinity();

    const std::size_t margin =
        std::min<std::size_t>(
            4'096,
            frames / 10);
    const std::size_t begin =
        std::max<std::size_t>(
            margin,
            1);
    const std::size_t end =
        frames > margin
            ? frames - margin
            : 0;

    if (end <= begin + 1)
        return std::numeric_limits<double>::infinity();

    double maximum = 0.0;

    for (std::size_t frame = begin;
         frame + 1 < end;
         ++frame) {
        const double previous =
            static_cast<double>(
                interleaved[
                    (frame - 1) *
                    kChannels]);
        const double current =
            static_cast<double>(
                interleaved[
                    frame *
                    kChannels]);
        const double next =
            static_cast<double>(
                interleaved[
                    (frame + 1) *
                    kChannels]);

        maximum =
            std::max(
                maximum,
                std::abs(
                    next -
                    2.0 * current +
                    previous));
    }

    return maximum;
}

[[nodiscard]] bool convert_input(
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

    return true;
}

[[nodiscard]] bool drain_all(
    SwrContext *context,
    std::vector<float> &output,
    std::int64_t &post_flush_delay)
{
    if (context == nullptr)
        return false;

    // FFmpeg defines end-of-stream flushing by repeatedly calling
    // swr_convert() with NULL input and zero input frames. swr_get_delay()
    // describes the delay a *next* input sample would experience and therefore
    // is useful telemetry, but it is not the EOS predicate.
    constexpr int kDrainCapacity = 8'192;

    for (int iteration = 0;
         iteration < 128;
         ++iteration) {
        std::vector<float> chunk_out(
            static_cast<std::size_t>(
                kDrainCapacity) *
            kChannels);

        uint8_t *output_planes[1]{
            reinterpret_cast<uint8_t *>(
                chunk_out.data())};

        const int produced =
            swr_convert(
                context,
                output_planes,
                kDrainCapacity,
                nullptr,
                0);
        if (produced < 0)
            return false;

        output.insert(
            output.end(),
            chunk_out.begin(),
            chunk_out.begin() +
                static_cast<std::ptrdiff_t>(
                    produced * kChannels));

        if (produced == 0) {
            post_flush_delay =
                swr_get_delay(
                    context,
                    kOutputRate);
            return true;
        }
    }

    post_flush_delay =
        swr_get_delay(
            context,
            kOutputRate);
    return false;
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

    if (!convert_input(
            owner.get(),
            input,
            input_frames,
            output))
        return false;

    std::int64_t post_flush_delay = -1;
    if (!drain_all(
            owner.get(),
            output,
            post_flush_delay))
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
        << " post_flush_delay=" << post_flush_delay
        << '\n';

    return
        std::abs(frame_delta) <= 1 &&
        measured > 0.0 &&
        error <=
            std::max(
                0.25,
                tone_hz * 0.001);
}

struct CompensationEvidence {
    bool valid = false;
    std::int64_t frame_delta = 0;
    double expected_tone_one_hz = 0.0;
    double expected_tone_two_hz = 0.0;
    double multitone_fit_rms_error = 0.0;
    double coarse_repeat_drop_rms_error = 0.0;
    double max_second_difference = 0.0;
    std::int64_t post_flush_delay = -1;
};

[[nodiscard]] CompensationEvidence compensation_case(
    int ppm)
{
    CompensationEvidence evidence;

    SwrOwner owner;
    if (!create_context(
            kOutputRate,
            owner))
        return evidence;

    constexpr int kSegmentSeconds = 10;
    constexpr int kSegments = 6;
    constexpr int kSegmentFrames =
        kOutputRate * kSegmentSeconds;
    constexpr int kTotalFrames =
        kSegmentFrames * kSegments;
    // Two deliberately non-commensurate components prevent a coarse
    // repeat/drop implementation from hiding corrections at extrema or at one
    // component's cycle boundary.
    constexpr double kToneOneHz = 731.29;
    constexpr double kToneTwoHz = 1'237.61;
    constexpr double kToneOneAmplitude = 0.16;
    constexpr double kToneTwoAmplitude = 0.09;

    std::vector<float> output;
    output.reserve(
        static_cast<std::size_t>(
            kTotalFrames + 8'192) *
        kChannels);

    for (int segment = 0;
         segment < kSegments;
         ++segment) {
        const int sample_delta =
            static_cast<int>(
                std::llround(
                    static_cast<double>(
                        kSegmentFrames) *
                    static_cast<double>(ppm) /
                    1'000'000.0));

        const int compensation_result =
            swr_set_compensation(
                owner.get(),
                sample_delta,
                kSegmentFrames);
        if (compensation_result < 0)
            return evidence;

        std::vector<float> input(
            static_cast<std::size_t>(
                kSegmentFrames) *
            kChannels);

        const std::int64_t segment_base =
            static_cast<std::int64_t>(
                segment) *
            kSegmentFrames;

        for (int frame = 0;
             frame < kSegmentFrames;
             ++frame) {
            const std::int64_t absolute_frame =
                segment_base + frame;
            const double time_seconds =
                static_cast<double>(
                    absolute_frame) /
                static_cast<double>(
                    kOutputRate);
            const double value_double =
                kToneOneAmplitude *
                    std::sin(
                        2.0 * kPi *
                        kToneOneHz *
                        time_seconds) +
                kToneTwoAmplitude *
                    std::sin(
                        2.0 * kPi *
                        kToneTwoHz *
                        time_seconds);
            const float value =
                static_cast<float>(
                    value_double);

            input[
                static_cast<std::size_t>(frame) *
                kChannels] = value;
            input[
                static_cast<std::size_t>(frame) *
                kChannels + 1] = value;
        }

        if (!convert_input(
                owner.get(),
                input,
                kSegmentFrames,
                output))
            return evidence;
    }

    if (!drain_all(
            owner.get(),
            output,
            evidence.post_flush_delay))
        return evidence;

    const std::int64_t output_frames =
        static_cast<std::int64_t>(
            output.size() / kChannels);
    evidence.frame_delta =
        output_frames - kTotalFrames;

    const double effective_rate_scale =
        static_cast<double>(
            kTotalFrames) /
        static_cast<double>(
            output_frames);

    evidence.expected_tone_one_hz =
        kToneOneHz *
        effective_rate_scale;
    evidence.expected_tone_two_hz =
        kToneTwoHz *
        effective_rate_scale;

    evidence.multitone_fit_rms_error =
        multitone_fit_rms_error(
            output,
            evidence.expected_tone_one_hz,
            evidence.expected_tone_two_hz,
            kOutputRate);

    // Negative control: exact frame-count matching through nearest-neighbor
    // staircase time scaling. This deliberately repeats/drops samples instead
    // of applying a smooth fractional-rate change. The selected backend must
    // beat this control by a material margin on the identical fixture.
    std::vector<float> coarse_output(
        static_cast<std::size_t>(
            output_frames) *
        kChannels);

    for (std::int64_t output_frame = 0;
         output_frame < output_frames;
         ++output_frame) {
        const std::int64_t input_frame =
            std::min<std::int64_t>(
                kTotalFrames - 1,
                (output_frame *
                 static_cast<std::int64_t>(
                     kTotalFrames)) /
                    output_frames);

        const double time_seconds =
            static_cast<double>(
                input_frame) /
            static_cast<double>(
                kOutputRate);

        const float value =
            static_cast<float>(
                kToneOneAmplitude *
                    std::sin(
                        2.0 * kPi *
                        kToneOneHz *
                        time_seconds) +
                kToneTwoAmplitude *
                    std::sin(
                        2.0 * kPi *
                        kToneTwoHz *
                        time_seconds));

        coarse_output[
            static_cast<std::size_t>(
                output_frame) *
            kChannels] = value;
        coarse_output[
            static_cast<std::size_t>(
                output_frame) *
            kChannels + 1] = value;
    }

    evidence.coarse_repeat_drop_rms_error =
        multitone_fit_rms_error(
            coarse_output,
            evidence.expected_tone_one_hz,
            evidence.expected_tone_two_hz,
            kOutputRate);

    evidence.max_second_difference =
        max_second_difference(
            output);

    const std::int64_t expected_delta =
        static_cast<std::int64_t>(
            std::llround(
                static_cast<double>(
                    kTotalFrames) *
                static_cast<double>(ppm) /
                1'000'000.0));

    // Across six independently refreshed 10 s compensation windows, the
    // resampler may retain a few samples of fractional filter phase. Four
    // frames over 2.88M input frames is ~1.4 ppm and still tightly bounds the
    // requested ±100 ppm correction.
    const bool frame_count_ok =
        std::abs(
            evidence.frame_delta -
            expected_delta) <= 4;

    // Require a material quality gap from an explicit nearest-neighbor
    // repeat/drop negative control instead of relying on an arbitrary absolute
    // residual alone. A backend that merely redistributes coarse sample
    // insertions/deletions cannot pass because it converges on the control.
    constexpr double kMaximumResidual = 0.006;
    constexpr double kMaximumCoarseRatio = 0.80;

    const bool continuity_ok =
        std::isfinite(
            evidence.multitone_fit_rms_error) &&
        std::isfinite(
            evidence.coarse_repeat_drop_rms_error) &&
        evidence.coarse_repeat_drop_rms_error > 0.0 &&
        evidence.multitone_fit_rms_error <=
            kMaximumResidual &&
        evidence.multitone_fit_rms_error <=
            evidence.coarse_repeat_drop_rms_error *
            kMaximumCoarseRatio;

    evidence.valid =
        frame_count_ok &&
        continuity_ok;

    std::cout
        << "SWRESAMPLE_COMPENSATION"
        << " ppm=" << ppm
        << " segments=" << kSegments
        << " segment_frames=" << kSegmentFrames
        << " total_input_frames=" << kTotalFrames
        << " output_frames=" << output_frames
        << " expected_delta=" << expected_delta
        << " observed_delta="
        << evidence.frame_delta
        << " expected_tone_one_hz="
        << evidence.expected_tone_one_hz
        << " expected_tone_two_hz="
        << evidence.expected_tone_two_hz
        << " multitone_fit_rms_error="
        << evidence.multitone_fit_rms_error
        << " coarse_repeat_drop_rms_error="
        << evidence.coarse_repeat_drop_rms_error
        << " coarse_ratio="
        << (evidence.coarse_repeat_drop_rms_error > 0.0
                ? evidence.multitone_fit_rms_error /
                      evidence.coarse_repeat_drop_rms_error
                : std::numeric_limits<double>::infinity())
        << " maximum_residual="
        << kMaximumResidual
        << " maximum_coarse_ratio="
        << kMaximumCoarseRatio
        << " max_second_difference="
        << evidence.max_second_difference
        << " post_flush_delay="
        << evidence.post_flush_delay
        << '\n';

    return evidence;
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

    const CompensationEvidence plus =
        compensation_case(+100);
    const CompensationEvidence minus =
        compensation_case(-100);

    if (!static_ok ||
        !plus.valid ||
        !minus.valid)
        return 2;

    const bool opposite =
        plus.frame_delta != 0 &&
        minus.frame_delta != 0 &&
        ((plus.frame_delta > 0 &&
          minus.frame_delta < 0) ||
         (plus.frame_delta < 0 &&
          minus.frame_delta > 0));

    if (!opposite)
        return 3;

    std::cout
        << "SWRESAMPLE_BASELINE PASS"
        << " static_src=yes"
        << " soft_ppm=yes"
        << " non_silent_continuity=yes"
        << " repeated_compensation=yes"
        << " plus100_delta="
        << plus.frame_delta
        << " minus100_delta="
        << minus.frame_delta
        << '\n';

    return 0;
}

#endif
