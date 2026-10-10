#!/usr/bin/env python3
"""Inspect actual H264/AAC MP4 duration, packets and decoded audio for acceptance.

Run on a device-created MP4. Generic runner codec smoke SKIPs are NOT acceptance.
Requires ffmpeg and ffprobe only on the QA machine, never in the GUI recorder.
Intentional source pauses/silence must remain valid. No audio gain modification.
"""
import argparse
from array import array
import csv
from fractions import Fraction
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys


class VerificationError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise VerificationError(message)


def evaluate(metadata, packets, elapsed=None, min_seconds=10.0):
    """Pure assertions allow CI to reject historical truncation without GPU."""
    streams = {s.get("codec_type"): s for s in metadata.get("streams", [])}
    require("video" in streams and "audio" in streams, "H264 video and AAC audio are required")
    video, audio = streams["video"], streams["audio"]
    require(video.get("codec_name") == "h264" and audio.get("codec_name") == "aac",
            "expected H264 video and AAC audio")
    require(int(audio.get("sample_rate", 0)) == 48000, "expected 48 kHz AAC")
    require(int(audio.get("channels", 0)) in (1, 2), "invalid audio channels")
    vs, ads = float(video.get("start_time", 0)), float(audio.get("start_time", 0))
    vd, ad = float(video.get("duration", 0)), float(audio.get("duration", 0))
    require(vd >= min_seconds and ad >= min_seconds,
            f"truncated track: video={vd:.3f}s audio={ad:.3f}s")
    require(abs(vs - ads) <= .100, f"A/V start mismatch {abs(vs-ads):.3f}s")
    require(abs(vs + vd - ads - ad) <= .100,
            f"A/V end mismatch {abs(vs+vd-ads-ad):.3f}s")
    if elapsed is not None:
        require(elapsed > 0 and min(vd, ad) >= .95 * elapsed,
                f"media coverage <95% of {elapsed:.3f}s recording")

    counts = {}
    for kind, stream in (("video", video), ("audio", audio)):
        sample_list = packets.get(int(stream["index"]), [])
        counts[kind] = len(sample_list)
        require(len(sample_list) >= 2, f"{kind}: missing packets")
        dts = [p[1] for p in sample_list]
        require(all(t is not None for t in dts), f"{kind}: missing DTS")
        require(all(b >= a - 0.000002 for a, b in zip(dts, dts[1:])),
                f"{kind}: backwards DTS")
        pts = sorted(p[0] for p in sample_list)
        if kind == "audio":
            interval = 1024 / 48000
        else:
            fps = Fraction(str(stream.get("avg_frame_rate", "0/1")))
            require(fps > 0, "video frame rate unavailable")
            interval = float(1 / fps)
        largest_gap = max((b-a for a, b in zip(pts, pts[1:])), default=0)
        require(largest_gap <= max(3*interval, .080 if kind == "audio" else .100) + .000002,
                f"{kind}: packet PTS gap {largest_gap:.3f}s")
        last = max(sample_list, key=lambda p: p[0])
        require(last[0] + (last[2] or interval) >=
                float(stream.get("start_time", 0)) + float(stream["duration"]) - .100,
                f"{kind}: packet tail truncated despite header duration")

    return {"video_seconds": vd, "audio_seconds": ad,
            "av_end_delta_ms": round(abs(vs+vd-ads-ad)*1000, 2),
            "video_packets": counts["video"], "audio_packets": counts["audio"],
            "elapsed_seconds": elapsed}


def probe(path):
    info = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries",
         "stream=index,codec_type,codec_name,start_time,duration,sample_rate,channels,avg_frame_rate",
         "-of", "json", str(path)], capture_output=True, text=True, check=True)
    result = {}
    args = ["ffprobe", "-v", "error", "-show_packets", "-show_entries",
            "packet=stream_index,pts_time,dts_time,duration_time",
            "-of", "csv=p=0", str(path)]
    with subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True) as process:
        for row in csv.reader(process.stdout):
            if not row:
                continue
            require(len(row) >= 4 and row[1] != "N/A",
                    f"unreadable packet timing: {row}")
            idx, pts, dts, dur = row[:4]
            result.setdefault(int(idx), []).append(
                (float(pts), float(dts) if dts != "N/A" else None,
                 float(dur) if dur != "N/A" else None))
        message = process.stderr.read()
        require(process.wait() == 0, f"ffprobe packets failed: {message[-250:]}")
    return json.loads(info.stdout), result


def decode_pcm(path):
    args = ["ffmpeg", "-nostdin", "-v", "error", "-i", str(path),
            "-map", "0:a:0", "-vn", "-ac", "2", "-ar", "48000",
            "-f", "s16le", "pipe:1"]
    count = total_squared = peak = clipped = 0
    with subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
        while True:
            raw = process.stdout.read(65536)
            if not raw:
                break
            require(len(raw) % 2 == 0, "invalid PCM alignment")
            values = array("h")
            values.frombytes(raw)
            if sys.byteorder != "little":
                values.byteswap()
            count += len(values)
            total_squared += sum(x*x for x in values)
            peak = max(peak, max((abs(x) for x in values), default=0))
            clipped += sum(abs(x) >= 32767 for x in values)
        message = process.stderr.read().decode("utf-8", "replace")
        require(process.wait() == 0, f"audio decode failed: {message[-250:]}")
    require(count and peak > 0, "AAC decoded as silent/empty")
    require(clipped / count < .0001, "AAC decoded PCM contains excessive clipping")
    return {"decoded_pcm_seconds": count / (2*48000),
            "pcm_peak_dbfs": round(20*math.log10(peak/32768), 2),
            "pcm_rms_dbfs": round(20*math.log10(math.sqrt(total_squared/count)/32768), 2),
            "clipped_samples": clipped}


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("mp4", type=Path)
    cli.add_argument("--diagnostics", type=Path,
                     help="matching .mp4.diagnostics.json from the recorder")
    cli.add_argument("--require-diagnostics", action="store_true",
                     help="strict hardware acceptance; cannot infer wall-clock from MP4")
    cli.add_argument("--min-seconds", type=float, default=10.0)
    args = cli.parse_args()
    try:
        require(args.mp4.is_file() and args.min_seconds > 0, "invalid MP4/minimum duration")
        require(not args.require_diagnostics or args.diagnostics is not None,
                "strict acceptance needs recorder diagnostics")
        require(shutil.which("ffprobe") and shutil.which("ffmpeg"),
                "external ffprobe and ffmpeg tools required")
        elapsed = None
        if args.diagnostics:
            diag = json.loads(args.diagnostics.read_text(encoding="utf-8-sig"))
            elapsed = float(diag["elapsed_ticks_100ns"]) / 10_000_000
            require(diag.get("result") == "ready" and diag.get("status_code") == 0,
                    "recorder did not report finalized success")
            require(diag.get("audio_writer_enabled") is True and
                    (diag.get("audio_microphone_requested") or
                     diag.get("audio_system_requested")), "audio source/track not enabled")
        metadata, packets = probe(args.mp4)
        result = evaluate(metadata, packets, elapsed=elapsed, min_seconds=args.min_seconds)
        decoded = decode_pcm(args.mp4)
        require(decoded["decoded_pcm_seconds"] >= .95*result["audio_seconds"],
                "decoded AAC substantially shorter than audio track")
        print(json.dumps({"result": "PASS" if elapsed is not None else
                          "MEDIA_PASS_DIAGNOSTICS_MISSING", **result, **decoded}, indent=2))
        return 0
    except (OSError, KeyError, ValueError, subprocess.CalledProcessError,
            VerificationError) as exc:
        print(json.dumps({"result": "FAIL", "reason": str(exc)}, indent=2), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
