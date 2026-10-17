#!/usr/bin/env python3
"""Validate actual GPU history against independently captured source targets.

Requires numpy/Pillow. Verifies RGB/alpha/depth and the native/true-motion
conversion, then reports photometric errors without claiming SDK readiness.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from probe_camera_motion import load_frame, reproject, bilinear
from probe_native_motion import stitch, decode_motion


def image(path, kind, fmt, channels, dtype):
    events=[json.loads(line) for line in (path/'events.jsonl').read_text().splitlines()]
    matches=[e for e in events if e['type']=='image' and e['kind']==kind and e['format']==fmt]
    if len(matches)!=1: raise ValueError(f'{path}: expected one {kind}')
    event=matches[0]
    return np.fromfile(path/event['file'],dtype=dtype).reshape(event['height'],event['width'],channels)


def inspect(path):
    summary,_,matrix,viewport,color,depth=load_frame(path)
    scene=image(path,'history-scene',97,4,'<f2')
    ui=image(path,'history-ui',97,4,'<f2')
    saved_depth=image(path,'history-depth',100,1,'<f4')[...,0]
    motion=image(path,'history-motion',83,2,'<f2').astype(np.float32)
    front=image(path,'final-front',37,4,np.uint8)
    height,width=depth.shape
    encoded,fmt=stitch(path,1024,(37,91,97))
    expected=decode_motion(encoded,width,height)
    valid_fraction=None
    true=None
    try: true=image(path,'true-motion-source',97,4,'<f2').astype(np.float32)
    except ValueError: pass  # Older probe captured the original encoded-only producer.
    if true is not None:
        valid=true[...,2]>.5
        expected[valid]=true[...,:2][valid]*[width,height]
        valid_fraction=float(valid[depth>0].mean())
    expected[depth<=0]=0
    quantized=expected.astype(np.float16).astype(np.float32)
    rgb=lambda value: np.rint(np.clip(value[...,:3].astype(float),0,1)*255).astype(np.uint8)
    checks=dict(scene_rgb_equal=bool(np.array_equal(rgb(scene),color)),
                ui_rgb_equal=bool(np.array_equal(rgb(ui),front[...,:3])),
                opaque_alpha=bool(np.all(scene[...,3]==1) and np.all(ui[...,3]==1)),
                depth_bit_equal=bool(np.array_equal(depth,saved_depth)),
                finite_motion=bool(np.isfinite(motion).all()),
                motion_conversion_matches=bool(np.allclose(motion,quantized,rtol=.001,atol=.002)))
    report=dict(frame=summary['header']['frame'],width=width,height=height,checks=checks,
                conversion_max_error_px=float(np.max(np.abs(motion-quantized))),
                true_motion_geometry_coverage=valid_fraction,encoded_format=fmt,
                passed=all(checks.values()),sdk_ready=False)
    (path/'history-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    return report,matrix,viewport,color,depth,motion


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('previous',type=Path)
    parser.add_argument('current',type=Path)
    args=parser.parse_args()
    a=inspect(args.previous);b=inspect(args.current)
    if b[0]['frame']!=a[0]['frame']+1: raise ValueError('non-adjacent frames')
    height,width=b[4].shape
    y,x=np.mgrid[:height,:width]
    grid=np.stack((x+.5,y+.5),axis=-1)
    motion=b[5]
    camera,_,_=reproject(b[4],b[1],a[1],b[2],a[2])
    rois={}
    for stage in ('source-before-post','post-before-ui'):
        before=stitch(args.previous,0,(37,))[0][...,:3]*255 if stage=='source-before-post' else a[3].astype(float)
        after=stitch(args.current,0,(37,))[0][...,:3]*255 if stage=='source-before-post' else b[3].astype(float)
        errors=dict(unwarped=np.abs(after-before).mean(-1),
                    camera=np.abs(after-bilinear(before,grid+camera)).mean(-1),
                    history=np.abs(after-bilinear(before,grid+motion)).mean(-1))
        rois[stage]={}
        for name,(x0,y0,x1,y1) in dict(left_wall=(40,100,400,500),right_wall=(900,100,1240,500),
                                      road=(50,570,1230,700),car=(450,350,850,570),car_inner=(510,390,770,510)).items():
            region=np.s_[y0:y1,x0:x1]
            coords=(grid+motion)[region]
            valid=(coords>=.5).all(-1)&(coords<[width-.5,height-.5]).all(-1)&(b[4][region]>0)
            rois[stage][name]={k:float(v[region][valid].mean()) for k,v in errors.items()}
    report=dict(frames=[a[0],b[0]],rois=rois,passed=a[0]['passed'] and b[0]['passed'],
                live_fg=False,note='Fixed ROIs are diagnostics, not object segmentation or full game quality acceptance.')
    (args.current.parent/'history-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__': raise SystemExit(main())
