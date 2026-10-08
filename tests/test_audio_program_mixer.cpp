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

void test_silence_and_mute(TestContext &test)
{
    using namespace arssyut::core::audio;

    AudioProgramMixer mixer;
    mixer.reset(0);

    auto silent = mixer.begin_block();
    mixer.note_missing_source_interval();
    mixer.commit_block(silent);

    test.expect(
        silent.source_presence_mask == 0,
        "Missing source interval remains deterministic silence");
    test.expect(
        mixer.stats().silent_blocks == 1,
        "Silent block is counted without changing the timeline");

    mixer.set_source_config(
        AudioSourceId::Microphone,
        {
            .gain = 1.0F,
            .muted = true,
        });

    auto muted = mixer.begin_block();
    std::array<float, CanonicalAudioBlock::kSampleCapacity>
        signal{};
    signal.fill(0.5F);

    test.expect(
        mixer.mix_source(
            muted,
            AudioSourceId::Microphone,
            signal),
        "Muted source is accepted without restarting or shifting time");

    for (float sample : muted.interleaved()) {
        if (sample != 0.0F) {
            test.expect(
                false,
                "Muted source must leave canonical block silent");
            return;
        }
    }

    test.expect(
        muted.source_presence_mask == 0,
        "Muted source does not claim audible presence");
}

void test_mix_levels_and_stale_policy(TestContext &test)
{
    using namespace arssyut::core::audio;

    AudioProgramMixer mixer;
    mixer.reset(10'000'000);

    mixer.set_source_config(
        AudioSourceId::SystemAudio,
        {
            .gain = 2.0F,
            .muted = false,
        });

    auto block = mixer.begin_block();
    std::array<float, CanonicalAudioBlock::kSampleCapacity>
        signal{};

    for (std::size_t index = 0;
         index < signal.size();
         ++index)
        signal[index] = 0.3F;

    test.expect(
        mixer.mix_source(
            block,
            AudioSourceId::SystemAudio,
            signal),
        "Canonical stereo source mixes into current block");

    test.expect(
        (block.source_presence_mask &
         source_presence_bit(
             AudioSourceId::SystemAudio)) != 0,
        "Audible source sets presence bit");

    const auto level =
        mixer.level(
            AudioSourceId::SystemAudio);

    test.expect(
        std::abs(level.peak - 0.6F) < 0.0001F,
        "Latest-wins level telemetry reflects applied gain");

    AudioSourcePacket stale;
    stale.timing.packet_start_qpc_100ns =
        mixer.current_start_100ns() - 1;

    test.expect(
        mixer.packet_is_stale(stale),
        "Packet before current closed interval is stale");

    mixer.count_stale_packet();

    test.expect(
        mixer.stats().stale_packets_discarded == 1,
        "Stale media is counted for discard instead of time-shifted");
}

} // namespace

int main()
{
    TestContext test;

    test_program_clock(test);
    test_silence_and_mute(test);
    test_mix_levels_and_stale_policy(test);

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
