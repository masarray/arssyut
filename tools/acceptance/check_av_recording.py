#!/usr/bin/env python3
"""Real Windows A/V release gate; requires Python 3, FFprobe, FFmpeg."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys

def probe(path, selector=None):
    cmd=["ffprobe","-v","error"]
    if selector:
        cmd+=["-select_streams",selector,"-show_packets","-show_entries","packet=pts_time"]
    else:
        cmd+=["-show_streams","-show_format"]
    return json.loads(subprocess.check_output(cmd+["-of","json",str(path)],text=True))

def timestamps(path,selector,expected):
    stamps=[float(p["pts_time"]) for p in probe(path,selector)["packets"]]
    if len(stamps)<3:
        raise ValueError("No packet timestamps for "+selector)
    gaps=[b-a for a,b in zip(stamps,stamps[1:])]
    return {"count":len(stamps),"bad_steps":sum(not (0 < step <= expected*1.6) for step in gaps),
            "max_step_ms":round(max(gaps)*1000,5)}

def decoded_audio(path):
    cmd=["ffmpeg","-nostdin","-hide_banner","-loglevel","error","-i",str(path),
         "-map","0:a:0","-ac","2","-ar","48000","-f","f32le","pipe:1"]
    p=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    n=0; last=None; clipped=0; nonfinite=0; windows={}
    assert p.stdout is not None
    while True:
        chunk=p.stdout.read(65536)
        if not chunk: break
        if len(chunk)%8: raise ValueError("Incomplete stereo frame")
        for l,r in struct.iter_unpack("<ff",chunk):
            if not (math.isfinite(l) and math.isfinite(r)):
                nonfinite+=1; l=r=0.
            clipped+=int(abs(l)>=0.999 or abs(r)>=0.999)
            stats=windows.setdefault(n//240000,[0.,0,0.,0,0.,0])
            stats[0]+=(l*l+r*r)*0.5;stats[1]+=1
            if last is not None:
                delta=abs(l-last[0])+abs(r-last[1])
                if n%1024==0:
                    stats[4]+=delta;stats[5]+=1
                else:
                    stats[2]+=delta;stats[3]+=1
            last=(l,r);n+=1
    p.stdout.close()
    err=p.stderr.read().decode("utf-8","replace")
    if p.wait(): raise ValueError("FFmpeg AAC decode failed: "+err[-400:])
    segments=[]
    for key,s in sorted(windows.items()):
        if s[1]<96000:continue
        rms=math.sqrt(s[0]/s[1]); inner=s[2]/s[3] if s[3] else 0
        boundary=s[4]/s[5] if s[5] else 0
        segments.append({"start_s":key*5,"audible":rms>=0.006,"rms":round(rms,6),
                         "boundary_count":s[5],
                         "boundary_ratio":round(boundary/inner,4) if inner>0 else None})
    return {"frames":n,"duration_s":n/48000,"clipped":clipped,
            "nonfinite":nonfinite,"segments":segments}

def digest(path):
    h=hashlib.sha256()
    with path.open("rb") as f:
        for part in iter(lambda:f.read(1048576),b""):h.update(part)
    return h.hexdigest()

def evaluate(media,diagnostics):
    d=json.loads(diagnostics.read_text(encoding="utf-8"))
    streams=probe(media)["streams"]
    v=next((s for s in streams if s.get("codec_type")=="video"),{})
    a=next((s for s in streams if s.get("codec_type")=="audio"),{})
    errors=[]
    def check(ok,message):
        if not ok:errors.append(message)
    required=["schema","result","status_code","audio_writer_enabled",
              "audio_stale_canonical_frames","audio_duplicate_canonical_frames",
              "audio_staging_gaps","audio_staging_overflows","audio_dropped_program_blocks",
              "encoder_backpressure","video_skipped","capture_busy_drops"]
    for source in ("system","microphone"):
        if d.get("audio_"+source+"_requested"):
            required += ["audio_"+source+"_"+k for k in (
                "packets_captured","packets_published","packets_dropped",
                "pool_exhaustions","ring_overflows","timestamp_errors")]
    for key in required:check(key in d,"Missing hardware telemetry "+key)
    check(d.get("schema")=="arssyut-diagnostics-v1","Diagnostics schema mismatch")
    check(d.get("result")=="ready" and d.get("status_code")==0,"Recording failed")
    check(v.get("codec_name")=="h264" and a.get("codec_name")=="aac","Not H264+AAC")
    check(int(a.get("sample_rate",0))==48000 and int(a.get("channels",0))==2,
          "Not 48kHz stereo")
    check(int(v.get("width",0))==d.get("width") and
          int(v.get("height",0))==d.get("height"),"Video size mismatch")
    vd,ad=float(v.get("duration",0)),float(a.get("duration",0))
    check(vd>=10 and abs(vd-ad)<0.12,"A/V duration mismatch")
    check(d.get("audio_writer_enabled") is True,"Audio writer disabled")
    for source in ("system","microphone"):
        if d.get("audio_"+source+"_requested"):
            prefix="audio_"+source+"_"
            captured=d.get(prefix+"packets_captured",0)
            check(captured>0 and d.get(prefix+"packets_published")==captured,
                  source+" packet loss")
            for metric in ("packets_dropped","pool_exhaustions",
                           "ring_overflows","timestamp_errors"):
                check(d.get(prefix+metric,1)==0,source+" "+metric)
    for key in required[4:]:check(d.get(key,1)==0,key+" nonzero")
    num,den=(int(x) for x in v.get("avg_frame_rate","60/1").split("/"))
    video_pts=timestamps(media,"v:0",den/num)
    audio_pts=timestamps(media,"a:0",1024/48000)
    check(video_pts["bad_steps"]==0,"Video packet PTS gaps")
    check(audio_pts["bad_steps"]==0,"AAC packet PTS gaps")
    pcm=decoded_audio(media)
    check(pcm["clipped"]==0 and pcm["nonfinite"]==0,"Decoded PCM clipping/NaN")
    check(abs(pcm["duration_s"]-ad)<0.12,"Decoded duration mismatch")
    check(any(s["audible"] for s in pcm["segments"]),"No audible source")
    for seg in pcm["segments"]:
        if seg["audible"] and seg["boundary_count"]>=50:
            check(seg["boundary_ratio"] is not None and seg["boundary_ratio"]<2.5,
                  "AAC block crackle near "+str(seg["start_s"])+"s")
    return {"status":"PASS" if not errors else "FAIL",
            "schema":"arssyut-av-acceptance-v1",
            "media_sha256":digest(media),"diagnostics_sha256":digest(diagnostics),
            "sources":{"system":d.get("audio_system_requested"),
                       "microphone":d.get("audio_microphone_requested")},
            "video":{"codec":v.get("codec_name"),"duration_s":vd,"pts":video_pts},
            "audio":{"codec":a.get("codec_name"),"duration_s":ad,
                     "pts":audio_pts,"decoded":pcm},
            "missing_source_intervals":d.get("audio_missing_source_intervals"),
            "errors":errors,"limitation":"Must also listen and test install/Start-Stop on real Windows"}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("media",type=Path)
    p.add_argument("diagnostics",type=Path)
    p.add_argument("--output",type=Path)
    a=p.parse_args()
    try:result=evaluate(a.media,a.diagnostics)
    except (OSError,ValueError,KeyError,subprocess.CalledProcessError) as e:
        print("FAIL",e,file=sys.stderr);return 2
    print(json.dumps({"status":result["status"],"sources":result["sources"],
                      "segments":result["audio"]["decoded"]["segments"],
                      "errors":result["errors"]},indent=2))
    if a.output:
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    return 0 if result["status"]=="PASS" else 1

if __name__=="__main__":
    sys.exit(main())
