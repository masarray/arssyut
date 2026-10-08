#include "core/audio/audio_canonical_staging.hpp"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

using namespace arssyut::core::audio;

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

void test_contiguous_future_suffix(TestContext &test)
{
    FixedCanonicalAudioStaging<2048> staging;

    std::array<float, 1200 * 2> samples{};
    for (std::size_t frame = 0;
         frame < 1200;
         ++frame) {
        samples[frame * 2] =
            static_cast<float>(frame);
        samples[frame * 2 + 1] =
            -static_cast<float>(frame);
    }

    test.expect(
        staging.push(
            0,
            samples,
            1200) ==
            AudioCanonicalStagePushStatus::Applied,
        "Canonical staging accepts contiguous SRC output");

    const auto first =
        staging.front_chunk();

    test.expect(
        first.valid() &&
        first.start_frame == 0 &&
        first.frame_count == 1200,
        "First physical chunk retains exact canonical coordinates");

    test.expect(
        staging.consume(1024),
        "Current program block can consume exactly 1024 frames");

    const auto suffix =
        staging.front_chunk();

    test.expect(
        suffix.valid() &&
        suffix.start_frame == 1024 &&
        suffix.frame_count == 176,
        "Future suffix is retained for the next program block");
    test.expect(
        suffix.interleaved[0] == 1024.0F &&
        suffix.interleaved[1] == -1024.0F,
        "Future suffix samples are not shifted or regenerated");
}

void test_wrap_and_high_water(TestContext &test)
{
    FixedCanonicalAudioStaging<1024> staging;

    std::array<float, 800 * 2> first{};
    std::array<float, 600 * 2> second{};

    first.fill(0.25F);
    second.fill(0.5F);

    test.expect(
        staging.push(
            100,
            first,
            800) ==
            AudioCanonicalStagePushStatus::Applied,
        "Initial ring append succeeds");

    test.expect(
        staging.consume(600),
        "Ring prefix consumption succeeds");

    test.expect(
        staging.push(
            900,
            std::span<const float>(
                second.data(),
                600 * 2),
            600) ==
            AudioCanonicalStagePushStatus::Applied,
        "Contiguous append wraps without allocation");

    test.expect(
        staging.size_frames() == 800 &&
        staging.high_water_frames() == 800,
        "Ring size/high-water stay bounded");

    const auto physical_tail =
        staging.front_chunk();
    test.expect(
        physical_tail.start_frame == 700 &&
        physical_tail.frame_count == 424,
        "Physical ring prefix from read index 600 spans 424 frames to wrap");

    test.expect(
        staging.consume(
            physical_tail.frame_count),
        "Wrapped first physical chunk consumes");

    const auto wrapped =
        staging.front_chunk();
    test.expect(
        wrapped.start_frame == 1124 &&
        wrapped.frame_count == 376,
        "Wrapped chunk starts at absolute frame 1124 with 376 remaining");
}

void test_discontinuity_boundary(TestContext &test)
{
    FixedCanonicalAudioStaging<2048> staging;

    std::array<float, 256 * 2> a{};
    std::array<float, 256 * 2> b{};

    test.expect(
        staging.push(
            0,
            a,
            256,
            false) ==
            AudioCanonicalStagePushStatus::Applied,
        "Pre-discontinuity media accepted");
    test.expect(
        staging.push(
            256,
            b,
            256,
            true) ==
            AudioCanonicalStagePushStatus::Applied,
        "Discontinuous contiguous suffix accepted");

    const auto before =
        staging.front_chunk();
    test.expect(
        before.frame_count == 256 &&
        !before.discontinuity,
        "Chunk is split before future discontinuity marker");

    test.expect(
        staging.consume(256),
        "Pre-discontinuity chunk consumed");

    const auto after =
        staging.front_chunk();
    test.expect(
        after.start_frame == 256 &&
        after.frame_count == 256 &&
        after.discontinuity,
        "Discontinuity survives until its exact future frame");
}

void test_fail_closed_gap_and_overflow(TestContext &test)
{
    FixedCanonicalAudioStaging<1024> staging;

    std::array<float, 900 * 2> large{};
    std::array<float, 200 * 2> extra{};

    test.expect(
        staging.push(
            10,
            large,
            900) ==
            AudioCanonicalStagePushStatus::Applied,
        "Bounded staging accepts initial media");

    test.expect(
        staging.push(
            911,
            std::span<const float>(extra.data(), 100 * 2),
            100) ==
            AudioCanonicalStagePushStatus::NonContiguous,
        "One-frame timeline gap fails closed");

    test.expect(
        staging.non_contiguous_events() == 1,
        "Non-contiguous source output is diagnosed");

    test.expect(
        staging.push(
            911,
            extra,
            100) ==
            AudioCanonicalStagePushStatus::Invalid,
        "Malformed source sample-span shape is distinct from a timeline gap");
    test.expect(
        staging.non_contiguous_events() == 1 &&
        staging.size_frames() == 900,
        "Malformed input never alters the canonical timeline or its counters");

    test.expect(
        staging.push(
            910,
            extra,
            200) ==
            AudioCanonicalStagePushStatus::Full,
        "Capacity overflow rejects whole append without partial media");

    test.expect(
        staging.size_frames() == 900 &&
        staging.overflow_events() == 1,
        "Overflow preserves previous staged media and bounded capacity");
}

} // namespace

int main()
{
    TestContext test;

    test_contiguous_future_suffix(test);
    test_wrap_and_high_water(test);
    test_discontinuity_boundary(test);
    test_fail_closed_gap_and_overflow(test);

    if (test.failures != 0) {
        std::cerr
            << test.failures << " of "
            << test.checks
            << " checks failed\n";
        return 1;
    }

    std::cout
        << "PASS: " << test.checks
        << " canonical staging checks\n";
    return 0;
}
