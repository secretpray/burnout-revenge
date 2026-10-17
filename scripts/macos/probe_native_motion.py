#!/usr/bin/env python3
"""Decode Burnout's own split-sign velocity MRT; never infer object IDs by draw.

The game's shaders put min(1+vx,1), max(vx,0), min(1+vy,1), max(vy,0)
in RGBA. v points current -> previous in UV coordinates for forward motion.
The reverse-depth-motion branch extrapolates the previous view: it must be
validated separately before accepting general camera cuts/replays.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image
from probe_camera_motion import load_frame, reproject, bilinear


def decode_motion(encoded, width, height):
    encoded = np.asarray(encoded,dtype=np.float32)
    if encoded.ndim != 3 or encoded.shape[2] != 4 or not np.isfinite(encoded).all():
        raise ValueError('expected finite split-sign RGBA velocity')
    return (encoded[..., (0, 2)]+encoded[..., (1, 3)]-1)*[width, height]


def stitch(path, base, formats):
    events = [json.loads(line) for line in (path/'events.jsonl').read_text().splitlines()]
    images = [e for e in events if e['type']=='image' and e['kind']=='resolve-color'
              and e['base']==base and e['height']<720 and e['format'] in formats]
    if not images or len({e['format'] for e in images}) != 1:
        raise ValueError('missing or ambiguous tiled source')
    width, height = images[0]['width'], max(e['y']+e['height'] for e in images)
    result = np.zeros((height, width, 4), dtype=np.float32)
    coverage = np.zeros((height, width), dtype=np.uint8)
    for event in images:
        dtype = '<f2' if event['format']==97 else '<u2' if event['format']==91 else np.uint8
        pixels = np.fromfile(path/event['file'], dtype=dtype).reshape(event['height'], event['width'], 4)
        if dtype == np.uint8:
            pixels = pixels.astype(np.float32)/255
        elif dtype == '<u2':
            pixels = pixels.astype(np.float32)/65535
        x, y, w, h = (event[k] for k in ('x','y','width','height'))
        result[y:y+h, x:x+w] = pixels
        coverage[y:y+h, x:x+w] += 1
    if not np.all(coverage==1):
        raise ValueError('holes or overlap in tiled source')
    return result, images[0]['format']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('previous', type=Path)
    parser.add_argument('current', type=Path)
    args = parser.parse_args()
    a, _, previous, pvp, before, _ = load_frame(args.previous)
    b, _, current, vp, after, depth = load_frame(args.current)
    post_before, post_after = before.astype(float), after.astype(float)
    if b['header']['frame'] != a['header']['frame']+1:
        raise ValueError('expected adjacent frames')
    encoded, fmt = stitch(args.current, 1024, (37,91,97))
    height, width = depth.shape
    if encoded.shape[:2] != (height, width):
        raise ValueError('velocity/depth extent mismatch')
    flow = decode_motion(encoded, width, height)
    camera, _, _ = reproject(depth, current, previous, vp, pvp)
    y, x = np.mgrid[:height, :width]
    coords = np.stack((x+.5, y+.5), axis=-1)
    valid = (np.isfinite(flow).all(axis=-1) & (coords+flow >= .5).all(axis=-1)
             & ((coords+flow) < [width-.5,height-.5]).all(axis=-1))
    report = dict(frames=[a['header']['frame'], b['header']['frame']], encoded_format=fmt,
                  motion_source='game MRT b1024 / per-object shader transforms',
                  motion_direction='current-to-previous pixels; forward-motion branch',
                  geometric_valid_fraction=float(valid.mean()), sdk_ready=False, rois={})
    for stage in ('source-before-post', 'post-before-ui'):
        if stage=='source-before-post':
            before = stitch(args.previous, 0, (37,))[0][..., :3]*255
            after = stitch(args.current, 0, (37,))[0][..., :3]*255
        else:
            before, after = post_before, post_after
        warped = bilinear(before.astype(float), coords+flow)
        cam_warped = bilinear(before.astype(float), coords+camera)
        errors = dict(unwarped=np.abs(after-before).mean(-1),
                      camera=np.abs(after-cam_warped).mean(-1),
                      native=np.abs(after-warped).mean(-1))
        report['rois'][stage] = {}
        for name, (x0,y0,x1,y1) in dict(left_wall=(40,100,400,500),right_wall=(900,100,1240,500),
                                       road=(50,570,1230,700),car=(450,350,850,570),
                                       car_inner=(510,390,770,510)).items():
            region = np.s_[y0:y1,x0:x1]
            mask = valid[region]
            report['rois'][stage][name] = {k:float(v[region][mask].mean()) for k,v in errors.items()}
        Image.fromarray(np.clip(warped,0,255).astype(np.uint8)).save(args.current.parent/(stage+'-native-warp.png'))
    np.save(args.current.parent/'motion-native.npy', flow.astype(np.float32))
    np.save(args.current.parent/'motion-native-validity.npy', valid)
    (args.current.parent/'native-motion.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
