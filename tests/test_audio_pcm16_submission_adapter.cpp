#include "app/audio_pcm16_submission_adapter.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <span>

namespace {
using arssyut::app::audio::AacPcm16SubmissionAdapter;
using arssyut::app::audio::AacPrepareStatus;
using arssyut::core::audio::AudioProgramBlock;
using arssyut::core::audio::frames_to_ticks_floor;

constexpr std::int64_t kZero = 100'000'000;
int checked = 0;
int failures = 0;
void require(bool truth, const char *label)
{
    ++checked;
    if (!truth) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

AudioProgramBlock make_block(std::uint64_t first_frame)
{
    AudioProgramBlock result;
    result.audio.clear();
    result.audio.media_start_100ns =
        kZero + static_cast<std::int64_t>(
            frames_to_ticks_floor(first_frame, 48'000));
    return result;
}

void one_block_and_clip()
{
    AacPcm16SubmissionAdapter a;
    a.reset(kZero);
    auto b = make_block(0);
    b.audio.samples[0] = 1.0F;
    b.audio.samples[1] = -1.0F;
    b.audio.samples[2] = 2.0F;
    b.audio.samples[3] = -2.0F;
    b.audio.samples[4] = 0.5F;
    b.audio.samples[5] = -0.5F;
    b.audio.samples[6] = std::numeric_limits<float>::quiet_NaN();
    b.audio.samples[7] = std::numeric_limits<float>::infinity();
    b.audio.discontinuity_mask = 1;
    auto r = a.prepare(b, 0);
    require(r.status == AacPrepareStatus::Ready, "prepare full canonical block");
    require(r.view.frames == 1024 &&
            r.view.interleaved.size() == 2048, "exact AAC input quantum");
    require(r.view.relative_pts_100ns == 0 &&
            r.view.duration_100ns == 213'333, "exact first-block rational PTS");
    require(r.view.interleaved[0] == 32767 &&
            r.view.interleaved[1] == -32768, "full-scale PCM16 endpoints");
    require(r.view.interleaved[2] == 32767 &&
            r.view.interleaved[3] == -32768, "out-of-range values saturate");
    require(r.view.interleaved[4] == 16384 &&
            r.view.interleaved[5] == -16384, "explicit deterministic rounding");
    require(r.view.interleaved[6] == 0 &&
            r.view.interleaved[7] == 0, "NaN/Inf do not reach AAC");
    require(r.view.discontinuity, "source discontinuity is carried");
    require(a.prepare(b, 0).status == AacPrepareStatus::Busy,
        "cannot overwrite in-flight writer input");
    require(a.finish(true), "successful writer acknowledgment");
    require(a.stats().clipped_samples == 2 &&
            a.stats().non_finite_samples == 2, "normalization diagnostics");
    require(a.stats().submitted_frames == 1024, "accepted frame count");
    require(!a.finish(true), "duplicate writer acknowledgment rejected");
    require(a.prepare(b, 0).status == AacPrepareStatus::OutOfOrder,
        "duplicate block is not written twice");
}

void backpressure_preserves_later_timestamps()
{
    AacPcm16SubmissionAdapter a;
    a.reset(kZero);
    auto first = make_block(0);
    auto r = a.prepare(first, 0);
    require(r.status == AacPrepareStatus::Ready, "writer attempt ready");
    require(a.finish(false), "backpressured sample dropped");
    auto next = make_block(1024);
    r = a.prepare(next, 1024);
    require(r.status == AacPrepareStatus::Ready &&
            r.view.relative_pts_100ns == 213'333,
        "future block keeps original PTS after backpressure");
    require(r.view.discontinuity, "writer drop marks next accepted output discontinuous");
    require(a.finish(true), "later block accepted");
    require(a.stats().dropped_blocks == 1 &&
            a.stats().submitted_blocks == 1,
        "bounded writer attempt counted without retry queue");
    auto future = make_block(3072);
    r = a.prepare(future, 3072);
    require(r.status == AacPrepareStatus::Ready &&
            r.view.discontinuity, "skipped future canonical block diagnosed");
    require(a.stats().skipped_frames == 1024,
        "exact missing-frame count maintained");
    require(a.finish(true), "future block accepted after missing interval");
}

void live_then_stop_tail()
{
    AacPcm16SubmissionAdapter a;
    a.reset(kZero);
    auto first = make_block(0);
    require(a.prepare(first, 0).status == AacPrepareStatus::Ready,
        "live block has no stop restriction");
    require(a.finish(true), "first full block submitted");

    auto tail = make_block(1024);
    // The exact 1280-frame boundary lies at 266666.666.. 100ns ticks.
    // A floor timestamp (266666) is still BEFORE that frame boundary and
    // must admit only 255 complete tail frames. A ceil timestamp (266667)
    // admits exactly 256. Do not conceal that 100ns quantization detail.
    const auto stop_floor = kZero +
        static_cast<std::int64_t>(frames_to_ticks_floor(1024 + 256, 48'000));
    AacPcm16SubmissionAdapter floor_gate;
    floor_gate.reset(kZero);
    const auto floor_result = floor_gate.prepare(tail, 1024, stop_floor);
    require(floor_result.status == AacPrepareStatus::Ready &&
            floor_result.view.frames == 255,
            "floor stop instant conservatively excludes partial final sample");
    require(floor_gate.finish(true),
            "floor-limited stop tail accepted without future retime");

    const auto stop = stop_floor + 1;
    auto r = a.prepare(tail, 1024, stop);
    require(r.status == AacPrepareStatus::Ready &&
            r.view.frames == 256 &&
            r.view.interleaved.size() == 512, "stop tail trimmed to 256 frames");
    require(r.view.relative_pts_100ns ==
            static_cast<std::int64_t>(frames_to_ticks_floor(1024, 48'000)),
            "tail retains exact canonical start PTS");
    require(r.view.duration_100ns ==
            static_cast<std::int64_t>(
              frames_to_ticks_floor(1280, 48'000) -
              frames_to_ticks_floor(1024, 48'000)),
            "tail duration uses global rational frame boundary");
    require(a.finish(true), "tail submitted");
    require(a.stats().trimmed_stop_frames == 768 &&
            a.stats().submitted_frames == 1280,
            "stop trim and frame counters truthful");
    require(a.prepare(make_block(2048), 2048, stop).status ==
            AacPrepareStatus::PastStop, "no samples written after stop");
}

void malformed_and_overflow_fail_closed()
{
    AacPcm16SubmissionAdapter a;
    a.reset(-1);
    require(a.prepare(make_block(0), 0).status ==
            AacPrepareStatus::Invalid, "negative media zero fails closed");
    a.reset(kZero);
    auto b = make_block(0);
    b.audio.media_start_100ns += 1;
    require(a.prepare(b, 0).status == AacPrepareStatus::Invalid,
        "invented shifted media clock rejected");
    b = make_block(0);
    b.audio.frame_count = 1200;
    require(a.prepare(b, 0).status == AacPrepareStatus::Invalid,
        "oversized block never leaks beyond AAC buffer");
    b.audio.frame_count = 1024;
    require(a.prepare(b, 1).status == AacPrepareStatus::Invalid,
        "non-canonical block index rejected");
    require(a.prepare(b, 0, kZero - 1).status == AacPrepareStatus::Invalid,
        "stop before media zero rejected");
    require(a.prepare(b, std::numeric_limits<std::uint64_t>::max()).status ==
            AacPrepareStatus::Invalid, "overflow frame index rejected");
    require(a.stats().invalid_blocks == 5,
        "every malformed input counted");
}

void hours_equivalent_no_pts_accumulation()
{
    AacPcm16SubmissionAdapter a;
    a.reset(kZero);
    // Test ~87 s sequential cadence AND the exact 1-hour canonical offset
    // without spending minutes synthesizing a full hour of PCM values.
    constexpr std::uint64_t blocks = 4'096;
    for (std::uint64_t i = 0; i < blocks; ++i) {
        const auto frame = i * 1024ULL;
        auto b = make_block(frame);
        const auto r = a.prepare(b, frame);
        if (r.status != AacPrepareStatus::Ready ||
            r.view.relative_pts_100ns != static_cast<std::int64_t>(
                frames_to_ticks_floor(frame, 48'000)) ||
            r.view.duration_100ns != static_cast<std::int64_t>(
                frames_to_ticks_floor(frame + 1024, 48'000) -
                frames_to_ticks_floor(frame, 48'000)) ||
            !a.finish(true)) {
            require(false, "sequential blocks keep rational timestamps");
            return;
        }
    }
    require(a.stats().submitted_blocks == blocks,
        "sequential blocks submitted");
    require(a.stats().submitted_frames == blocks * 1024,
        "no accumulated frame-count rounding");

    constexpr std::uint64_t one_hour_frame =
        ((48'000ULL * 3600ULL) / 1024ULL) * 1024ULL;
    auto future = make_block(one_hour_frame);
    auto r = a.prepare(future, one_hour_frame);
    require(r.status == AacPrepareStatus::Ready &&
            r.view.relative_pts_100ns == static_cast<std::int64_t>(
                frames_to_ticks_floor(one_hour_frame, 48'000)) &&
            r.view.duration_100ns == static_cast<std::int64_t>(
                frames_to_ticks_floor(one_hour_frame + 1024, 48'000) -
                frames_to_ticks_floor(one_hour_frame, 48'000)),
            "1-hour exact rational timestamp without extra clock");
    require(r.view.discontinuity, "jump to future interval marked");
    require(a.finish(true), "future offset submitted");
    require(a.stats().skipped_frames ==
            one_hour_frame - blocks * 1024,
            "exact skipped interval recorded without retiming");
}


struct FakeProgramWriter {
    int calls = 0;
    arssyut::core::Status next_status =
        arssyut::core::Status::success();
    bool throws = false;
    std::int64_t last_pts = -1;
    std::int64_t last_duration = 0;
    bool last_discontinuity = false;
    std::size_t last_samples = 0;
    std::int16_t first_pcm16 = 0;

    [[nodiscard]] arssyut::core::Status write_audio_pcm16(
        std::span<const std::int16_t> interleaved,
        arssyut::core::TimePoint pts,
        std::int64_t duration,
        bool discontinuity)
    {
        ++calls;
        last_pts = pts.ticks_100ns;
        last_duration = duration;
        last_discontinuity = discontinuity;
        last_samples = interleaved.size();
        first_pcm16 = interleaved[0];
        if (throws)
            throw std::runtime_error("writer failed unexpectedly");
        return next_status;
    }
};

void single_writer_submission_contract()
{
    using arssyut::core::Status;
    using arssyut::core::StatusCode;
    AacPcm16SubmissionAdapter adapter;
    FakeProgramWriter writer;
    adapter.reset(kZero);
    auto b = make_block(0);
    b.audio.samples[0] = 0.5F;
    auto result = adapter.submit_to(writer, b, 0);
    require(result.prepare_status == AacPrepareStatus::Ready &&
            result.writer_attempted && result.submitted &&
            result.writer_status.ok() && !adapter.has_pending(),
        "single synchronous AAC submission pairs prepare/write/finish");
    require(writer.calls == 1 && writer.last_samples == 2048 &&
            writer.last_pts == 0 && writer.last_duration == 213'333 &&
            writer.first_pcm16 == 16384 && !writer.last_discontinuity,
        "writer receives canonical PCM16 with exact initial PTS");

    auto invalid = b;
    invalid.audio.media_start_100ns += 1;
    result = adapter.submit_to(writer, invalid, 1024);
    require(result.prepare_status == AacPrepareStatus::Invalid &&
            !result.writer_attempted && writer.calls == 1 &&
            result.writer_status.code == StatusCode::InvalidArgument,
        "invalid timestamp fails before touching Media Foundation");
    result = adapter.submit_to(writer, b, 0);
    require(result.prepare_status == AacPrepareStatus::OutOfOrder &&
            !result.writer_attempted &&
            result.writer_status.code == StatusCode::InvalidStateTransition &&
            writer.calls == 1,
        "duplicate output cannot be sent to writer twice");

    writer.next_status = Status::failure(
        StatusCode::EncoderBackpressure, 0xABCD);
    const auto second = make_block(1024);
    result = adapter.submit_to(writer, second, 1024);
    require(result.writer_attempted && !result.submitted &&
            result.writer_status.code == StatusCode::EncoderBackpressure &&
            result.writer_status.detail == 0xABCD &&
            !adapter.has_pending() && writer.calls == 2,
        "writer backpressure is surfaced and leaves no pending sample");

    writer.next_status = Status::success();
    const auto third = make_block(2048);
    result = adapter.submit_to(writer, third, 2048);
    require(result.submitted && writer.last_discontinuity &&
            writer.last_pts == static_cast<std::int64_t>(
                frames_to_ticks_floor(2048, 48'000)) &&
            adapter.stats().dropped_blocks == 1,
        "backpressure marks next AAC sample without retiming media");

    writer.throws = true;
    const auto fourth = make_block(3072);
    result = adapter.submit_to(writer, fourth, 3072);
    require(!result.submitted && result.writer_attempted &&
            result.writer_status.code == StatusCode::InternalError &&
            !adapter.has_pending() &&
            adapter.stats().dropped_blocks == 2,
        "unexpected writer exception cannot strand an in-flight PCM buffer");

    writer.throws = false;
    const auto fifth = make_block(4096);
    result = adapter.submit_to(writer, fifth, 4096);
    require(result.submitted && writer.last_discontinuity &&
            adapter.stats().submitted_blocks == 3,
        "writer resumes from canonical PTS with discontinuity after exception");

    // Deliberately hold the old direct prepare() contract: the newer API
    // must not overwrite another attempt's fixed 2048-sample staging buffer.
    const auto pending = make_block(5120);
    require(adapter.prepare(pending, 5120).status == AacPrepareStatus::Ready,
        "pending attempt exists for Busy regression");
    const int before_calls = writer.calls;
    result = adapter.submit_to(writer, pending, 5120);
    require(result.prepare_status == AacPrepareStatus::Busy &&
            !result.writer_attempted && writer.calls == before_calls,
        "busy adapter never overwrites or double-writes in-flight sample");
    require(adapter.finish(false), "manual pending attempt released");
}

void single_writer_stop_and_reset()
{
    AacPcm16SubmissionAdapter adapter;
    FakeProgramWriter writer;
    adapter.reset(kZero);
    const auto first = make_block(0);
    const auto stop = kZero +
        static_cast<std::int64_t>(frames_to_ticks_floor(256, 48'000)) + 1;
    auto result = adapter.submit_to(writer, first, 0, stop);
    require(result.submitted && writer.last_samples == 512 &&
            writer.last_duration ==
                static_cast<std::int64_t>(
                    frames_to_ticks_floor(256, 48'000)),
        "stop cuts AAC submission to 256 complete canonical frames");
    result = adapter.submit_to(writer, make_block(1024), 1024, stop);
    require(result.prepare_status == AacPrepareStatus::PastStop &&
            !result.writer_attempted && result.writer_status.ok() &&
            writer.calls == 1, "after-stop audio is dropped before writer");

    adapter.reset(kZero);
    result = adapter.submit_to(writer, make_block(0), 0);
    require(result.submitted && writer.last_pts == 0 &&
            !writer.last_discontinuity &&
            adapter.stats().submitted_blocks == 1 &&
            adapter.stats().dropped_blocks == 0,
        "new recording generation resets writer media-time and error state");
}

} // namespace

int main()
{
    one_block_and_clip();
    backpressure_preserves_later_timestamps();
    live_then_stop_tail();
    malformed_and_overflow_fail_closed();
    hours_equivalent_no_pts_accumulation();
    single_writer_submission_contract();
    single_writer_stop_and_reset();

    if (failures != 0) {
        std::cerr << "FAIL: " << failures << "/" << checked
                  << " P7A6 AAC submission checks\n";
        return 1;
    }
    std::cout << "PASS: " << checked
              << " P7A6 AAC submission checks\n";
    return 0;
}
