#!/usr/bin/env python3
"""Reproducible offline MetalFX half-frame control (numpy, no game data).

Creates a textured plane at z=1 and a camera shift of 16 pixels. Runs the
standalone native probe and checks phase, rather than accepting API success
or a change of alpha as evidence of frame interpolation. Requires macOS/GPU.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import numpy as np


def pattern(width, height, shift):
    y, x = np.mgrid[:height, :width]
    x = x-shift
    rgb = np.stack((128+57*np.sin(x/23)+31*np.cos(y/37),
                    125+61*np.cos(x/31)+29*np.sin(y/29),
                    124+59*np.sin(x/19+y/41)), axis=-1)
    rgba = np.full((height, width, 4), 255, dtype=np.uint8)
    rgba[..., :3] = np.rint(np.clip(rgb, 0, 255)).astype(np.uint8)
    return rgba


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path, help='compiled probe_frame_interpolation binary')
    parser.add_argument('output', type=Path, help='dedicated diagnostic output folder')
    args = parser.parse_args()
    width, height, shift = 1280, 720, 16
    args.output.mkdir(parents=True, exist_ok=True)
    near, far = .1, 100.
    projection = np.zeros((4, 4))
    projection[0, 0], projection[1, 1] = 1, width/height
    projection[2, 2], projection[2, 3] = far/(far-near), -near*far/(far-near)
    projection[3, 2] = 1
    previous_view = np.eye(4)
    current_view = previous_view.copy()
    current_view[0, 3] = shift/(width/2)
    info = dict(width=width, height=height, assumed_simulation_delta=1/60,
                near=near, far=far, fov_y=float(np.degrees(2*np.arctan(height/width))),
                previous_view=previous_view.tolist(), current_view=current_view.tolist(),
                previous_projection=projection.tolist(), current_projection=projection.tolist(),
                synthetic_control=True)
    (args.output/'inputs.json').write_text(json.dumps(info, indent=2)+'\n')
    # Viewport reverses the conventional perspective depth.
    device_depth = 1-(projection[2, 2]+projection[2, 3])
    depth = np.full((height, width), device_depth, dtype='<f4')
    for label, offset in [('previous', 0), ('current', shift)]:
        color = pattern(width, height, offset).tobytes()
        (args.output/(label+'.rgba')).write_bytes(color)
        (args.output/(label+'-ui.rgba')).write_bytes(color)
        depth.tofile(args.output/(label+'-depth.f32'))
    motion = np.zeros((height, width, 2), dtype='<f2')
    motion[..., 0] = -shift  # current -> previous
    motion.tofile(args.output/'motion-camera-only.f16')
    result = subprocess.run([str(args.probe.resolve()), str(args.output.resolve()),
                             '--float16', '--no-ui'], env={**os.environ, 'MTL_DEBUG_LAYER': '0'},
                            text=True, capture_output=True, timeout=60)
    (args.output/'native-probe.log').write_text(result.stdout+result.stderr)
    if result.returncode:
        raise SystemExit(result.returncode)
    report = json.loads((args.output/'result-rgba16-no-ui-matrices.json').read_text())
    raw = (args.output/report['output_file']).read_bytes()
    actual = np.frombuffer(raw, dtype=np.uint8).reshape(height, width, 4)
    # Exclude disoccluded borders; this verifies a textured plane, not the
    # algorithm's handling of object motion, UI or newly revealed content.
    region = np.s_[48:-48, 96:-96, :3]
    errors = {k: float(np.abs(actual[region].astype(float)-
                            pattern(width, height, k)[region]).mean())
              for k in range(shift+1)}
    best = min(errors, key=errors.get)
    check = dict(source_shift_px=shift, best_output_shift_px=best,
                 mean_errors_by_shift=errors, region='y48:-48 x96:-96 RGB',
                 passed=bool(best == shift//2 and errors[best] < 1
                             and errors[best] < min(errors[0], errors[shift])*.1),
                 note='Offline synthetic plane only; not a game performance or quality acceptance test.')
    (args.output/'phase-check.json').write_text(json.dumps(check, indent=2)+'\n')
    print(json.dumps(check, indent=2))
    return 0 if check['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
