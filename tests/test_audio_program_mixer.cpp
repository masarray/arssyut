#include "core/audio/audio_program_clock.hpp"
#include "core/audio/audio_program_mixer.hpp"
#include "core/audio/audio_source_block_window.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <thread>

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

    auto transition = mixer.begin_block();
    test.expect(
        mixer.mix_source(transition, AudioSourceId::Microphone, signal),
        "Live microphone mute is accepted without touching the media clock");
    test.expect(
        transition.audio.samples[0] > 0.0F &&
        transition.audio.samples[0] < 0.5F &&
        transition.audio.samples[476] > 0.0F &&
        transition.audio.samples[476] < 0.01F &&
        transition.audio.samples[478] == 0.0F &&
        transition.audio.samples[480] == 0.0F,
        "Mute ramps to exact silence in 240 stereo frames without a click");

    auto muted = mixer.begin_block();
    test.expect(
        mixer.mix_source(muted, AudioSourceId::Microphone, signal),
        "Steady mute follows the original zero-work, zero-presence fast path");
    test.expect(
        muted.audio.source_presence_mask == 0 &&
        muted.audio.samples[0] == 0.0F &&
        muted.audio.samples[2047] == 0.0F,
        "Muted source produces no output or program discontinuity");

    mixer.set_source_config(AudioSourceId::Microphone, {1.0F, false});
    auto restored = mixer.begin_block();
    test.expect(mixer.mix_source(restored, AudioSourceId::Microphone, signal) &&
        restored.audio.samples[0] > 0.0F &&
        restored.audio.samples[0] < 0.01F &&
        restored.audio.samples[478] == 0.5F &&
        restored.audio.samples[480] == 0.5F,
        "Unmute ramps from silence then restores original audio bit-exact");
}

void test_live_mic_mute_reversal_never_modifies_system_audio(TestContext &test)
{
    using namespace arssyut::core::audio;
    AudioProgramMixer mixer;
    mixer.reset(0);
    std::array<float, CanonicalAudioBlock::kSampleCapacity> mic{};
    std::array<float, CanonicalAudioBlock::kSampleCapacity> system{};
    mic.fill(0.25F);
    system.fill(0.125F);

    auto initial = mixer.begin_block();
    test.expect(mixer.mix_source(initial, AudioSourceId::Microphone, mic) &&
                mixer.mix_source(initial, AudioSourceId::SystemAudio, system) &&
                initial.audio.samples[0] == 0.375F,
        "Unchanged dual-source input uses the existing bit-exact steady mix path");

    // Simulate a small packet rather than a full AAC block. Gain envelope
    // must persist across packet boundaries and a rapid reversal must not
    // jump back to full-scale 0.25 at the start of the next chunk.
    mixer.set_source_config(AudioSourceId::Microphone, {1.0F, true});
    auto fading = mixer.begin_block();
    fading.audio.clear(120);
    test.expect(mixer.mix_source(fading, AudioSourceId::Microphone,
        std::span<const float>(mic.data(), 240)) &&
        mixer.mix_source(fading, AudioSourceId::SystemAudio,
            std::span<const float>(system.data(), 240)),
        "Partial mic fade and unmodified system source both fit same program window");
    const float before_reversal = fading.audio.samples[238];

    mixer.set_source_config(AudioSourceId::Microphone, {1.0F, false});
    auto recovering = mixer.begin_block();
    recovering.audio.clear(120);
    test.expect(mixer.mix_source(recovering, AudioSourceId::Microphone,
        std::span<const float>(mic.data(), 240)) &&
        mixer.mix_source(recovering, AudioSourceId::SystemAudio,
            std::span<const float>(system.data(), 240)),
        "Reversal does not restart capture or AAC");
    const float after_reversal = recovering.audio.samples[0];
    test.expect(std::abs(after_reversal-before_reversal) < 0.005F &&
                after_reversal < 0.35F && after_reversal > 0.12F &&
                recovering.audio.samples[238] > after_reversal,
        "Rapid mute/unmute reverses smoothly from current gain, not unity");

    // Source-local muted intervals never move or attenuate System Audio.
    mixer.set_source_config(AudioSourceId::Microphone, {1.0F, true});
    auto silenced = mixer.begin_block();
    test.expect(mixer.mix_source(silenced, AudioSourceId::Microphone, mic) &&
        mixer.mix_source(silenced, AudioSourceId::SystemAudio, system) &&
        silenced.audio.samples[480] == 0.125F &&
        silenced.audio.samples[2047] == 0.125F,
        "System Audio remains bit-exact once Mic fade has reached silence");
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

void test_source_block_window(TestContext &test)
{
    using namespace arssyut::core::audio;

    AudioSourceBlockWindow window;
    window.begin(1'024);

    std::array<float, 200 * 2> first{};
    for (std::size_t frame = 0;
         frame < 200;
         ++frame) {
        first[frame * 2] =
            static_cast<float>(frame + 1);
        first[frame * 2 + 1] =
            -static_cast<float>(frame + 1);
    }

    const auto partial_stale =
        window.offer(
            1'000,
            std::span<const float>(
                first.data(),
                100 * 2),
            100);

    test.expect(
        partial_stale.status ==
            AudioWindowOfferStatus::Applied &&
        partial_stale.stale_prefix_frames == 24 &&
        partial_stale.frames_applied == 76 &&
        partial_stale.input_frames_consumed == 100,
        "Packet prefix before current block is trimmed while overlap is preserved");

    const auto duplicate =
        window.offer(
            1'024,
            std::span<const float>(
                first.data() + 24 * 2,
                76 * 2),
            76);

    test.expect(
        duplicate.frames_applied == 0 &&
        duplicate.duplicate_frames == 76,
        "Duplicate canonical overlap is diagnosed and never double-written");

    const auto future =
        window.offer(
            2'048,
            std::span<const float>(
                first.data(),
                20 * 2),
            20);

    test.expect(
        future.status ==
            AudioWindowOfferStatus::Future &&
        future.input_frames_consumed == 0 &&
        future.future_suffix_frames == 20,
        "Entirely future packet remains in caller-owned bounded storage");

    const auto crossing_end =
        window.offer(
            2'000,
            std::span<const float>(
                first.data(),
                100 * 2),
            100,
            true);

    test.expect(
        crossing_end.status ==
            AudioWindowOfferStatus::Applied &&
        crossing_end.frames_applied == 48 &&
        crossing_end.input_frames_consumed == 48 &&
        crossing_end.future_suffix_frames == 52,
        "Block-end overlap consumes only the closed prefix and retains future suffix");

    test.expect(
        window.discontinuity(),
        "Discontinuity touching current overlap is retained on the source window");

    const auto stale =
        window.offer(
            900,
            std::span<const float>(
                first.data(),
                100 * 2),
            100);

    test.expect(
        stale.status ==
            AudioWindowOfferStatus::FullyStale &&
        stale.input_frames_consumed == 100,
        "Packet ending before current block is fully stale and consumed");

    const auto rendered =
        window.interleaved();

    test.expect(
        rendered[0] == first[24 * 2] &&
        rendered[1] == first[24 * 2 + 1],
        "Trimmed packet lands at exact frame zero of current source window");

    test.expect(
        window.covered_frames() == 124 &&
        window.missing_frames() == 900,
        "Uncovered source frames remain deterministic silence");

    test.expect(
        rendered[500 * 2] == 0.0F &&
        rendered[500 * 2 + 1] == 0.0F,
        "Gap inside program interval is represented by exact silence");
}

void test_cross_thread_publication(TestContext &test)
{
    using namespace arssyut::core::audio;

    AudioProgramMixer mixer;
    mixer.reset(0);

    std::atomic<bool> start{false};
    std::atomic<bool> mixer_done{false};
    std::atomic<int> violations{0};

    std::thread control([&] {
        while (!start.load(
            std::memory_order_acquire)) {
        }

        for (int iteration = 0;
             iteration < 4'000;
             ++iteration) {
            mixer.set_source_config(
                AudioSourceId::Microphone,
                {
                    .gain =
                        (iteration & 1) != 0
                            ? 0.5F
                            : 1.5F,
                    .muted =
                        (iteration % 7) == 0,
                });
        }
    });

    std::thread telemetry([&] {
        while (!start.load(
            std::memory_order_acquire)) {
        }

        while (!mixer_done.load(
            std::memory_order_acquire)) {
            const auto level =
                mixer.level(
                    AudioSourceId::Microphone);
            const auto stats =
                mixer.stats();

            if (!std::isfinite(level.peak) ||
                !std::isfinite(level.rms) ||
                level.rms < 0.0F ||
                level.peak < 0.0F ||
                stats.blocks_emitted >
                    stats.blocks_closed) {
                violations.fetch_add(
                    1,
                    std::memory_order_relaxed);
            }
        }
    });

    start.store(
        true,
        std::memory_order_release);

    std::array<float, CanonicalAudioBlock::kSampleCapacity>
        signal{};
    signal.fill(0.25F);

    for (int iteration = 0;
         iteration < 4'000;
         ++iteration) {
        auto block =
            mixer.begin_block();

        if (!mixer.mix_source(
                block,
                AudioSourceId::Microphone,
                signal)) {
            violations.fetch_add(
                1,
                std::memory_order_relaxed);
        }

        if ((iteration % 13) == 0) {
            mixer.note_missing_source_interval(
                AudioSourceId::SystemAudio);
        }

        mixer.close_block(
            block,
            true);
    }

    mixer_done.store(
        true,
        std::memory_order_release);

    control.join();
    telemetry.join();

    const auto final_stats =
        mixer.stats();

    test.expect(
        violations.load(
            std::memory_order_relaxed) == 0,
        "Cross-thread config and telemetry publication remains coherent");
    test.expect(
        final_stats.blocks_closed == 4'000 &&
        final_stats.blocks_emitted == 4'000,
        "Mixer owner progress remains deterministic under concurrent readers");
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
    test_live_mic_mute_reversal_never_modifies_system_audio(test);
    test_overlap_stale_policy(test);
    test_output_overflow_preserves_timeline(test);
    test_final_block_clipping(test);
    test_gain_level_telemetry(test);
    test_source_block_window(test);
    test_cross_thread_publication(test);

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
