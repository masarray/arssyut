"""Fast mocked tests for the independent Windows A/V acceptance script."""
import importlib.util
import json
from pathlib import Path
import tempfile
from unittest import TestCase, mock

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location(
    "av_gate",ROOT/"tools"/"acceptance"/"check_av_recording.py")
gate=importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

class AVGateContract(TestCase):
    def setUp(self):
        tmp=tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.video=Path(tmp.name)/"fake.mp4"
        self.video.write_bytes(b"fixture")
        self.diag=Path(tmp.name)/"fake.json"
        self.d={
            "schema":"arssyut-diagnostics-v1","result":"ready","status_code":0,
            "width":1920,"height":1080,"audio_writer_enabled":True,
            "audio_system_requested":True,"audio_microphone_requested":True,
            "audio_stale_canonical_frames":0,"audio_duplicate_canonical_frames":0,
            "audio_staging_gaps":0,"audio_staging_overflows":0,
            "audio_dropped_program_blocks":0,"encoder_backpressure":0,
            "video_skipped":0,"capture_busy_drops":0,
            "audio_missing_source_intervals":350}
        for source in ("system","microphone"):
            for key in ("packets_captured","packets_published"):
                self.d["audio_"+source+"_"+key]=100
            for key in ("packets_dropped","pool_exhaustions","ring_overflows",
                        "timestamp_errors"):
                self.d["audio_"+source+"_"+key]=0
        self.audio={"duration_s":52.096,"clipped":0,"nonfinite":0,
                    "segments":[{"start_s":10,"audible":True,"boundary_count":230,
                                 "boundary_ratio":1.02}]}
        self.streams={"streams":[{"codec_type":"video","codec_name":"h264",
            "width":1920,"height":1080,"duration":"52.06665",
            "avg_frame_rate":"60/1"},
            {"codec_type":"audio","codec_name":"aac","sample_rate":"48000",
             "channels":2,"duration":"52.096"}]}
    def run_gate(self):
        self.diag.write_text(json.dumps(self.d),encoding="utf-8")
        with mock.patch.object(gate,"probe",return_value=self.streams), \
             mock.patch.object(gate,"timestamps",return_value={"bad_steps":0,"count":2400}), \
             mock.patch.object(gate,"decoded_audio",return_value=self.audio):
            return gate.evaluate(self.video,self.diag)
    def test_healthy_dual_with_missing_source_intervals_passes(self):
        self.assertEqual(self.run_gate()["status"],"PASS")
    def test_stale_pcm_fails(self):
        self.d["audio_stale_canonical_frames"]=240127
        self.assertEqual(self.run_gate()["status"],"FAIL")
    def test_crackling_aac_fails_despite_ready_mux(self):
        self.audio["segments"][0]["boundary_ratio"]=5.7
        self.assertEqual(self.run_gate()["status"],"FAIL")
    def test_missing_source_counter_is_not_assumed(self):
        del self.d["audio_system_packets_dropped"]
        self.assertEqual(self.run_gate()["status"],"FAIL")
