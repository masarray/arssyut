"""Non-skipped CI policy tests. Hardware MP4 acceptance must run separately."""
import importlib.util
from pathlib import Path
import unittest

PATH = Path(__file__).resolve().parents[2] / "tools/qa/verify_av_mp4.py"
spec = importlib.util.spec_from_file_location("qa", PATH)
qa = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qa)


def fixture(v_dur=27.133317, a_dur=27.157312, v_n=1628, a_n=1273, offset=0):
    metadata = {"streams": [
        {"index": 0, "codec_type": "video", "codec_name": "h264",
         "duration": str(v_dur), "start_time": "0", "avg_frame_rate": "60/1"},
        {"index": 1, "codec_type": "audio", "codec_name": "aac", "channels": 2,
         "sample_rate": "48000", "duration": str(a_dur), "start_time": str(offset)}]}
    return metadata, {
        0: [(i/60, i/60, 1/60) for i in range(v_n)],
        1: [(offset+i*1024/48000, offset+i*1024/48000, 1024/48000)
            for i in range(a_n)]}


class MP4Policy(unittest.TestCase):
    def test_real_27s_system_audio_shape_and_pause_allowed(self):
        m, p = fixture()
        a = qa.evaluate(m, p, elapsed=27.16)
        self.assertEqual(a["video_packets"], 1628)
        self.assertLess(a["av_end_delta_ms"], 25)

    def test_old_22s_capture_with_2s_video_and_170ms_audio_rejected(self):
        m, p = fixture(2.149983, .170646, 129, 8)
        with self.assertRaisesRegex(qa.VerificationError, "truncated"):
            qa.evaluate(m, p, elapsed=22.6167)

    def test_equally_short_streams_rejected_against_elapsed(self):
        m, p = fixture(12, 12, 720, 563)
        with self.assertRaisesRegex(qa.VerificationError, "coverage"):
            qa.evaluate(m, p, elapsed=26)

    def test_missing_audio_rejected(self):
        m, p = fixture()
        m["streams"].pop()
        with self.assertRaisesRegex(qa.VerificationError, "required"):
            qa.evaluate(m, p)

    def test_start_offset_rejected(self):
        m, p = fixture(offset=.18)
        with self.assertRaisesRegex(qa.VerificationError, "start mismatch"):
            qa.evaluate(m, p)

    def test_lost_audio_packets_rejected(self):
        m, p = fixture()
        p[1] = p[1][:450] + p[1][457:]
        with self.assertRaisesRegex(qa.VerificationError, "PTS gap"):
            qa.evaluate(m, p)

    def test_lost_tail_rejected_even_with_correct_header(self):
        m, p = fixture()
        p[1] = p[1][:1000]
        with self.assertRaisesRegex(qa.VerificationError, "tail truncated"):
            qa.evaluate(m, p)

    def test_backward_dts_rejected(self):
        m, p = fixture()
        p[1][10] = (p[1][10][0], -1, p[1][10][2])
        with self.assertRaisesRegex(qa.VerificationError, "backwards DTS"):
            qa.evaluate(m, p)

    def test_excessive_end_delta_rejected(self):
        m, p = fixture(a_dur=27.5)
        with self.assertRaisesRegex(qa.VerificationError, "end mismatch"):
            qa.evaluate(m, p)


if __name__ == "__main__":
    unittest.main()
