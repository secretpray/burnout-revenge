"""The benchmark must read timed session records, and not substitute queue time for GPU execution."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts/macos'))
from perf_log import parse_sample, parse_session, session_clock_warnings


class PerfLogTest(unittest.TestCase):
    record = '[perf] 59.9 fps (worst frame 18.0 ms) | CP busy 4.0 ms/frame | per frame: 3000 draws, 1.6 uploads, GPU busy 12.4 ms (executing 8.1 ms) | 0.2 ms stalled (0.0 pool full) | post-processing GPU 2.50 ms (AA 0.70 ms)'

    def test_session_timestamp_and_gpu_fields(self):
        s = parse_sample('[   83.2] ' + self.record, 999)
        self.assertEqual((s['seconds'], s['fps']), (83.2, 59.9))
        self.assertEqual((s['game_gpu_ms'], s['gpu_queue_ms'], s['post_gpu_ms'], s['aa_gpu_ms']), (8.1, 12.4, 2.5, .7))

    def test_legacy_stdout(self):
        self.assertEqual(parse_sample(self.record, 12.25)['seconds'], 12.25)
        self.assertIsNone(parse_sample(self.record))

    def test_cutoff_and_non_perf_lines(self):
        text = '[ 10.0] ' + self.record + '\n[ 20.0] unrelated\n[ 30.0] ' + self.record
        self.assertEqual([s['seconds'] for s in parse_session(text, 15)], [30.0])
        self.assertIsNone(parse_sample('[perf] malformed', 0))

    def test_missing_gpu_time_is_not_invented(self):
        self.assertNotIn('game_gpu_ms', parse_sample('[perf] 60.0 fps', 0))

    def test_clock_discontinuity_is_reported(self):
        warnings = session_clock_warnings('[ 917.1] [perf] 60.0 fps', 275.1)
        self.assertEqual(len(warnings), 1)
        self.assertIn('917.1s', warnings[0])

    def test_normal_clock_and_untimed_log_are_accepted(self):
        self.assertEqual(session_clock_warnings('[ 184.0] stopping', 185.1), [])
        self.assertEqual(session_clock_warnings('[perf] 60.0 fps', 10), [])


if __name__ == '__main__':
    unittest.main()
