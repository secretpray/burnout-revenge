#!/usr/bin/env python3
"""Offline camera-only reprojection from temporal captures (requires numpy/Pillow).

Diagnostic hypothesis, never an SDK-ready motion texture. Moving objects,
occlusion changes and world-space labels need separate treatment in the game.
"""
import argparse
import json
from pathlib import Path
import struct
import numpy as np
from PIL import Image, ImageDraw


def reproject(depth, current, previous, viewport, previous_viewport):
    """Current device-depth -> previous pixels; motion points current -> previous.

    Matrices are row-major clip-from-world, viewport=(sx,ox,sy,oy,sz,oz).
    Pixel coordinates use centers (x+.5,y+.5), with Vulkan's top-left origin.
    """
    height, width = depth.shape
    y, x = np.mgrid[:height, :width]
    sx, ox, sy, oy, sz, oz = viewport
    if sx == 0 or sy == 0 or sz == 0:
        raise ValueError('degenerate viewport')
    clip = np.stack(((x+.5-ox)/sx, (y+.5-oy)/sy, (depth-oz)/sz, np.ones_like(depth)), axis=-1)
    # Homogeneous world scale cancels when the previous clip is divided by W.
    prev_clip = clip @ (previous @ np.linalg.inv(current)).T
    with np.errstate(divide='ignore', invalid='ignore'):
        prev_ndc = prev_clip[..., :3] / prev_clip[..., 3:4]
    psx, pox, psy, poy, _, _ = previous_viewport
    coords = prev_ndc[..., :2] * [psx, psy] + [pox, poy]
    motion = coords - np.stack((x+.5, y+.5), axis=-1)
    valid = (np.isfinite(depth) & (depth > 0) & (depth <= 1)
             & np.isfinite(coords).all(axis=-1) & (prev_clip[..., 3] > 0)
             & (coords[..., 0] >= .5) & (coords[..., 0] < width-.5)
             & (coords[..., 1] >= .5) & (coords[..., 1] < height-.5))
    return motion, coords, valid


def load_frame(path):
    summary = json.loads((path / 'summary.json').read_text())
    if summary['errors']:
        raise ValueError(f'{path}: capture errors')
    events = [json.loads(line) for line in (path / 'events.jsonl').read_text().splitlines()]
    candidate = summary['common_c0_c3_matrix_candidates'][0]
    viewport = next(e for e in events if e['type'] == 'viewport' and e['draw'] == candidate['representative_draw'])
    # This prototype accepts only ordinary perspective clip coordinates and
    # a full viewport. Packed/pre-divided guest positions require another path.
    if viewport['vte'] & 0x3f != 0x3f or viewport['vte'] & 0x300:
        raise ValueError('unsupported guest viewport/position convention')
    vp = list(struct.unpack('<6f', struct.pack('<6I', *viewport['bits'])))
    # Renderer.cpp adds .5 to D3DZero viewport offsets; untiled rendering
    # cancels the tile window offset (TileShift), so don't add it again.
    if viewport['vertex_control'] & 1 == 0:
        vp[1] += .5
        vp[3] += .5
    image = next(e for e in events if e['type'] == 'image' and e['kind'] == 'pre-screen-2d-candidate'
                 and e['width'] >= 640 and e['height'] >= 360 and e['format'] == 37)
    width, height = image['width'], image['height']
    if image['x'] or image['y']:
        raise ValueError('cropped color candidate')
    color = np.fromfile(path / image['file'], dtype=np.uint8).reshape(height, width, 4)[..., :3]
    tiles = [e for e in events if e['type'] == 'image' and e['kind'] == 'resolve-depth' and e['width'] == width]
    if not tiles or len({e['base'] for e in tiles}) != 1:
        raise ValueError('ambiguous main depth candidates')
    depth = np.full((height, width), np.nan, dtype=np.float32)
    coverage = np.zeros((height, width), dtype=np.uint8)
    for tile in tiles:
        x, y, w, h = (tile[k] for k in ('x', 'y', 'width', 'height'))
        if x < 0 or y < 0 or x+w > width or y+h > height:
            raise ValueError('depth outside color extent')
        depth[y:y+h, x:x+w] = np.fromfile(path / tile['file'], dtype='<f4').reshape(h, w)
        coverage[y:y+h, x:x+w] += 1
    if not np.all(coverage == 1):
        raise ValueError('depth overlap or holes')
    return summary, candidate, np.array(candidate['rows']), np.array(vp), color, depth


def bilinear(image, coordinates):
    height, width = image.shape[:2]
    coords = np.nan_to_num(coordinates, nan=0, posinf=0, neginf=0)
    # Array index 0 is the pixel centered at .5.
    x = np.clip(coords[..., 0]-.5, 0, width-1.001)
    y = np.clip(coords[..., 1]-.5, 0, height-1.001)
    ix, iy = x.astype(int), y.astype(int)
    fx, fy = (x-ix)[..., None], (y-iy)[..., None]
    return ((1-fy)*((1-fx)*image[iy, ix]+fx*image[iy, ix+1])
            + fy*((1-fx)*image[iy+1, ix]+fx*image[iy+1, ix+1]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('previous', type=Path)
    parser.add_argument('current', type=Path)
    args = parser.parse_args()
    a, ac, prev, pvp, before, _ = load_frame(args.previous)
    b, bc, current, vp, after, depth = load_frame(args.current)
    if b['header']['frame'] != a['header']['frame']+1 or before.shape != after.shape:
        raise ValueError('expected adjacent frames of the same extent')
    flow, coordinates, valid = reproject(depth, current, prev, vp, pvp)
    warped = bilinear(before.astype(float), coordinates)
    error_before = np.abs(after.astype(float)-before).mean(axis=-1)
    error_after = np.abs(after.astype(float)-warped).mean(axis=-1)
    rois = {'left_wall': (40, 100, 400, 500), 'right_wall': (900, 100, 1240, 500),
            'road': (50, 570, 1230, 700), 'car_dynamic_unhandled': (450, 350, 850, 570)}
    result = {'frames': [a['header']['frame'], b['header']['frame']],
              'matrix_candidate_counts': [ac['count'], bc['count']],
              'viewport': vp.tolist(), 'motion_direction': 'current-to-previous pixels',
              'geometric_valid_fraction': float(valid.mean()), 'rois': {},
              'sdk_ready': False, 'dynamic_object_motion': False,
              'note': 'Dominant matrix is a hypothesis; photometric comparison does not validate object motion or all camera types.'}
    for name, (x0, y0, x1, y1) in rois.items():
        mask = valid[y0:y1, x0:x1]
        result['rois'][name] = {'mad_unwarped': float(error_before[y0:y1, x0:x1][mask].mean()),
                                'mad_camera_reprojected': float(error_after[y0:y1, x0:x1][mask].mean()),
                                'valid_pixels': int(mask.sum())}
    out = args.current.parent
    np.save(out / 'motion-camera-only.npy', flow.astype(np.float32))
    np.save(out / 'motion-geometric-validity.npy', valid)
    (out / 'camera-motion.json').write_text(json.dumps(result, indent=2) + '\n')
    preview = Image.fromarray(after)
    draw = ImageDraw.Draw(preview)
    for y in range(40, depth.shape[0], 60):
        for x in range(40, depth.shape[1], 60):
            if valid[y, x]:
                dx, dy = flow[y, x]
                draw.line((x, y, x+dx*4, y+dy*4), fill=(255, 70, 20), width=2)
    draw.rectangle((0, 0, 1030, 22), fill='black')
    draw.text((8, 5), 'CAMERA ONLY / arrows x4 / dynamic cars incorrect / NOT SDK-ready', fill='white')
    preview.save(out / 'motion-camera-only.png')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
