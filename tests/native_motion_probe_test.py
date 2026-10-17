"""Analytic tests for the observed split-sign MRT contract; requires numpy/Pillow."""
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/macos'))
from probe_native_motion import decode_motion


class NativeMotionProbeTest(unittest.TestCase):
    def test_stationary(self):
        image=np.tile([1,0,1,0],(3,4,1))
        np.testing.assert_array_equal(decode_motion(image,1280,720),0)

    def test_sign_and_pixel_units(self):
        motion=decode_motion(np.array([[[1,.01,.98,0]]]),1280,720)
        np.testing.assert_allclose(motion,[[[12.8,-14.4]]],atol=.0001)

    def test_unorm16_keeps_subpixel_motion(self):
        uv=np.array([.0003,-.0003])
        encoded=np.array([[[1,uv[0],1+uv[1],0]]])
        rgba8=np.rint(encoded*255)/255
        rgba16=np.rint(encoded*65535)/65535
        target=uv*[1280,720]
        self.assertGreater(np.max(np.abs(decode_motion(rgba8,1280,720)-target)),.2)
        self.assertLess(np.max(np.abs(decode_motion(rgba16,1280,720)-target)),.02)

    def test_invalid_data_rejected(self):
        with self.assertRaises(ValueError): decode_motion(np.full((1,1,4),np.nan),1280,720)
        with self.assertRaises(ValueError): decode_motion(np.zeros((2,2,3)),1280,720)


if __name__=='__main__': unittest.main()
