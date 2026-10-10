#include "core/audio/audio_input_peak.hpp"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>

using namespace arssyut::core::audio;
int main() {
    constexpr std::uint32_t n = 300;
    std::array<std::byte, n*2*4> payload{};
    auto write = [&](std::uint32_t frame, std::uint32_t ch, float sample) {
        const auto bits = std::bit_cast<std::uint32_t>(sample);
        for (std::uint32_t b = 0; b < 4; ++b)
            payload[(frame*2+ch)*4+b] =
                static_cast<std::byte>((bits >> (8*b)) & 255U);
    };
    write(0, 0, -.70f);
    write(299, 1, .25f);
    AudioSourcePacket p{};
    p.native_format = {48000,AudioSampleType::Float32,2,32,32,kStereoChannelMask,8};
    p.frame_count = n; p.pool_slot = 0; p.payload_bytes = payload.size();
    AudioInputPeak peak{};
    int fails = 0;
    const auto check = [&](bool okay) { if (!okay) ++fails; };
    check(accumulate_input_peak(p,payload,peak));
    check(peak.channels==2 && peak.left>.69f && peak.left<.71f &&
          peak.right>.24f && peak.right<.26f);
    auto silent=p;
    silent.flags=AudioPacketFlag::Silent;
    silent.pool_slot=kInvalidAudioPoolSlot;
    silent.payload_bytes=0;
    AudioInputPeak zero{};
    check(accumulate_input_peak(silent,{},zero) &&
          zero.channels==2 && zero.left==0 && zero.right==0);
    ++p.payload_bytes;
    check(!accumulate_input_peak(p,payload,peak));
    std::cout << "stereo input meter " << (fails?"FAIL":"PASS") << '\n';
    return fails ? 1 : 0;
}
