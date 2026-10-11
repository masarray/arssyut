"""Fast no-FFmpeg unit tests for the Pause/Resume quality gate."""
import importlib.util
import json
from pathlib import Path
import tempfile
from unittest import TestCase, mock

ROOT = Path(__file__).resolve().parents[2]
path = ROOT / "tools/acceptance/check_pause_recording.py"
spec = importlib.util.spec_from_file_location("pause_quality", path)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

def av_result(errors=()):
    return {
        "status": "PASS" if not errors else "FAIL",
        "errors": list(errors),
        "audio": {"duration_s": 229.96},
        "video": {"duration_s": 229.93},
    }

class PauseAcceptanceTests(TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.mp4 = Path(folder.name) / "sample.mp4"
        self.mp4.write_bytes(b"fake")
        self.diag = Path(folder.name) / "sample.json"
        self.data = {
            "pause_count": 13,
            "pause_excluded_ticks_100ns": 2327152073,
            "elapsed_ticks_100ns": 2299576620,
            "pause_resume_events": 13,
            "pause_resume_seam_overflow": 0,
            "pause_resume_media_ticks_100ns":
                [100000000+i*160000000 for i in range(13)],
            "audio_microphone_packets_captured": 46458,
            "audio_microphone_packets_published": 46458,
            "audio_microphone_packets_dropped": 0,
            "audio_microphone_pool_exhaustions": 0,
            "audio_system_packets_dropped": 0,
            "audio_stale_canonical_frames": 0,
            "audio_duplicate_canonical_frames": 0,
            "audio_staging_gaps": 0,
            "audio_staging_overflows": 0,
            "audio_dropped_program_blocks": 0,
            "video_skipped": 0,
            "encoder_backpressure": 0,
        }
    def evaluate(self, errors=()):
        self.diag.write_text(json.dumps(self.data), encoding="utf-8")
        per_join = [{"bad_steps":0,"media_time_s":1.0}]
        with mock.patch.object(gate.av, "evaluate",
                               return_value=av_result(errors)), \
             mock.patch.object(gate, "_seam_pts", return_value=per_join):
            return gate.evaluate_pause(self.mp4, self.diag)

    def test_clean_thirteen_resumes_pass(self):
        output = self.evaluate()
        self.assertEqual(output["status"], "PASS")
        self.assertTrue(output["release_accepted"])
        self.assertEqual(output["resume_count"], 13)
        self.assertAlmostEqual(output["excluded_pause_s"], 232.7152073)

    def test_real_capture_eight_mic_pool_drops_requires_review(self):
        # Sanitized counters from the user-tested 13-pause MP4.
        self.data["audio_microphone_packets_published"] = 46450
        self.data["audio_microphone_packets_dropped"] = 8
        self.data["audio_microphone_pool_exhaustions"] = 8
        self.data["video_skipped"] = 1
        report = self.evaluate((
            "microphone packet loss",
            "microphone packets_dropped",
            "microphone pool_exhaustions",
            "video_skipped nonzero",
        ))
        self.assertEqual(report["status"], "REVIEW")
        self.assertFalse(report["release_accepted"])

    def test_larger_dropouts_fail(self):
        self.data["audio_microphone_packets_dropped"] = 200
        self.assertEqual(self.evaluate(("microphone packet loss",))["status"],
                         "FAIL")

    def test_system_audio_gap_never_warn_only(self):
        self.data["audio_system_packets_dropped"] = 1
        self.assertEqual(self.evaluate(("system packet loss",))["status"],
                         "FAIL")

    def test_evidence_without_individual_seams_needs_review(self):
        for key in ("pause_resume_events", "pause_resume_seam_overflow",
                    "pause_resume_media_ticks_100ns"):
            del self.data[key]
        report = self.evaluate()
        self.assertEqual(report["status"], "REVIEW")
        self.assertFalse(report["release_accepted"])

    def test_fake_seam_timestamp_fails(self):
        self.data["pause_resume_media_ticks_100ns"][4] = -200
        self.assertEqual(self.evaluate()["status"], "FAIL")

    def test_stop_while_paused_is_legal(self):
        self.data["pause_resume_events"] = 12
        self.data["pause_resume_media_ticks_100ns"].pop()
        self.assertEqual(self.evaluate()["status"], "PASS")

    def test_stop_while_paused_without_resume_is_valid(self):
        self.data["pause_count"] = 1
        self.data["pause_resume_events"] = 0
        self.data["pause_resume_media_ticks_100ns"] = []
        self.assertEqual(self.evaluate()["status"], "PASS")

    def test_more_than_64_resumes_requires_manual_review(self):
        self.data["pause_count"] = 65
        self.data["pause_resume_events"] = 65
        self.data["pause_resume_seam_overflow"] = 1
        self.data["pause_resume_media_ticks_100ns"] = [
            5_000_000 + 30_000_000 * i for i in range(64)]
        self.assertEqual(self.evaluate()["status"], "REVIEW")

    def test_core_audio_staging_error_is_always_fatal(self):
        self.data["audio_staging_gaps"] = 1
        self.assertEqual(self.evaluate(("audio_staging_gaps nonzero",))["status"],
                         "FAIL")

    def test_resume_instrumentation_has_fixed_memory_bound(self):
        header = (ROOT / "src/app/recorder_session.hpp").read_text("utf-8")
        cpp = (ROOT / "src/app/recorder_session.cpp").read_text("utf-8")
        self.assertIn("kMaxPauseResumeSeams = 64;", header)
        self.assertIn("std::array<std::int64_t, kMaxPauseResumeSeams>", header)
        self.assertIn("pause_resume_seams_.fill(0);", cpp)
        self.assertIn("pause_clock.elapsed(physical_now.ticks_100ns)", cpp)
        self.assertIn("pause_resume_media_ticks_100ns", cpp)
        self.assertIn("pause_resume_seam_overflow", cpp)

    def test_nonmonotonic_pts_around_seam_fails(self):
        self.diag.write_text(json.dumps(self.data), encoding="utf-8")
        with mock.patch.object(gate.av, "evaluate", return_value=av_result()), \
             mock.patch.object(gate, "_seam_pts",
                               return_value=[{"bad_steps":1}]):
            self.assertEqual(gate.evaluate_pause(self.mp4, self.diag)["status"],
                             "FAIL")
