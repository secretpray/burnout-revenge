"""Analytic checks for optional offline diagnostics (requires numpy).

Run with the analysis Python documented in BURNOUT_REVENGE_MAC_TEMPORAL_INPUTS.md;
not part of the dependency-free CMake test suite.
"""
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts/macos'))
from probe_camera_motion import reproject


class CameraMotionProbeTest(unittest.TestCase):
    def setUp(self):
        self.depth = np.full((4, 4), .2)
        self.identity = np.eye(4)
        self.viewport = np.array([2, 2, 2, 2, 1, 0], dtype=float)

    def test_identity(self):
        motion, _, _ = reproject(self.depth, self.identity, self.identity, self.viewport, self.viewport)
        np.testing.assert_allclose(motion, 0)

    def test_current_to_previous_pixels(self):
        previous = self.identity.copy()
        previous[0, 3] = .5
        motion, _, _ = reproject(self.depth, self.identity, previous, self.viewport, self.viewport)
        # Half an NDC unit at sx=2 is one pixel to the previous location.
        np.testing.assert_allclose(motion[..., 0], 1)
        np.testing.assert_allclose(motion[..., 1], 0)

    def test_reversed_viewport(self):
        self.viewport[4:] = [-1, 1]
        motion, _, _ = reproject(self.depth, self.identity, self.identity, self.viewport, self.viewport)
        np.testing.assert_allclose(motion, 0)

    def test_depth_dependent_motion_uses_device_depth_convention(self):
        previous = self.identity.copy()
        previous[0, 2] = 1  # x_previous = x_current + z_clip
        forward, _, _ = reproject(self.depth, self.identity, previous, self.viewport, self.viewport)
        np.testing.assert_allclose(forward[..., 0], .4)  # sx * .2
        self.viewport[4:] = [-1, 1]
        reverse, _, _ = reproject(self.depth, self.identity, previous, self.viewport, self.viewport)
        np.testing.assert_allclose(reverse[..., 0], 1.6)  # sx * (1-.2)

    def test_invalid_depth(self):
        self.depth[1, 1] = np.nan
        _, _, valid = reproject(self.depth, self.identity, self.identity, self.viewport, self.viewport)
        self.assertFalse(valid[1, 1])


if __name__ == '__main__':
    unittest.main()
