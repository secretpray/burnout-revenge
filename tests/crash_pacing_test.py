#!/usr/bin/env python3
"""Pacing ledger joins and immutable render-pose resets, without game data."""
import copy
import math
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts/macos'))
import analyze_crash_pacing as pacing
import experiment_crash_poses as poses


def sample(time=1_000_000_000, world=1., kind='render_camera'):
    r = {k: '0' for k in ('car_x', 'car_y', 'car_z', 'car_dx', 'car_dy', 'car_dz',
                          *('c'+str(i) for i in range(12)))}
    r.update(kind=kind, steady_ns=str(time), session_s=str(time/1e9), world_s=str(world),
             dt_s=str(1/60), phase='5', event_state='18', playback='0', camera_frame='123',
             camera_mode='9', clock_flags=str(0x01000000), c7='1', car_dz='1', clock_tick='60')
    return r


class PoseExperiment(unittest.TestCase):
    def test_translation_rotation_and_immutability(self):
        a, b = sample(), sample(1_033_333_333, 1+1/30)
        b.update(car_x='2', c8='4', c6=str(math.sin(math.pi/12)), c7=str(math.cos(math.pi/12)))
        saved = copy.deepcopy((a, b))
        mid, error = poses.midpoint(a, b)
        self.assertIsNone(error)
        self.assertEqual(mid['car_position'], (1., 0., 0.))
        self.assertEqual(mid['camera_position'], (2., 0., 0.))
        self.assertAlmostEqual(poses.length(mid['camera_quaternion']), 1)
        self.assertAlmostEqual(poses.angle_degrees((0, 0, 0, 1), mid['camera_quaternion'], True), 15)
        self.assertEqual((a, b), saved)

    def test_quaternion_sign_is_not_a_cut(self):
        a, b = sample(), sample(1_033_333_333, 1+1/30)
        b['c7'] = '-1'
        mid, error = poses.midpoint(a, b)
        self.assertIsNone(error)
        self.assertEqual(mid['camera_quaternion'], (0., 0., 0., 1.))

    def test_resets(self):
        changes = [({'playback': '1'}, 'context_change'), ({'camera_mode': '12'}, 'context_change'),
                   ({'camera_frame': '456'}, 'context_change'), ({'event_state': '2'}, 'context_change'),
                   ({'phase': '4'}, 'context_change'), ({'clock_flags': '0'}, 'clock_not_live'),
                   ({'world_s': '.5'}, 'paused_or_rewound'), ({'world_s': '1'}, 'paused_or_rewound'),
                   ({'steady_ns': '1200000000'}, 'gap_or_order'),
                   ({'steady_ns': '1000000000'}, 'gap_or_order'),
                   ({'car_x': '9'}, 'translation_cut'), ({'c8': '9'}, 'translation_cut'),
                   ({'c6': '1', 'c7': '0'}, 'camera_turn_cut'),
                   ({'car_dz': '-1'}, 'car_turn_cut'), ({'c7': 'nan'}, 'invalid_pose'),
                   ({'car_dz': '0'}, 'invalid_pose')]
        for change, reason in changes:
            with self.subTest(change=change):
                a, b = sample(), sample(1_033_333_333, 1+1/30)
                b.update(change)
                self.assertEqual(poses.midpoint(a, b), (None, reason))

    def test_replay_never_blended(self):
        a, b = sample(), sample(1_033_333_333, 1+1/30)
        a['playback'] = b['playback'] = '1'
        self.assertEqual(poses.midpoint(a, b), (None, 'outside_live_crash'))

    def test_full_vehicle_matrix_and_roll(self):
        a, b = sample(), sample(1_033_333_333, 1+1/30)
        for r in (a, b):
            r.update({"m"+str(i): "0" for i in range(16)})
            r.update(m0='1', m5='1', m10='1')
        angle = math.pi/6
        b.update(m0=str(math.cos(angle)), m1=str(math.sin(angle)),
                 m4=str(-math.sin(angle)), m5=str(math.cos(angle)))
        mid, error = poses.midpoint(a, b)
        self.assertIsNone(error)
        self.assertAlmostEqual(poses.angle_degrees((0, 0, 0, 1), mid['car_quaternion'], True), 15)
        b['m10'] = '-1'
        self.assertEqual(poses.midpoint(a, b), (None, 'invalid_pose'))

    def test_vehicle_half_turns_on_each_axis(self):
        # These exact half turns exercise all branches of matrix-to-quaternion
        # conversion without depending on another conversion implementation.
        for diagonal, expected in [((1, -1, -1), (1, 0, 0, 0)),
                                   ((-1, 1, -1), (0, 1, 0, 0)),
                                   ((-1, -1, 1), (0, 0, 1, 0))]:
            with self.subTest(diagonal=diagonal):
                r = sample()
                r.update({'m'+str(i): '0' for i in range(16)})
                for i, x in zip((0, 5, 10), diagonal):
                    r['m'+str(i)] = str(x)
                self.assertLess(poses.angle_degrees(poses.matrix_quaternion(r), expected, True), 1e-6)

    def test_native_post_step_comparison(self):
        a, b = sample(), sample(1_033_333_333, 1+1/30)
        b['car_x'] = '2'
        native = sample(1_016_666_667, 1+1/60, 'native_update_end')
        native['car_x'] = '1'
        trace = [a, native, b]
        report, candidates = poses.experiment(trace, 0, 2)
        self.assertEqual(report['simulation_writes'], 0)
        self.assertEqual(len(candidates), 1)
        e = report['native_inner_sample_errors_world_units_or_degrees']
        self.assertEqual(e['car_position_interpolated']['max'], 0)
        self.assertEqual(e['car_position_held']['max'], 1)
        self.assertGreater(report['minimum_midpoint_lookback_ms']['p50'], 16)

    def test_native_comparison_skips_different_camera_context(self):
        a, b = sample(), sample(1_033_333_333, 1+1/30)
        native = sample(1_016_666_667, 1+1/60, 'native_update_end')
        native['camera_mode'] = '99'
        report, candidates = poses.experiment([a, native, b], 0, 2)
        self.assertEqual(len(candidates), 1)
        self.assertEqual(report['native_inner_sample_errors_world_units_or_degrees'], {})


class TimingLedger(unittest.TestCase):
    def test_reports_join_by_swapchain_and_present_not_row_order(self):
        def present(i, swap, source, at):
            return dict(id=str(i), swapchain=str(swap), source_id=str(source), presented=str(at),
                        submitted=str(at-10), vblank=str(at-20), on_screen=str(at+2), fresh='1')
        presents = [present(1, 1, 10, 100), present(2, 1, 10, 200), present(1, 2, 11, 300),
                    present(2, 2, 12, 400), present(3, 2, 13, 500)]
        actual = [dict(id='1', swapchain='2', actual_ns='330', host_to_steady_ns='0'),
                  dict(id='1', swapchain='1', actual_ns='130', host_to_steady_ns='0'),
                  dict(id='2', swapchain='1', actual_ns='230', host_to_steady_ns='0'),
                  dict(id='2', swapchain='2', actual_ns='0', host_to_steady_ns='0')]
        sources = [dict(submitted='90', replaced_source_id='0'), dict(submitted='290', replaced_source_id='9')]
        world = [sample(50, .1, 'boundary')]
        world[0]['session_s'] = '0.00000005'
        r = pacing.analyze(presents, actual, sources, world, 0, .000001, 'unknown')
        self.assertEqual(r['driver_reported_shown'], 3)
        self.assertEqual(r['driver_reported_never_shown'], 1)
        self.assertEqual(r['driver_reports_missing'], 1)
        self.assertEqual(r['distinct_source_ids_shown'], 2)
        self.assertEqual(r['same_source_redraws_shown'], 1)
        self.assertEqual(r['pending_replacements'], 1)
        self.assertIsNone(r['fixed_refresh_holds'])

    def test_partial_tail_and_interior_corruption(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)/'ledger.csv'
            p.write_text('# clock\na,b\n1,2\n3,')
            self.assertEqual(pacing.rows(p), [{'a': '1', 'b': '2'}])
            p.write_text('a,b\n1,2,3\n4,5\n')
            with self.assertRaises(ValueError): pacing.rows(p)

    def test_display_label_and_focus_evidence(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)/'session.log'
            p.write_text('[ 0.2] [pacing-display] mode_hz=60.0000 min_hz=59.9967 max_hz=59.9967 external_fixed=1; note\n'
                         '[ 1.0] [video] window focus gained (here)\n'
                         '[ 5.0] [video] window focus lost (elsewhere)\n')
            r = pacing.session_evidence(p, 2, 6, 'fixed-60')
            self.assertEqual(r['focus_at_window_start'], 'foreground')
            self.assertEqual(r['focus_changes_in_window'], [{'session_s': 5., 'state': 'background'}])
            with self.assertRaises(ValueError): pacing.session_evidence(p, 2, 6, 'promotion')
            with self.assertRaises(ValueError): pacing.session_evidence(p, 2, 6, 'fixed-120')

    def test_empty_is_unknown_not_zero_latency(self):
        self.assertIsNone(pacing.stats([]))
        with self.assertRaises(ValueError): pacing.analyze([], [], [], [], 0, 1, 'unknown')


if __name__ == '__main__':
    unittest.main()
