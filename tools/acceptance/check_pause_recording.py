#!/usr/bin/env python3
"""Strict, auditable real-Windows Pause/Resume quality gate.

Runs the existing integrated H264/AAC recording gate ONCE and adds bounded
individual Resume-seam checks. REVIEW is not PASS and never approves a release.
Python stdlib + ffmpeg/ffprobe; no changes to the realtime recorder.
"""
import argparse
import bisect
import importlib.util
import json
from pathlib import Path
import sys

BASE = Path(__file__).with_name("check_av_recording.py")
spec = importlib.util.spec_from_file_location("arssyut_av_base", BASE)
av = importlib.util.module_from_spec(spec)
spec.loader.exec_module(av)

TICKS_PER_SECOND = 10_000_000
MAX_RECORDED_SEAMS = 64
# Measured field capture: 8 / 46458 microphone packets and 1 / 13796 video
# frames were dropped while the canonical AAC program stayed gap-free.
# Such sessions require listening/review; this is NOT automatic signoff.
MIC_REVIEW_MAX_DROPS = 16
MIC_REVIEW_MAX_FRACTION = 0.0003  # 0.03% (only for manual-review band)
VIDEO_REVIEW_MAX_SKIPS = 2
SOFT_ERRORS = {
    "microphone packet loss",
    "microphone packets_dropped",
    "microphone pool_exhaustions",
    "video_skipped nonzero",
}

def _packet_times(path, stream):
    packets = av.probe(path, stream)["packets"]
    return [float(p["pts_time"]) for p in packets]

def _seam_pts(path, seams, stream, expected):
    values = _packet_times(path, stream)
    if len(values) < 3:
        raise ValueError("Not enough " + stream + " timestamps")
    result = []
    for ticks in seams:
        sec = ticks / TICKS_PER_SECOND
        index = bisect.bisect_left(values, sec)
        # PTS at 60 FPS or AAC 1024-sample cadence: locate each seam rather
        # than relying solely on whole-file aggregate counters.
        low = max(0, index - 3)
        high = min(len(values), index + 4)
        steps = [values[i + 1] - values[i]
                 for i in range(low, high - 1)]
        result.append({
            "media_time_s": round(sec, 6),
            "nearest_pts_delta_ms": round(
                min(abs(values[min(index, len(values)-1)]-sec),
                    abs(values[max(index-1, 0)]-sec)) * 1000, 4),
            "max_surrounding_step_ms": round(max(steps)*1000, 4),
            "bad_steps": sum(not (0 < v <= 1.6*expected) for v in steps),
        })
    return result

def evaluate_pause(media, diagnostics):
    d = json.loads(Path(diagnostics).read_text(encoding="utf-8"))
    base = av.evaluate(media, diagnostics)
    hard = []
    review = []
    for error in base["errors"]:
        if error in SOFT_ERRORS:
            review.append(error)
        else:
            hard.append(error)

    count = d.get("pause_count")
    excluded = d.get("pause_excluded_ticks_100ns")
    elapsed = d.get("elapsed_ticks_100ns")
    if not isinstance(count, int) or isinstance(count, bool) or count < 1:
        hard.append("Pause count must be positive")
    if not isinstance(excluded, int) or excluded <= 0:
        hard.append("Pause duration not excluded")
    if not isinstance(elapsed, int) or elapsed <= 0:
        hard.append("Encoded duration missing")

    # Zero packet loss is the ideal. An extremely small microphone loss has
    # an explicit REVIEW band, never a PASS, while 0 program loss remains hard.
    mic_drop = d.get("audio_microphone_packets_dropped", 0)
    mic_pool = d.get("audio_microphone_pool_exhaustions", 0)
    mic_cap = d.get("audio_microphone_packets_captured", 0)
    video_skip = d.get("video_skipped", 0)
    if (not all(isinstance(n, int) and not isinstance(n, bool) and n >= 0
                for n in (mic_drop, mic_pool, mic_cap, video_skip))):
        hard.append("Invalid microphone/video drop counters")
    else:
        if mic_drop:
            if (mic_drop > MIC_REVIEW_MAX_DROPS or mic_cap <= 0 or
                    mic_drop / mic_cap > MIC_REVIEW_MAX_FRACTION or
                    mic_pool != mic_drop):
                hard.append("Microphone loss exceeds documented review band")
            else:
                review.append("Microphone packets dropped: " + str(mic_drop))
        if video_skip:
            if video_skip > VIDEO_REVIEW_MAX_SKIPS:
                hard.append("Video skipped frames exceed review band")
            else:
                review.append("Video skipped frames: " + str(video_skip))
    # Dropped program blocks, source staging gaps/overflow, timestamp errors
    # and ANY system-loopback packet drop are always hard failures, regardless
    # of how few per-source mic frames were lost.
    for key in ("audio_stale_canonical_frames",
                "audio_duplicate_canonical_frames",
                "audio_staging_gaps", "audio_staging_overflows",
                "audio_dropped_program_blocks",
                "audio_system_packets_dropped", "encoder_backpressure"):
        if d.get(key) != 0:
            hard.append("Fatal canonical A/V counter: " + key)

    seams = d.get("pause_resume_media_ticks_100ns")
    events = d.get("pause_resume_events")
    overflow = d.get("pause_resume_seam_overflow")
    packet_checks = {}
    if seams is None or events is None or overflow is None:
        # Older successfully finalized 13-pause recordings do not have seam
        # locations. Preserve evidence but never imply all seams were audited.
        review.append("Older diagnostics: individual Resume locations unavailable")
        seams = []
    elif (not isinstance(seams, list) or
          not isinstance(events, int) or isinstance(events, bool) or
          not isinstance(overflow, int) or isinstance(overflow, bool)):
        hard.append("Malformed Resume location diagnostics")
        seams = []
    else:
        if (events < 0 or overflow < 0 or len(seams) != min(events, MAX_RECORDED_SEAMS)
                or overflow != max(0, events - MAX_RECORDED_SEAMS)):
            hard.append("Resume trace count/overflow mismatch")
        if any(not isinstance(t, int) or isinstance(t, bool) for t in seams):
            hard.append("Nonintegral Resume timestamps")
            seams = []
        if isinstance(count, int) and events > count:
            hard.append("More Resume transitions than Pause transitions")
        if isinstance(count, int) and count - events not in (0, 1):
            hard.append("Only Stop-while-paused may leave one Pause unresumed")
        if isinstance(elapsed, int) and (any(t <= 0 or t > elapsed + 500_000
                                             for t in seams)
                or any(b <= a for a, b in zip(seams, seams[1:]))):
            hard.append("Nonmonotonic/out-of-bounds Resume media positions")
        if overflow:
            review.append("Only first 64 Resume seams are individually traced")
        if seams and not any("Resume" in e for e in hard):
            video_step = 1 / 60.0
            frame_rate = base.get("video", {}).get("codec", "")
            # PCM and video already have global packet PTS checks in av.evaluate.
            # These additional checks produce a readable per-join audit.
            packet_checks["video"] = _seam_pts(
                media, seams, "v:0", video_step)
            packet_checks["audio"] = _seam_pts(
                media, seams, "a:0", 1024 / 48000.0)
            if any(x["bad_steps"] for v in packet_checks.values() for x in v):
                hard.append("Packet PTS gap adjacent to Resume seam")

    if isinstance(elapsed, int) and elapsed > 0:
        audio_duration = base.get("audio", {}).get("duration_s")
        video_duration = base.get("video", {}).get("duration_s")
        if (not isinstance(audio_duration, (int, float)) or
                abs(audio_duration - elapsed / TICKS_PER_SECOND) > .12):
            hard.append("AAC encoded duration diverges from paused media clock")
        if (not isinstance(video_duration, (int, float)) or
                abs(video_duration - elapsed / TICKS_PER_SECOND) > .12):
            hard.append("H264 encoded duration diverges from paused media clock")

    status = "FAIL" if hard else "REVIEW" if review else "PASS"
    return {
        "schema": "arssyut-pause-acceptance-v1",
        "status": status,
        "base_status": base["status"],
        "pause_count": count,
        "resume_count": events,
        "encoded_duration_s": (round(elapsed/TICKS_PER_SECOND, 6)
                               if isinstance(elapsed, int) else None),
        "excluded_pause_s": (round(excluded/TICKS_PER_SECOND, 6)
                             if isinstance(excluded, int) else None),
        "resume_join_packet_audit": packet_checks,
        "hard_errors": sorted(set(hard)),
        "review_items": sorted(set(review)),
        "release_accepted": status == "PASS",
        "note": "REVIEW requires listening and seam inspection; never publish it as quality-approved",
    }

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mp4", type=Path)
    p.add_argument("diagnostics", type=Path)
    p.add_argument("--output", type=Path)
    x = p.parse_args()
    try:
        result = evaluate_pause(x.mp4, x.diagnostics)
    except (OSError, ValueError, KeyError) as exc:
        print("FAIL: " + str(exc), file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2))
    if x.output:
        x.output.parent.mkdir(parents=True, exist_ok=True)
        x.output.write_text(json.dumps(result, indent=2) + "\n",
                            encoding="utf-8")
    return {"PASS": 0, "REVIEW": 3, "FAIL": 1}[result["status"]]

if __name__ == "__main__":
    sys.exit(main())
