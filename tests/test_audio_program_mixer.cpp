#include "core/audio/audio_program_clock.hpp"
#include "core/audio/audio_program_mixer.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

struct TestContext {
    int checks = 0;
    int failures = 0;

    void expect(bool condition, const char *message)
    {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

void test_program_clock(TestContext &test)
{
    using namespace arssyut::core::audio;

    AudioProgramClock clock;
    clock.reset(1'000'000);

    test.expect(
        clock.next_start_100ns() == 1'000'000,
        "Program clock starts exactly at RecorderSession media zero");

    const auto first_end =
        clock.next_end_100ns();

    test.expect(
        first_end > clock.next_start_100ns(),
        "Canonical block has positive duration");

    clock.advance();

    test.expect(
        clock.next_start_100ns() == first_end,
        "Adjacent blocks share an exact rational boundary");

    const auto block_45_start =
        clock.block_start_100ns(45);
    const auto expected =
        1'000'000 +
        static_cast<std::int64_t>(
            frames_to_ticks_floor(
                45ULL * 1'024ULL,
                48'000));

    test.expect(
        block_45_start == expected,
        "Program block time derives only from block index and media zero");
}

void test_silence_mute_and_meter_reset(TestContext &test)
{
    using namespace arssyut::core::audio;

    AudioProgramMixer mixer;
    mixer.reset(0);

    auto first = mixer.begin_block();
    std::array<float, CanonicalAudioBlock::kSampleCapacity>
        signal{};
    signal.fill(0.5F);

    test.expect(
        mixer.mix_source(
            first,
            AudioSourceId::Microphone,
            signal),
        "Audible microphone source mixes");
    mixer.close_block(first, true);

    test.expect(
        mixer.level(AudioSourceId::Microphone).peak > 0.0F,
        "Audible source publishes a nonzero latest meter");

    auto silent = mixer.begin_block();
    mixer.note_missing_source_interval(
        AudioSourceId::Microphone);
    mixer.close_block(silent, true);

    test.expect(
        silent.audio.source_presence_mask == 0,
        "Missing source interval remains deterministic silence");
    test.expect(
        mixer.stats().silent_blocks == 1,
        "Silent block is counted without changing the timeline");
    test.expect(
        mixer.level(AudioSourceId::Microphone).peak == 0.0F,
        "Missing source clears stale meter state");
    test.expect(
        mixer.stats().missing_source_intervals_by_source[
            source_index(AudioSourceId::Microphone)] == 1,
        "Missing interval telemetry remains source-specific");

    mixer.set_source_config(
        AudioSourceId::Microphone,
        {
            .gain = 1.0F,
            .muted = true,
        });

    auto muted = mixer.begin_block();
    test.expect(
        mixer.mix_source(
            muted,
            AudioSourceId::Microphone,
            signal),
        "Muted source is accepted without restarting or shifting time");

    for (float sample : muted.audio.interleaved()) {
        if (sample != 0.0F) {
            test.expect(
                false,
                "Muted source must leave canonical block silent");
            return;
        }
    }

    test.expect(
        muted.audio.source_presence_mask == 0,
        "Muted source does not claim audible presence");
}

void test_overlap_stale_policy(TestContext &test)
{
    using namespace arssyut::core::audio;

    AudioProgramMixer mixer;
    mixer.reset(10'000'000);

    const auto start =
        mixer.current_start_100ns();
    const auto end =
        mixer.current_end_100ns();

    const AudioProgramMediaSpan fully_stale{
        .media_start_100ns = start - 20'000,
        .media_end_100ns = start,
    };
    const AudioProgramMediaSpan overlapping{
        .media_start_100ns = start - 20'000,
        .media_end_100ns = start + 20'000,
    };
    const AudioProgramMediaSpan current{
        .media_start_100ns = start,
        .media_end_100ns = end,
    };

    test.expect(
        mixer.interval_is_fully_stale(
            fully_stale),
        "Interval ending at current block boundary is fully stale");
    test.expect(
        !mixer.interval_is_fully_stale(
            overlapping),
        "Packet overlapping current block is not discarded wholesale");
    test.expect(
        !mixer.interval_is_fully_stale(
            current),
        "Current interval is not stale");

    mixer.count_stale_packet();
    test.expect(
        mixer.stats().stale_packets_discarded == 1,
        "Fully stale media is counted for discard");
}

void test_output_overflow_preserves_timeline(TestContext &test)
{
    using namespace arssyut::core::audio;

    AudioProgramMixer mixer;
    mixer.reset(0);

    const auto first_start =
        mixer.current_start_100ns();

    auto dropped = mixer.begin_block();
    mixer.close_block(
        dropped,
        false);

    test.expect(
        mixer.stats().output_overflow_blocks == 1,
        "Rejected writer handoff is counted as output overflow");
    test.expect(
        mixer.current_start_100ns() >
            first_start,
        "Dropped output still closes and advances the program interval");

    auto recovered = mixer.begin_block();
    test.expect(
        recovered.follows_output_discontinuity,
        "First block after output loss carries discontinuity state");

    mixer.close_block(
        recovered,
        true);

    test.expect(
        mixer.stats().output_discontinuities == 1,
        "Successful recovery consumes one output-discontinuity marker");
    test.expect(
        mixer.stats().blocks_closed == 2 &&
        mixer.stats().blocks_emitted == 1,
        "Closed versus emitted blocks remain separately observable");
}

void test_final_block_clipping(TestContext &test)
{
    using namespace arssyut::core::audio;

    AudioProgramMixer mixer;
    mixer.reset(0);

    mixer.set_source_config(
        AudioSourceId::SystemAudio,
        {
            .gain = 2.0F,
            .muted = false,
        });

    std::array<float, CanonicalAudioBlock::kSampleCapacity>
        positive{};
    std::array<float, CanonicalAudioBlock::kSampleCapacity>
        negative{};

    positive.fill(0.6F);
    negative.fill(-0.3F);

    auto cancelled = mixer.begin_block();

    test.expect(
        mixer.mix_source(
            cancelled,
            AudioSourceId::SystemAudio,
            positive),
        "First source may transiently exceed range");
    test.expect(
        mixer.mix_source(
            cancelled,
            AudioSourceId::Microphone,
            negative),
        "Second source can bring final program sample back in range");

    mixer.close_block(
        cancelled,
        true);

    test.expect(
        mixer.stats().clipped_samples == 0,
        "Program clipping is counted from final committed samples only");

    auto clipped = mixer.begin_block();
    positive.fill(0.75F);

    test.expect(
        mixer.mix_source(
            clipped,
            AudioSourceId::SystemAudio,
            positive),
        "Final over-range block mixes");

    mixer.close_block(
        clipped,
        true);

    test.expect(
        mixer.stats().clipped_samples ==
            CanonicalAudioBlock::kSampleCapacity,
        "Each final over-range program sample is counted exactly once");
}

void test_gain_level_telemetry(TestContext &test)
{
    using namespace arssyut::core::audio;

    AudioProgramMixer mixer;
    mixer.reset(0);

    mixer.set_source_config(
        AudioSourceId::SystemAudio,
        {
            .gain = 2.0F,
            .muted = false,
        });

    auto block = mixer.begin_block();
    std::array<float, CanonicalAudioBlock::kSampleCapacity>
        signal{};
    signal.fill(0.3F);

    test.expect(
        mixer.mix_source(
            block,
            AudioSourceId::SystemAudio,
            signal),
        "Canonical stereo source mixes into current block");

    const auto level =
        mixer.level(
            AudioSourceId::SystemAudio);

    test.expect(
        std::abs(level.peak - 0.6F) < 0.0001F,
        "Latest-wins level telemetry reflects applied gain");
}

} // namespace

int main()
{
    TestContext test;

    test_program_clock(test);
    test_silence_mute_and_meter_reset(test);
    test_overlap_stale_policy(test);
    test_output_overflow_preserves_timeline(test);
    test_final_block_clipping(test);
    test_gain_level_telemetry(test);

    if (test.failures != 0) {
        std::cerr
            << test.failures << " of "
            << test.checks
            << " checks failed\n";
        return 1;
    }

    std::cout
        << "PASS: " << test.checks
        << " P7A5 canonical mixer core checks\n";
    return 0;
}
