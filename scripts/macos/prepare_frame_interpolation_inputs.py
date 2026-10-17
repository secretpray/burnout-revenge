#!/usr/bin/env python3
"""Prepare *experimental* camera-only inputs for the standalone MetalFX probe.

Requires numpy/Pillow and validated capture summaries. Motion is recomputed
from this exact pair, never borrowed from another analysis in the same folder.
Never used by the game; simulation delta must be stated explicitly.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from probe_camera_motion import load_frame, reproject
from probe_native_motion import decode_motion, stitch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('previous', type=Path)
    parser.add_argument('current', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--simulation-delta', type=float, required=True,
                        help='explicit hypothesis in seconds; diagnostic host readback time is not simulation time')
    parser.add_argument('--motion-source', choices=['camera','native','history'], default='camera')
    args = parser.parse_args()
    if not np.isfinite(args.simulation_delta) or not 0 < args.simulation_delta <= 1:
        parser.error('use a finite simulation delta in (0,1]')
    args.output.mkdir(parents=True, exist_ok=True)
    info = {'assumed_simulation_delta': args.simulation_delta, 'sdk_ready': False,
            'motion_source': args.motion_source,
            'camera_only_motion': args.motion_source=='camera',
            'camera_parameters': 'inferred from dominant matrix, not captured engine camera state'}
    frames, cameras = [], []
    history_meta = []
    for label, path in zip(('previous', 'current'), (args.previous, args.current)):
        summary, _, matrix, viewport, _, depth = load_frame(path)
        cameras.append((matrix, viewport, depth))
        frames.append(summary['header']['frame'])
        height, width = depth.shape
        if 'width' in info and (info['width'], info['height']) != (width, height):
            raise ValueError('input size changed')
        info.update(width=width, height=height)
        events = [json.loads(line) for line in (path/'events.jsonl').read_text().splitlines()]
        if args.motion_source=='history':
            meta=[e for e in events if e['type']=='history_meta']
            if len(meta)!=1 or meta[0]['frame']!=frames[-1]:
                raise ValueError('missing/mismatched GPU history frame metadata')
            history_meta.append(meta[0])
        for kind, name in [('pre-screen-2d-candidate', label+'.rgba'), ('final-front', label+'-ui.rgba')]:
            if args.motion_source=='history':
                kind='history-scene' if kind=='pre-screen-2d-candidate' else 'history-ui'
            choices = [e for e in events if e['type']=='image' and e['kind']==kind
                       and e['format']==(97 if args.motion_source=='history' else 37)
                       and (e['width'], e['height'])==(width, height)
                       and e['x']==0 and e['y']==0]
            if len(choices) != 1:
                raise ValueError(f'ambiguous or unsupported {kind} color')
            event = choices[0]
            if args.motion_source=='history':
                pixels=np.fromfile(path/event['file'],dtype='<f2').reshape(height,width,4)
                if not np.isfinite(pixels).all(): raise ValueError('nonfinite GPU history colour')
                rgba=bytearray(np.rint(np.clip(pixels.astype(float),0,1)*255).astype(np.uint8).tobytes())
            else:
                rgba = bytearray((path/event['file']).read_bytes())
            if len(rgba) != width*height*4:
                raise ValueError('color byte count mismatch')
            # Xenos uses zero alpha in the opaque scene target, then one
            # after the final composite. Preserve RGB, normalize opacity
            # consistently before testing composited UI in MetalFX.
            rgba[3::4] = b'\xff'*(width*height)
            (args.output/name).write_bytes(rgba)
        if args.motion_source=='history':
            event=next(e for e in events if e['type']=='image' and e['kind']=='history-depth' and e['format']==100)
            saved=np.fromfile(path/event['file'],dtype='<f4').reshape(height,width)
            if not np.array_equal(saved,depth): raise ValueError('GPU history depth disagrees with source')
            depth=saved
        depth.astype('<f4').tofile(args.output/(label+'-depth.f32'))
        sx, sy, sw = (np.linalg.norm(matrix[i, :3]) for i in (0, 1, 3))
        view = np.eye(4)
        view[0], view[1], view[2] = matrix[0]/sx, matrix[1]/sy, matrix[3]/sw
        alpha = np.dot(matrix[2, :3], matrix[3, :3])/np.dot(matrix[3, :3], matrix[3, :3])
        beta = matrix[2, 3]-alpha*matrix[3, 3]
        projection = np.zeros((4, 4))
        projection[0, 0], projection[1, 1], projection[2, 2], projection[2, 3], projection[3, 2] = sx, sy, alpha*sw, beta, sw
        if not np.allclose(projection@view, matrix, atol=1e-6, rtol=0):
            raise ValueError('matrix is not the expected perspective camera candidate')
        info[label+'_view'], info[label+'_projection'] = view.tolist(), projection.tolist()
        info.update(near=float(-beta/alpha), far=float(-beta/(alpha-1)),
                    fov_y=float(np.degrees(2*np.arctan(1/sy))))
        if not all(np.isfinite(info[k]) for k in ('near', 'far', 'fov_y')) or not 0 < info['near'] < info['far']:
            raise ValueError('invalid inferred camera planes')
    if frames[1] != frames[0]+1:
        raise ValueError('non-adjacent frame IDs')
    info['frames'] = frames
    if args.motion_source=='history':
        if history_meta[0]['epoch']!=history_meta[1]['epoch'] or not history_meta[1]['pair']:
            raise ValueError('GPU history epoch gap or reset between selected frames')
        info['history_meta']=history_meta
    previous_matrix, previous_viewport, _ = cameras[0]
    current_matrix, current_viewport, current_depth = cameras[1]
    if args.motion_source=='camera':
        flow, _, valid = reproject(current_depth, current_matrix, previous_matrix,
                                   current_viewport, previous_viewport)
    elif args.motion_source=='native':
        encoded, fmt = stitch(args.current,1024,(37,91,97))
        flow = decode_motion(encoded,width,height)
        valid = current_depth>0
        info['encoded_format'] = fmt
        info['motion_limitations'] = 'Game velocity shader extrapolation branch for receding objects not yet validated'
    else:
        event = next(e for e in events if e['type']=='image' and e['kind']=='history-motion' and e['format']==83)
        flow = np.fromfile(args.current/event['file'],dtype='<f2').reshape(height,width,2)
        valid = current_depth>0
        info['motion_limitations'] = 'GPU history; no live MetalFX consumer or quality acceptance for every camera'
        info['true_motion_source'] = any(e['type']=='image' and e['kind']=='true-motion-source' for e in events)
    flow[~valid] = 0  # explicit test-only fallback; not a validated full motion field
    if not np.isfinite(flow).all() or np.max(np.abs(flow)) > 65504:
        raise ValueError('motion cannot be represented in float16')
    flow.astype('<f2').tofile(args.output/'motion-camera-only.f16')
    (args.output/'inputs.json').write_text(json.dumps(info, indent=2)+'\n')
    print(args.output)


if __name__ == '__main__':
    main()
