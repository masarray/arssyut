#include "core/audio/audio_sample_normalizer.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

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

[[nodiscard]] AudioFormat format(
    AudioSampleType type,
    std::uint16_t bits,
    std::uint16_t valid_bits) noexcept
{
    return {
        .sample_rate = 48'000,
        .sample_type = type,
        .channels = 2,
        .container_bits_per_sample = bits,
        .valid_bits_per_sample = valid_bits,
        .channel_mask = kStereoChannelMask,
        .block_align =
            static_cast<std::uint16_t>(
                2 * (bits / 8)),
    };
}

void write_u16(
    std::span<std::byte> bytes,
    std::size_t offset,
    std::uint16_t value)
{
    bytes[offset] =
        static_cast<std::byte>(
            value & 0xffU);
    bytes[offset + 1] =
        static_cast<std::byte>(
            (value >> 8U) & 0xffU);
}

void write_u32(
    std::span<std::byte> bytes,
    std::size_t offset,
    std::uint32_t value)
{
    bytes[offset] =
        static_cast<std::byte>(
            value & 0xffU);
    bytes[offset + 1] =
        static_cast<std::byte>(
            (value >> 8U) & 0xffU);
    bytes[offset + 2] =
        static_cast<std::byte>(
            (value >> 16U) & 0xffU);
    bytes[offset + 3] =
        static_cast<std::byte>(
            (value >> 24U) & 0xffU);
}

template <std::size_t N>
[[nodiscard]] AudioSourcePacket packet_for(
    AudioFormat audio_format,
    const std::array<std::byte, N> &payload)
{
    AudioSourcePacket packet;
    packet.native_format = audio_format;
    packet.frame_count = 1;
    packet.pool_slot = 0;
    packet.payload_bytes =
        static_cast<std::uint32_t>(
            payload.size());
    return packet;
}

void test_pcm16(TestContext &test)
{
    std::array<std::byte, 4> bytes{};
    write_u16(
        bytes,
        0,
        static_cast<std::uint16_t>(
            static_cast<std::int16_t>(-32768)));
    write_u16(
        bytes,
        2,
        static_cast<std::uint16_t>(
            static_cast<std::int16_t>(32767)));

    auto packet =
        packet_for(
            format(
                AudioSampleType::Pcm16,
                16,
                16),
            bytes);

    std::array<float, 2> output{};
    AudioNormalizationStats stats;

    const auto status =
        normalize_audio_packet_to_float32(
            packet,
            bytes,
            output,
            &stats);

    test.expect(
        status == AudioNormalizeStatus::Ok,
        "PCM16 packet normalizes");
    test.expect(
        output[0] == -1.0F,
        "PCM16 minimum maps exactly to -1");
    test.expect(
        std::abs(
            output[1] -
            (32767.0F / 32768.0F)) <
            1.0e-7F,
        "PCM16 positive maximum preserves asymmetric full scale");
    test.expect(
        stats.frames_written == 1 &&
        stats.samples_written == 2,
        "PCM16 normalization reports exact work");
}

void test_pcm24_in_32_left_aligned(TestContext &test)
{
    std::array<std::byte, 8> bytes{};

    // WAVEFORMATEXTENSIBLE valid PCM bits are left-aligned in the container.
    // +0x7fffff therefore appears as 0x7fffff00, and -0x800000 as 0x80000000.
    write_u32(
        bytes,
        0,
        0x80000000U);
    write_u32(
        bytes,
        4,
        0x7fffff00U);

    auto packet =
        packet_for(
            format(
                AudioSampleType::Pcm24In32,
                32,
                24),
            bytes);

    std::array<float, 2> output{};

    const auto status =
        normalize_audio_packet_to_float32(
            packet,
            bytes,
            output);

    test.expect(
        status == AudioNormalizeStatus::Ok,
        "PCM24-in-32 packet normalizes");
    test.expect(
        output[0] == -1.0F,
        "Left-aligned PCM24 minimum maps to -1");
    test.expect(
        std::abs(
            output[1] -
            (2147483392.0F /
             2147483648.0F)) <
            1.0e-6F,
        "Left-aligned PCM24 positive maximum retains scale");
}

void test_pcm32(TestContext &test)
{
    std::array<std::byte, 8> bytes{};
    write_u32(bytes, 0, 0x40000000U);
    write_u32(bytes, 4, 0xc0000000U);

    auto packet =
        packet_for(
            format(
                AudioSampleType::Pcm32,
                32,
                32),
            bytes);

    std::array<float, 2> output{};

    const auto status =
        normalize_audio_packet_to_float32(
            packet,
            bytes,
            output);

    test.expect(
        status == AudioNormalizeStatus::Ok,
        "PCM32 packet normalizes");
    test.expect(
        output[0] == 0.5F &&
        output[1] == -0.5F,
        "PCM32 signed full-scale normalization is exact");
}

void test_float_and_non_finite(TestContext &test)
{
    std::array<std::byte, 8> bytes{};
    write_u32(
        bytes,
        0,
        std::bit_cast<std::uint32_t>(0.25F));
    write_u32(
        bytes,
        4,
        std::bit_cast<std::uint32_t>(
            std::numeric_limits<float>::infinity()));

    auto packet =
        packet_for(
            format(
                AudioSampleType::Float32,
                32,
                32),
            bytes);

    std::array<float, 2> output{};
    AudioNormalizationStats stats;

    const auto status =
        normalize_audio_packet_to_float32(
            packet,
            bytes,
            output,
            &stats);

    test.expect(
        status == AudioNormalizeStatus::Ok,
        "Float32 packet normalizes");
    test.expect(
        output[0] == 0.25F,
        "Finite Float32 payload is preserved");
    test.expect(
        output[1] == 0.0F &&
        stats.non_finite_float_samples == 1,
        "Non-finite Float32 cannot poison mixer state");
}

void test_silent_packet(TestContext &test)
{
    AudioSourcePacket packet;
    packet.native_format =
        format(
            AudioSampleType::Float32,
            32,
            32);
    packet.frame_count = 1;
    packet.flags =
        AudioPacketFlag::Silent;
    packet.pool_slot =
        kInvalidAudioPoolSlot;
    packet.payload_bytes = 0;

    std::array<float, 2> output{
        0.75F,
        -0.25F};

    const auto status =
        normalize_audio_packet_to_float32(
            packet,
            {},
            output);

    test.expect(
        status == AudioNormalizeStatus::Ok,
        "Allocation-free silent packet normalizes");
    test.expect(
        output[0] == 0.0F &&
        output[1] == 0.0F,
        "Silent packet produces exact timeline silence");
}

void test_fail_closed_shapes(TestContext &test)
{
    std::array<std::byte, 4> bytes{};
    auto packet =
        packet_for(
            format(
                AudioSampleType::Pcm16,
                16,
                16),
            bytes);

    std::array<float, 1> too_small{};

    test.expect(
        normalize_audio_packet_to_float32(
            packet,
            bytes,
            too_small) ==
            AudioNormalizeStatus::OutputTooSmall,
        "Normalizer rejects undersized output without partial write");

    auto malformed = packet;
    malformed.payload_bytes = 2;

    std::array<float, 2> output{};
    test.expect(
        normalize_audio_packet_to_float32(
            malformed,
            std::span<const std::byte>(
                bytes.data(),
                2),
            output) ==
            AudioNormalizeStatus::InvalidPacket,
        "Malformed packet metadata fails closed");
}

} // namespace

int main()
{
    TestContext test;

    test_pcm16(test);
    test_pcm24_in_32_left_aligned(test);
    test_pcm32(test);
    test_float_and_non_finite(test);
    test_silent_packet(test);
    test_fail_closed_shapes(test);

    if (test.failures != 0) {
        std::cerr
            << test.failures << " of "
            << test.checks
            << " checks failed\n";
        return 1;
    }

    std::cout
        << "PASS: " << test.checks
        << " audio sample normalizer checks\n";
    return 0;
}
