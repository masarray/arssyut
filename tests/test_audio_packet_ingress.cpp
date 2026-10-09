#include "core/audio/audio_packet_ingress.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#ifdef _WIN32
#include "platform/windows/audio/wasapi_audio_utils.hpp"
#endif

using namespace arssyut::core::audio;

namespace {
struct Test {
    int checks = 0;
    int failures = 0;
    void check(bool pass, const char *why) {
        ++checks;
        if (!pass) { ++failures; std::cerr << "FAIL: " << why << '\n'; }
    }
};

constexpr std::int64_t kZero = 100'000'000;
constexpr AudioFormat kFloatStereo{
    .sample_rate = 48'000,
    .sample_type = AudioSampleType::Float32,
    .channels = 2,
    .container_bits_per_sample = 32,
    .valid_bits_per_sample = 32,
    .channel_mask = kStereoChannelMask,
    .block_align = 8
};
constexpr AudioFormat kPcm16Mono44{
    .sample_rate = 44'100,
    .sample_type = AudioSampleType::Pcm16,
    .channels = 1,
    .container_bits_per_sample = 16,
    .valid_bits_per_sample = 16,
    .channel_mask = 0,
    .block_align = 2
};
struct FakeResampler final : IAudioResampler {
    AudioResamplerConfig cfg{};
    bool configured = false;
    bool fail = false;
    std::uint32_t drain_frames = 0;
    int resets = 0;
    int calls = 0;
    double last_ppm = 0.0;

    AudioResampleStatus configure(AudioResamplerConfig x) noexcept override {
        configured = x.valid();
        cfg = x;
        return configured ? AudioResampleStatus::Ok :
            AudioResampleStatus::InvalidArgument;
    }
    void reset() noexcept override { ++resets; }
    AudioResampleResult process(
        std::span<const float> in, std::uint32_t frames,
        std::span<float> out, double ppm) noexcept override {
        ++calls;
        last_ppm = ppm;
        AudioResampleResult result;
        if (fail) {
            result.status = AudioResampleStatus::Failed;
            return result;
        }
        if (!configured || in.size() !=
                static_cast<std::size_t>(frames) * cfg.input_channels) {
            result.status = AudioResampleStatus::InvalidArgument;
            return result;
        }
        const auto produced = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(frames) * 48'000U /
            cfg.input_sample_rate);
        if (out.size() < static_cast<std::size_t>(produced) * 2U) {
            result.status = AudioResampleStatus::OutputFull;
            return result;
        }
        for (std::uint32_t f=0; f<produced; ++f) {
            const auto from = static_cast<std::size_t>(
                static_cast<std::uint64_t>(f)*cfg.input_sample_rate/48'000U);
            const auto left = in[from*cfg.input_channels];
            const auto right = cfg.input_channels == 1
                ? left : in[from*cfg.input_channels + 1];
            out[2*f] = left;
            out[2*f+1] = right;
        }
        result.status = AudioResampleStatus::Ok;
        result.input_frames_consumed = frames;
        result.output_frames_produced = produced;
        return result;
    }
    std::uint64_t current_delay_100ns() const noexcept override {
        return 0;
    }
    AudioResamplerRateState current_rate_state() const noexcept override {
        return {.effective_output_per_input_ratio =
                    static_cast<double>(48'000U)/cfg.input_sample_rate,
                .phase_remainder_denominator=1};
    }
    std::uint32_t maximum_drain_frames() const noexcept override {
        return 256;
    }
    AudioResampleDrainResult drain(std::span<float> out) noexcept override {
        AudioResampleDrainResult result;
        if (out.size() < static_cast<std::size_t>(drain_frames)*2U) {
            result.status=AudioResampleStatus::OutputFull;
            return result;
        }
        for (std::uint32_t i=0;i<drain_frames*2U;++i)
            out[i] = 0.125F;
        result.status=AudioResampleStatus::Ok;
        result.output_frames_produced=drain_frames;
        result.complete=true;
        drain_frames=0;
        return result;
    }
};
template <class T, std::size_t N>
std::span<const std::byte> bytes(const std::array<T,N> &data) {
    return {reinterpret_cast<const std::byte*>(data.data()),
            sizeof(T)*N};
}
AudioSourcePacket packet(AudioSourceId source,
                         AudioFormat format,
                         std::uint32_t frames,
                         std::int64_t time,
                         std::uint64_t device_frames=0) {
    AudioSourcePacket result;
    result.source=source;
    result.native_format=format;
    result.frame_count=frames;
    result.pool_slot=1;
    result.payload_bytes=static_cast<std::uint32_t>(
        format.bytes_for_frames(frames));
    result.timing.quality=AudioTimestampQuality::DeviceQpcTrusted;
    result.timing.packet_start_qpc_100ns=time;
    result.timing.device_frame_position=device_frames;
    return result;
}

void test_mic48_ingress_to_mix(Test &t) {
    auto assembler=std::make_unique<CanonicalProgramAssembler>();
    auto input=std::make_unique<CanonicalPacketIngress>();
    FakeResampler src;
    assembler->reset(kZero,true,false);
    t.check(input->configure(AudioSourceId::Microphone,kFloatStereo,
                             kZero,src),
            "capture config initializes existing SRC with 48k stereo");
    std::array<float,1024*2> samples{};
    samples.fill(0.25F);
    auto in=packet(AudioSourceId::Microphone,kFloatStereo,1024,kZero);
    const auto processed=input->process_packet(in,bytes(samples),*assembler);
    t.check(processed.status==PacketIngressStatus::Applied &&
            processed.native_frames_consumed==1024 &&
            processed.canonical_frames_emitted==1024 &&
            processed.first_canonical_frame==0 && src.calls==1,
            "real native float32 payload normalized mapped and SRC offered");
    t.check(assembler->close_one_due(
            kZero+static_cast<std::int64_t>(
                frames_to_ticks_floor(1024,48'000))),
            "first program block closes at recorder-owned 48k deadline");
    AudioProgramBlock out;
    std::uint64_t first=99;
    t.check(assembler->try_take(out,first) && first==0 &&
            out.audio.source_presence_mask==1 &&
            out.audio.samples[0]==0.25F &&
            out.audio.samples[2047]==0.25F,
            "WASAPI float32 samples reach canonical program after mix");

    // Stop-drain uses same accepted backend, not an invented source clock.
    src.drain_frames=100;
    const auto drained=input->drain_one(*assembler);
    t.check(drained.status==PacketIngressStatus::Applied &&
            drained.first_canonical_frame==1024 &&
            drained.canonical_frames_emitted==100,
            "SRC stop-drain keeps remaining frames in next interval");
    t.check(assembler->close_one_due(
            kZero+static_cast<std::int64_t>(
                frames_to_ticks_floor(2048,48'000))) &&
            assembler->try_take(out,first) && first==1024 &&
            out.audio.samples[0]==0.125F &&
            out.audio.samples[200]==0.0F,
            "drained SRC filter tail is not lost or time shifted");
}

void test_pcm16_mono_44100(Test &t) {
    auto assembler=std::make_unique<CanonicalProgramAssembler>();
    auto input=std::make_unique<CanonicalPacketIngress>();
    FakeResampler src;
    assembler->reset(kZero,false,true);
    t.check(input->configure(AudioSourceId::SystemAudio,kPcm16Mono44,
                             kZero,src),
            "mono 44.1 kHz endpoint accepted for canonical stereo SRC");
    std::array<std::int16_t,441> mono{};
    mono.fill(16384);
    auto p=packet(AudioSourceId::SystemAudio,kPcm16Mono44,441,kZero);
    const auto a=input->process_packet(p,bytes(mono),*assembler);
    t.check(a.status==PacketIngressStatus::Applied &&
            a.canonical_frames_emitted==480 &&
            a.first_canonical_frame==0 &&
            src.cfg.input_channels==1 &&
            src.cfg.input_sample_rate==44'100,
            "actual PCM16 normalization 44.1 to 48 kHz producer");
    auto p2=packet(AudioSourceId::SystemAudio,kPcm16Mono44,441,
        kZero+100'000,441);
    const auto b=input->process_packet(p2,bytes(mono),*assembler);
    t.check(b.status==PacketIngressStatus::Applied &&
            b.canonical_frames_emitted==480 &&
            b.first_canonical_frame==480,
            "repeated 44.1 packets place contiguous 48k sample frames");
    t.check(assembler->close_one_due(
            kZero+static_cast<std::int64_t>(
                frames_to_ticks_floor(1024,48'000))),
            "44.1 input closes into canonical program interval");
    AudioProgramBlock out;
    std::uint64_t n=500;
    t.check(assembler->try_take(out,n) && n==0 &&
            out.audio.source_presence_mask==2 &&
            out.audio.samples[0]==0.5F &&
            out.audio.samples[959*2+1]==0.5F &&
            out.audio.samples[960*2]==0.0F,
            "PCM16 44.1k source produces stereo 48k with correct silent tail");
}

void test_invalid_and_pre_zero(Test &t) {
    auto assembler=std::make_unique<CanonicalProgramAssembler>();
    auto input=std::make_unique<CanonicalPacketIngress>();
    FakeResampler src;
    assembler->reset(kZero,true,false);
    t.check(input->configure(AudioSourceId::Microphone,kFloatStereo,
                             kZero,src),
            "processor configured");
    std::array<float,2048> samples{};
    samples.fill(0.125F);
    auto p=packet(AudioSourceId::Microphone,kFloatStereo,1024,
                  kZero-300'000);
    const auto before=input->process_packet(p,bytes(samples),*assembler);
    t.check(before.status==PacketIngressStatus::BeforeMediaZero &&
            src.calls==0,
            "fully pre-zero packet must not reach resampler or program");

    p=packet(AudioSourceId::Microphone,kFloatStereo,1024,
             kZero-100'000);
    const auto overlap=input->process_packet(p,bytes(samples),*assembler);
    t.check(overlap.status==PacketIngressStatus::Applied &&
            overlap.native_frames_consumed>0 &&
            overlap.native_frames_consumed<1024 &&
            overlap.first_canonical_frame==0,
            "overlap before media zero trimmed in native frame domain");
    p=packet(AudioSourceId::SystemAudio,kFloatStereo,1024,kZero);
    const auto mismatched=input->process_packet(p,bytes(samples),*assembler);
    t.check(mismatched.status==PacketIngressStatus::Invalid &&
            src.calls==1,
            "wrong WASAPI source identity rejected before SRC");

    std::array<float,2048> normal{};
    p=packet(AudioSourceId::Microphone,kFloatStereo,1024,kZero+400'000);
    p.frame_count=1025;
    const auto oversized=input->process_packet(p,bytes(normal),*assembler);
    t.check(oversized.status==PacketIngressStatus::Invalid ||
            oversized.status==PacketIngressStatus::TooLarge,
            "oversized/malformed native packet cannot overrun fixed scratch");
    src.fail=true;
    p=packet(AudioSourceId::Microphone,kFloatStereo,1024,kZero+500'000);
    const auto failing=input->process_packet(p,bytes(samples),*assembler);
    t.check(failing.status==PacketIngressStatus::ResamplerFailure,
            "failed backend cannot be mistaken for silence");
    t.check(!input->ready(), "failed SRC consumes state and cannot be retried");
}

void test_detect_trusted_qpc_gap(Test &t) {
    auto assembler=std::make_unique<CanonicalProgramAssembler>();
    auto input=std::make_unique<CanonicalPacketIngress>();
    FakeResampler src;
    assembler->reset(kZero,true,false);
    t.check(input->configure(AudioSourceId::Microphone,kFloatStereo,kZero,src),
            "gap test selected source");
    std::array<float,2048> samples{};
    samples.fill(0.2F);
    const auto first=input->process_packet(packet(
        AudioSourceId::Microphone,kFloatStereo,1024,kZero),
        bytes(samples),*assembler);
    t.check(first.status==PacketIngressStatus::Applied,
            "first trusted packet starts at media zero");
    t.check(assembler->close_one_due(
        kZero+static_cast<std::int64_t>(
            frames_to_ticks_floor(1024,48'000))),
        "initial stage consumed before QPC gap");
    AudioProgramBlock scratch;
    std::uint64_t first_frame=0;
    t.check(assembler->try_take(scratch,first_frame),
            "closed pre-gap output taken");
    const auto jumped=input->process_packet(packet(
        AudioSourceId::Microphone,kFloatStereo,1024,
        kZero+1'000'000,1024), bytes(samples),*assembler);
    t.check(jumped.status==PacketIngressStatus::Applied &&
            jumped.first_canonical_frame==4'800 &&
            jumped.discontinuity && src.resets==1,
            "100ms packet gap reanchors canonical QPC and resets SRC");
    for (int i=1;i<5;++i)
        t.check(assembler->close_one_due(
            kZero+static_cast<std::int64_t>(
                frames_to_ticks_floor((i+1)*1024ULL,48'000))),
            "missing intervals close as silence without retiming");
    t.check(assembler->try_take(scratch,first_frame) &&
            scratch.audio.source_presence_mask==0,
            "first missing interval remains silence");
    t.check(assembler->close_one_due(
        kZero+static_cast<std::int64_t>(
            frames_to_ticks_floor(6*1024ULL,48'000))),
        "after-gap interval closes at original clock tick");
    bool saw_gap = false;
    while (assembler->try_take(scratch,first_frame)) {
        if (first_frame == 4096 &&
            scratch.audio.discontinuity_mask ==
                source_presence_bit(AudioSourceId::Microphone))
            saw_gap = true;
    }
    t.check(saw_gap, "source discontinuity survives future staging");
}


#ifdef _WIN32
void test_real_wasapi_lease_pipeline(Test &t)
{
    using arssyut::windows::WasapiPacketHandoff;
    using arssyut::windows::WasapiPacketLease;
    using arssyut::windows::WasapiPacketPublishResult;

    // This is the real Windows handoff/pool/lease implementation, not a
    // synthetic fake lease. Only the actual device and SRC DSP are injected.
    auto handoff = std::make_shared<WasapiPacketHandoff>(
        8, 8, 4'096 * sizeof(float) * 2);
    t.check(handoff->valid(), "real Windows WASAPI packet handoff allocated");
    auto assembler = std::make_unique<CanonicalProgramAssembler>();
    auto ingress = std::make_unique<CanonicalPacketIngress>();
    FakeResampler src;
    assembler->reset(kZero, true, false);
    t.check(ingress->configure(
        AudioSourceId::Microphone, kFloatStereo, kZero, src),
        "lease ingress config uses already accepted resampler API");

    std::array<float, 2'400 * 2> samples{};
    samples.fill(0.375F);
    auto meta = packet(AudioSourceId::Microphone,
        kFloatStereo, 2'400, kZero);
    t.check(handoff->publish(meta, bytes(samples)) ==
        WasapiPacketPublishResult::Published,
        "2400-frame native packet published into REAL fixed WASAPI pool");
    AudioSourcePacket popped{};
    t.check(handoff->try_pop(popped),
        "real WASAPI packet metadata popped");
    WasapiPacketLease lease(handoff, popped);
    const auto result = ingress->process_lease(lease, *assembler);
    t.check(result.status == PacketIngressStatus::Applied &&
            result.native_frames_consumed == 2'400 &&
            result.canonical_frames_emitted == 2'400 &&
            src.calls == 3 &&
            !lease.valid() &&
            handoff->outstanding_payload_leases() == 0,
        "single real lease split as 1024+1024+352; payload released exactly once");
    t.check(assembler->close_one_due(
        kZero+static_cast<std::int64_t>(
            frames_to_ticks_floor(1024,48'000))),
        "lease-fed first program block closes");
    AudioProgramBlock program;
    std::uint64_t first=99;
    t.check(assembler->try_take(program,first) && first == 0 &&
            program.audio.samples[0] == 0.375F &&
            program.audio.samples[2047] == 0.375F,
        "real WASAPI lease bytes reached actual canonical mixer");

    struct SourceView {
        std::shared_ptr<WasapiPacketHandoff> handoff;
        [[nodiscard]] bool try_pop(WasapiPacketLease &out) noexcept {
            AudioSourcePacket item;
            if (!handoff->try_pop(item))
                return false;
            out = WasapiPacketLease(handoff, item);
            return true;
        }
    } source{handoff};

    // Actual WASAPI silence has no backing bytes, but still has meaningful
    // frames that must advance the 48k canonical media timeline.
    auto silent = packet(AudioSourceId::Microphone,
        kFloatStereo, 1'024,
        kZero+static_cast<std::int64_t>(
            frames_to_ticks_floor(2'400,48'000)),2'400);
    silent.flags = AudioPacketFlag::Silent;
    t.check(handoff->publish(silent, {}) ==
        WasapiPacketPublishResult::Published,
        "real handoff accepts allocation-free silence packet");
    auto pumped=ingress->pump_available(source,*assembler,1);
    t.check(pumped.status==PacketIngressStatus::Applied &&
            pumped.packets_popped==1 &&
            pumped.canonical_frames_emitted==1'024 &&
            handoff->outstanding_payload_leases()==0 &&
            src.calls == 4,
        "bounded pump consumes actual silent lease and releases it");
    pumped=ingress->pump_available(source,*assembler,2);
    t.check(pumped.packets_popped==0 && pumped.source_empty,
        "nonblocking WASAPI pump returns immediately on no data");

    auto too_big = packet(AudioSourceId::Microphone,
        kFloatStereo, 8*1'024+1, kZero);
    // A large allocation-free silence lease must fail closed in bounded
    // ingress but still return the handoff to its owning generation.
    too_big.flags=AudioPacketFlag::Silent;
    t.check(handoff->publish(too_big,{}) ==
        WasapiPacketPublishResult::Published,
        "larger-than-budget silent lease can be represented by handoff");
    t.check(handoff->try_pop(popped),
        "overbudget lease popped");
    WasapiPacketLease large(handoff,popped);
    const auto rejected=ingress->process_lease(large,*assembler);
    t.check(rejected.status==PacketIngressStatus::TooLarge &&
            !large.valid() &&
            handoff->outstanding_payload_leases()==0,
        "overbudget lease rejected without leaking pool generation");
}
#endif

} // namespace

int main() {
    Test t;
    test_mic48_ingress_to_mix(t);
    test_pcm16_mono_44100(t);
    test_invalid_and_pre_zero(t);
    test_detect_trusted_qpc_gap(t);
#ifdef _WIN32
    test_real_wasapi_lease_pipeline(t);
#endif
    if (t.failures) {
        std::cerr << "FAIL: " << t.failures << '/' << t.checks
                  << " packet ingress checks\n";
        return 1;
    }
    std::cout << "PASS: " << t.checks << " packet ingress checks\n";
    return 0;
}
