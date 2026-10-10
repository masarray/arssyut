#pragma once

// Recorded presentation sound cues. All input timestamps use RecorderSession's
// monotonic QPC media zero; existing 48 kHz float32 stereo program is the only
// output. No extra hook, sound device, writer, resampler or heap allocation.
#include "core/audio/audio_program_mixer.hpp"
#include "core/audio/audio_time.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace arssyut::app::audio {
enum class RecordedCue : std::uint8_t { LeftClick, RightClick, Keycap };

class RecordedInputSounds final {
public:
    static constexpr std::uint32_t kRate = 48'000;
    static constexpr std::size_t kVoices = 24;
    static constexpr std::size_t kWaveFrames = 2'400;
    RecordedInputSounds() noexcept { generate(); }

    void reset(std::int64_t zero) noexcept {
        media_zero_ = zero;
        unwritten_frame_ = 0;
        voices_.fill({});
        dropped_ = 0;
    }
    void push(RecordedCue cue, std::int64_t timestamp) noexcept {
        if (media_zero_ <= 0 || timestamp < media_zero_)
            return;
        const auto position = core::audio::ticks_to_frames_floor(
            static_cast<std::uint64_t>(timestamp-media_zero_), kRate);
        for (auto &voice:voices_) {
            if (!voice.active) {
                voice={true,cue,std::max(position,unwritten_frame_)};
                return;
            }
        }
        ++dropped_;
    }

    void apply(core::audio::AudioProgramBlock &block,
               std::uint64_t first_frame) noexcept {
        const auto frames=block.audio.frame_count;
        if (frames==0 || frames>1024) return;
        const auto end_block=first_frame+frames;
        for (auto &voice:voices_) {
            if (!voice.active) continue;
            const auto voice_end=voice.first_frame+kWaveFrames;
            if (voice_end<=first_frame) {
                voice.active=false;
                continue;
            }
            const auto from=std::max(first_frame,voice.first_frame);
            const auto end=std::min(end_block,voice_end);
            if (from<end) {
                const auto &wave=waves_[static_cast<std::size_t>(voice.cue)];
                for(auto frame=from;frame<end;++frame) {
                    const auto offset=static_cast<std::size_t>(frame-first_frame)*2U;
                    const auto amount=wave_[index(frame,voice.first_frame,wave)];
                    for(std::size_t c=0;c<2;++c) {
                        const auto original=block.audio.samples[offset+c];
                        // Preserve original microphone/system samples; constrain
                        // only the added transient to remaining sample headroom.
                        const auto safe=std::clamp(amount,
                            std::min(0.0f,-1.0f-original),
                            std::max(0.0f,1.0f-original));
                        block.audio.samples[offset+c]=original+safe;
                    }
                }
            }
            if(voice_end<=end_block) voice.active=false;
        }
        unwritten_frame_=std::max(unwritten_frame_,end_block);
    }
    [[nodiscard]] std::uint64_t dropped_events() const noexcept { return dropped_; }

private:
    struct Voice {
        bool active=false;
        RecordedCue cue=RecordedCue::LeftClick;
        std::uint64_t first_frame=0;
    };
    static std::size_t index(std::uint64_t frame,std::uint64_t beginning,
                             const std::array<float,kWaveFrames>&) noexcept {
        return static_cast<std::size_t>(frame-beginning);
    }
    void generate() noexcept {
        constexpr double pi=3.14159265358979323846;
        for(std::size_t kind=0;kind<3;++kind) {
            const double hz=kind==0?1870.0:kind==1?530.0:1330.0;
            const double decay=kind==0?120.0:kind==1?210.0:280.0;
            std::uint32_t rand=0x5AEFF057U+static_cast<std::uint32_t>(kind)*191U;
            for(std::size_t i=0;i<kWaveFrames;++i) {
                rand^=rand<<13U;rand^=rand>>17U;rand^=rand<<5U;
                const double noise=static_cast<double>(rand&0xffffU)/32767.5-1.0;
                const double t=static_cast<double>(i);
                double effect=std::exp(-t/decay) *
                    (0.067*noise+0.052*std::sin(2*pi*hz*t/kRate));
                // Keycap's second short impact is its mechanical "clack".
                if(kind==2 && i>=330) {
                    const double tail=static_cast<double>(i-330);
                    effect+=std::exp(-tail/105.0) *
                        (0.035*noise+0.025*std::sin(2*pi*950.0*tail/kRate));
                }
                waves_[kind][i]=static_cast<float>(effect);
            }
        }
    }
    std::array<std::array<float,kWaveFrames>,3> waves_{};
    std::array<Voice,kVoices> voices_{};
    std::int64_t media_zero_=0;
    std::uint64_t unwritten_frame_=0;
    std::uint64_t dropped_=0;
};
} // namespace arssyut::app::audio
