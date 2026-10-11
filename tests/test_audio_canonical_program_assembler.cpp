#include "core/audio/audio_canonical_program_assembler.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>

using namespace arssyut::core::audio;

namespace {
struct Test {
    int failures = 0;
    int checks = 0;
    void check(bool ok, const char *description) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL: " << description << '\n';
        }
    }
};
constexpr std::int64_t kZero = 1'000'000;
std::int64_t due(std::uint64_t index) {
    return kZero + static_cast<std::int64_t>(
        frames_to_ticks_floor(index * 1024ULL + 1024, 48'000));
}

void suffix_and_deadline(Test &t)
{
    auto a = std::make_unique<CanonicalProgramAssembler>();
    a->reset(kZero, true, false);
    std::array<float, 1200 * 2> source{};
    for (std::size_t i = 0; i < 1200; ++i) {
        source[i * 2] = static_cast<float>(i) / 2000.0F;
        source[i * 2 + 1] = -static_cast<float>(i) / 2000.0F;
    }
    t.check(a->offer(AudioSourceId::Microphone, 0, source, 1200) ==
            CanonicalProgramOfferStatus::Applied,
            "assembler accepts 1200-frame canonical resampler output");
    t.check(!a->close_one_due(due(0) - 1) && a->pending_blocks() == 0,
            "program interval cannot close ahead of due deadline");
    t.check(a->close_one_due(due(0)) && a->pending_blocks() == 1,
            "first 1024-frame interval closes once due");
    AudioProgramBlock block{};
    std::uint64_t first = 1;
    t.check(a->try_take(block, first) && first == 0 &&
            block.audio.media_start_100ns == kZero &&
            block.audio.source_presence_mask == source_presence_bit(
                AudioSourceId::Microphone) &&
            block.audio.samples[0] == 0.0F &&
            block.audio.samples[1023*2] == 1023.0F/2000.0F,
            "first full 1024 frames retain absolute PTS and exact samples");
    t.check(!a->try_take(block, first),
            "no fake future audio when reader is empty");
    t.check(a->close_one_due(due(1)) && a->try_take(block, first) &&
            first == 1024 &&
            block.audio.samples[0] == 1024.0F/2000.0F &&
            block.audio.samples[175*2] == 1199.0F/2000.0F &&
            block.audio.samples[176*2] == 0.0F,
            "exact 176-frame future suffix retained and rest zero padded");
    t.check(a->mix_stats().blocks_closed == 2 &&
            a->stats().dequeued_program_blocks == 2,
            "close/dequeue counts agree with existing program clock");
}

void pause_discards_future_source_not_program_clock(Test &t)
{
    auto a = std::make_unique<CanonicalProgramAssembler>();
    a->reset(kZero, true, true);
    std::array<float, 2*1024> mic{}, sys{};
    mic.fill(0.25F); sys.fill(0.125F);
    t.check(a->offer(AudioSourceId::Microphone, 0, mic, 1024) ==
        CanonicalProgramOfferStatus::Applied &&
        a->offer(AudioSourceId::SystemAudio, 0, sys, 1024) ==
        CanonicalProgramOfferStatus::Applied,
        "Canonical program takes both live sources before Pause");
    t.check(a->close_one_due(due(0)),
        "Pre-Pause 1024-frame AAC program block closes");
    AudioProgramBlock block{};
    std::uint64_t first=0;
    t.check(a->try_take(block,first) && first==0,
        "First block leaves program buffer without resetting writer");

    t.check(a->offer(AudioSourceId::SystemAudio, 1024, sys, 1024) ==
        CanonicalProgramOfferStatus::Applied,
        "Source capture can get ahead of the Pause boundary");
    const auto due_before = a->next_due_100ns();
    a->discard_sources_after_pause();
    t.check(a->next_due_100ns() == due_before &&
            a->mix_stats().blocks_closed == 1 &&
            a->stats().dequeued_program_blocks == 1,
        "Pause discards only source PCM; never reinitializes AAC program clock");

    t.check(a->offer(AudioSourceId::SystemAudio, 1024, sys, 1024, true) ==
        CanonicalProgramOfferStatus::Applied &&
        a->offer(AudioSourceId::Microphone, 1024, mic, 1024, true) ==
        CanonicalProgramOfferStatus::Applied,
        "Resumed same-frame source PCM is allowed without StageGap");
    t.check(a->close_one_due(due(1)) &&
            a->try_take(block,first) && first==1024 &&
            block.audio.source_presence_mask==3 &&
            block.audio.samples[0]==0.375F,
        "Second AAC block has contiguous 1024-frame PTS and both sources");
    t.check(a->stats().staging_gaps==0 &&
            a->stats().dropped_program_blocks==0,
        "Paused source staging never corrupts 48k output clock or ring");
}

void mixed_dual_source(Test &t)
{
    auto a = std::make_unique<CanonicalProgramAssembler>();
    a->reset(kZero, true, true);
    std::array<float, 2*1024> mic, system;
    mic.fill(0.20F);
    system.fill(0.30F);
    t.check(a->offer(AudioSourceId::Microphone, 0, mic, 1024) ==
            CanonicalProgramOfferStatus::Applied &&
            a->offer(AudioSourceId::SystemAudio, 0, system, 1024, true) ==
            CanonicalProgramOfferStatus::Applied,
            "two independent source windows receive canonical input");
    t.check(a->close_one_due(due(0)), "dual source block closes");
    AudioProgramBlock block{};
    std::uint64_t first = 44;
    t.check(a->try_take(block, first) && first == 0 &&
            block.audio.source_presence_mask == 3 &&
            block.audio.discontinuity_mask ==
                source_presence_bit(AudioSourceId::SystemAudio) &&
            block.audio.samples[10] > 0.49F &&
            block.audio.samples[10] < 0.51F,
            "two-source mix sums samples and preserves source discontinuity");
    t.check(a->mix_stats().missing_source_intervals == 0,
            "both present sources do not count missing interval");
}

void queue_pressure_discontinuity(Test &t)
{
    auto a = std::make_unique<CanonicalProgramAssembler>();
    a->reset(kZero, true, false);
    std::array<float, 2048> samples{};
    samples.fill(0.25F);
    for (std::uint64_t i = 0; i < 5; ++i) {
        t.check(a->offer(AudioSourceId::Microphone,
                    static_cast<std::int64_t>(i * 1024ULL),
                    samples, 1024) == CanonicalProgramOfferStatus::Applied,
                "canonical source accepts one future interval");
        t.check(a->close_one_due(due(i)), "each source interval closes");
    }
    t.check(a->pending_blocks() == 4 &&
            a->stats().dropped_program_blocks == 1 &&
            a->mix_stats().output_overflow_blocks == 1,
            "bounded output stores only four blocks and drops fifth");
    AudioProgramBlock b{};
    std::uint64_t f = 0;
    t.check(a->try_take(b, f) && f == 0,
            "oldest output remains ordered when queue fills");
    t.check(a->offer(AudioSourceId::Microphone, 5*1024, samples, 1024) ==
                CanonicalProgramOfferStatus::Applied &&
            a->close_one_due(due(5)),
            "next output is queued after freeing one slot");
    for (int i = 0; i < 3; ++i)
        t.check(a->try_take(b, f), "earlier queued media still readable");
    t.check(a->try_take(b, f) && f == 5*1024 &&
            b.follows_output_discontinuity,
            "post-overflow program output flags discontinuity at real PTS");
}

void stale_silent_and_reset(Test &t)
{
    auto a = std::make_unique<CanonicalProgramAssembler>();
    a->reset(kZero, true, false);
    t.check(a->close_one_due(due(0)),
            "missing capture interval closes as deterministic silence");
    AudioProgramBlock b{};
    std::uint64_t f = 555;
    t.check(a->try_take(b, f) && f == 0 &&
            b.audio.source_presence_mask == 0 &&
            b.audio.samples[100] == 0 &&
            a->mix_stats().silent_blocks == 1,
            "no input produces real silent canonical program block");
    std::array<float, 2*512> samples{};
    samples.fill(0.125F);
    t.check(a->offer(AudioSourceId::Microphone, 0, samples, 512) ==
            CanonicalProgramOfferStatus::Applied &&
            a->close_one_due(due(1)),
            "late source data may arrive but cannot retime closed block");
    t.check(a->stats().stale_canonical_frames == 512 &&
            a->mix_stats().blocks_closed == 2,
            "late source frames discarded and clock keeps advancing");
    t.check(a->try_take(b, f) && f == 1024 &&
            b.audio.samples[0] == 0,
            "late samples cannot be shifted into a future interval");
    a->reset(kZero+10'000, false, true);
    t.check(!a->try_take(b, f) && a->pending_blocks() == 0 &&
            a->stats().stale_canonical_frames == 0 &&
            a->mix_stats().blocks_closed == 0,
            "new recording generation discards old output and telemetry");
}


void loopback_resume_after_silence(Test &t)
{
    auto assembler = std::make_unique<CanonicalProgramAssembler>();
    assembler->reset(kZero, true, true);
    std::array<float, 2 * 256> microphone{};
    std::array<float, 2 * 1024> initial_system{};
    std::array<float, 2 * 1024> resumed_system{};
    microphone.fill(0.10F);
    initial_system.fill(0.20F);
    resumed_system.fill(0.30F);

    // Audio source is initially buffered, then Windows loopback can jump
    // forward when YouTube starts playback after several seconds of silence.
    t.check(assembler->offer(AudioSourceId::SystemAudio, 0, initial_system,
                             1024) == CanonicalProgramOfferStatus::Applied &&
            assembler->offer(AudioSourceId::SystemAudio, 192000, resumed_system,
                             1024, true) == CanonicalProgramOfferStatus::Applied,
            "Loopback QPC gap is accepted without a fabricated PCM bridge");

    AudioProgramBlock block{};
    std::uint64_t first = 0;
    for (std::uint64_t n = 0; n < 188; ++n) {
        t.check(assembler->close_one_due(due(n)) &&
                assembler->try_take(block, first) &&
                first == n * 1024,
                "Canonical video/audio interval remains monotonically timed");
        if (n == 0) {
            t.check(block.audio.samples[0] == 0.20F,
                    "Initial system audio still renders");
        } else if (n == 1) {
            t.check(block.audio.source_presence_mask == 0 &&
                    block.audio.samples[0] == 0.0F,
                    "Actual loopback silence is not filled with replayed audio");
        } else if (n == 187) {
            // Block 187 begins at frame 191488; resumed frame 192000 is
            // exactly 512 frames into this canonical interval.
            t.check(block.audio.samples[511 * 2] == 0.0F &&
                    block.audio.samples[512 * 2] == 0.30F &&
                    (block.audio.discontinuity_mask &
                        source_presence_bit(AudioSourceId::SystemAudio)) != 0,
                    "YouTube onset is mixed at exact QPC frame and marked");
        }
    }
}

void invalid_and_noncontiguous(Test &t)
{
    auto a = std::make_unique<CanonicalProgramAssembler>();
    a->reset(kZero, true, false);
    std::array<float, 2048> data{};
    t.check(a->offer(AudioSourceId::SystemAudio, 0, data, 1024) ==
            CanonicalProgramOfferStatus::Inactive,
            "source not selected in start profile cannot publish");
    t.check(a->offer(AudioSourceId::Microphone, -1, data, 1024) ==
            CanonicalProgramOfferStatus::Invalid,
            "negative absolute frame index cannot poison program");
    t.check(a->offer(AudioSourceId::Microphone, 0, data, 1023) ==
            CanonicalProgramOfferStatus::Invalid,
            "wrong sample-span shape is rejected before staging");
    t.check(a->offer(AudioSourceId::Microphone, 0, data, 1024) ==
            CanonicalProgramOfferStatus::Applied,
            "valid source can be offered");
    t.check(a->offer(AudioSourceId::Microphone, 2048, data, 1024) ==
            CanonicalProgramOfferStatus::NonContiguous &&
            a->stats().staging_gaps == 1,
            "source QPC mapping gap fails closed, never invents continuity");
}
} // namespace

int main()
{
    Test t;
    suffix_and_deadline(t);
    mixed_dual_source(t);
    pause_discards_future_source_not_program_clock(t);
    queue_pressure_discontinuity(t);
    stale_silent_and_reset(t);
    loopback_resume_after_silence(t);
    invalid_and_noncontiguous(t);
    if (t.failures != 0) {
        std::cerr << "FAIL: " << t.failures << '/' << t.checks
                  << " assembler checks\n";
        return 1;
    }
    std::cout << "PASS: " << t.checks
              << " canonical P7A5 assembly checks\n";
}
