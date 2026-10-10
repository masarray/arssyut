#include "app/recorded_input_sounds.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

using arssyut::app::audio::RecordedInputSounds;
using arssyut::app::audio::RecordedCue;
using arssyut::core::audio::AudioProgramBlock;
int main() {
    int failures=0;
    auto expect=[&](bool ok,const char* name) {
        if(!ok) {++failures;std::cerr<<"FAIL: "<<name<<'\n';}
    };
    constexpr std::int64_t zero=12'000'000;
    RecordedInputSounds fx;
    fx.reset(zero);
    fx.push(RecordedCue::LeftClick,zero+100'000); // event at 480 frames
    AudioProgramBlock block{};
    block.audio.frame_count=1024;
    for(std::size_t i=0;i<2048;i+=2) {
        block.audio.samples[i]=.10f;block.audio.samples[i+1]=-.10f;
    }
    fx.apply(block,0);
    expect(block.audio.samples[2*479]==.10f,"no signal before event");
    bool changed=false,stereo=true;
    for(std::size_t i=480;i<1024;++i) {
        auto left=block.audio.samples[i*2],right=block.audio.samples[i*2+1];
        changed|=std::fabs(left-.10f)>.000001f;
        stereo&=std::fabs((left-right)-.20f)<.000002f;
    }
    expect(changed,"left click audible");
    expect(stereo,"preexisting independent L/R preserved");

    fx.reset(zero);
    AudioProgramBlock right{},key{};
    right.audio.frame_count=key.audio.frame_count=1024;
    fx.push(RecordedCue::RightClick,zero);
    fx.apply(right,0);
    fx.reset(zero);
    fx.push(RecordedCue::Keycap,zero);
    fx.apply(key,0);
    expect(!std::equal(right.audio.samples.begin(),
        right.audio.samples.begin()+2048,key.audio.samples.begin()),
        "mouse and keycap timbre distinct");

    fx.reset(zero);
    for(std::size_t i=0;i<RecordedInputSounds::kVoices+4;i++)
        fx.push(RecordedCue::Keycap,zero);
    expect(fx.dropped_events()==4,"polyphony strictly bounded");
    fx.reset(zero);
    AudioProgramBlock missed{};
    missed.audio.frame_count=1024;
    fx.apply(missed,0);
    fx.push(RecordedCue::LeftClick,zero);
    AudioProgramBlock next{};
    next.audio.frame_count=1024;
    fx.apply(next,1024);
    expect(std::any_of(next.audio.samples.begin(),
        next.audio.samples.begin()+2048,
        [](float s){return std::fabs(s)>.000001f;}),
        "late event starts at next unwritten sample");

    fx.reset(zero);
    fx.push(RecordedCue::Keycap,zero-1);
    AudioProgramBlock preroll{};
    preroll.audio.frame_count=1024;
    fx.apply(preroll,0);
    expect(std::all_of(preroll.audio.samples.begin(),
        preroll.audio.samples.begin()+2048,
        [](float s){return s==0.0f;}),"preroll event excluded");

    std::cout<<(failures?"FAIL":"PASS")<<" recorded input cues\n";
    return failures?1:0;
}
