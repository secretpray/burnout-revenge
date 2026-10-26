#!/usr/bin/env python3
"""Offline 30->60 render-pose experiment on immutable Burnout pacing samples.

No game invocation or guest writes. Midpoints are pose candidates, not rendered
images. They require the later endpoint: a live implementation would need an
owned second render pass and at least a half-source-period of look-back.
"""
import argparse
import bisect
import hashlib
import json
import math
from pathlib import Path

from analyze_crash_pacing import rows


def vector(row, keys):
    return tuple(float(row[k]) for k in keys)


def length(v):
    return math.sqrt(sum(x*x for x in v))


def unit(v):
    n = length(v)
    if not math.isfinite(n) or n < 1e-8:
        raise ValueError('invalid direction/orientation')
    return tuple(x/n for x in v)


def distance(a, b):
    return length(tuple(x-y for x, y in zip(a, b)))


def lerp(a, b, t):
    return tuple(x+(y-x)*t for x, y in zip(a, b))


def slerp(a, b, t):
    a, b = unit(a), unit(b)
    dot = sum(x*y for x, y in zip(a, b))
    if dot < 0:
        b, dot = tuple(-x for x in b), -dot
    dot = min(1., max(0., dot))
    if dot > .9995:
        return unit(lerp(a, b, t))
    angle = math.acos(dot)
    wa, wb = math.sin((1-t)*angle)/math.sin(angle), math.sin(t*angle)/math.sin(angle)
    return unit(tuple(wa*x+wb*y for x, y in zip(a, b)))


def angle_degrees(a, b, quaternion=False):
    dot = sum(x*y for x, y in zip(unit(a), unit(b)))
    if quaternion:
        dot = abs(dot)
    return (2 if quaternion else 1)*math.degrees(math.acos(min(1., max(-1., dot))))


def matrix_quaternion(row):
    # RenderWare world matrix: right, up, forward, position; flags/padding
    # at indices 3/7/11/15 are not interpolated as floating point data.
    axes = [vector(row, tuple('m'+str(i+j) for j in range(3))) for i in (0, 4, 8)]
    if not all(.9 <= length(a) <= 1.1 for a in axes):
        raise ValueError('invalid vehicle scale')
    axes = [unit(a) for a in axes]
    if any(abs(sum(x*y for x, y in zip(axes[i], axes[j]))) > .02 for i, j in ((0, 1), (0, 2), (1, 2))):
        raise ValueError('nonorthogonal vehicle axes')
    r, u, f = axes
    cross = (r[1]*u[2]-r[2]*u[1], r[2]*u[0]-r[0]*u[2], r[0]*u[1]-r[1]*u[0])
    if sum(x*y for x, y in zip(cross, f)) < .98:
        raise ValueError('reflected vehicle axes')
    m = tuple(tuple(axes[c][r] for c in range(3)) for r in range(3))
    trace = m[0][0]+m[1][1]+m[2][2]
    if trace > 0:
        s = math.sqrt(trace+1)*2
        q = ((m[2][1]-m[1][2])/s, (m[0][2]-m[2][0])/s, (m[1][0]-m[0][1])/s, s/4)
    else:
        i = max(range(3), key=lambda i: m[i][i])
        j, k = (i+1)%3, (i+2)%3
        s = math.sqrt(max(0., 1+m[i][i]-m[j][j]-m[k][k]))*2
        if s < 1e-8:
            raise ValueError('invalid rotation')
        q = [0.]*4
        q[i], q[j], q[k], q[3] = s/4, (m[j][i]+m[i][j])/s, (m[k][i]+m[i][k])/s, (m[k][j]-m[j][k])/s
    return unit(q)


def pose(row):
    # Native frame: FOV at +16, quaternion +32..44, position +48..56.
    # Participant: position +64..72; legacy traces use +80..88 as direction.
    # New traces take orientation from the verified complete draw matrix.
    result = {
        'car_position': vector(row, ('car_x', 'car_y', 'car_z')),
        'car_forward': vector(row, ('car_dx', 'car_dy', 'car_dz')),
        'camera_position': vector(row, ('c8', 'c9', 'c10')),
        'camera_quaternion': vector(row, ('c4', 'c5', 'c6', 'c7')),
    }
    if 'm0' in row:
        result['car_quaternion'] = matrix_quaternion(row)
        result['car_forward'] = vector(row, ('m8', 'm9', 'm10'))
    if not all(math.isfinite(x) for v in result.values() for x in v):
        raise ValueError('nonfinite pose')
    for name in ('car_forward', 'camera_quaternion'):
        if not .9 <= length(result[name]) <= 1.1:
            raise ValueError('invalid direction/orientation')
    return result


def midpoint(a, b):
    """Fail closed on discontinuities; never infer velocity across a reset."""
    for key in ('phase', 'event_state', 'playback', 'camera_frame', 'camera_mode'):
        if a.get(key) != b.get(key):
            return None, 'context_change'
    if a['phase'] != '5' or a['event_state'] != '18' or a['playback'] != '0':
        return None, 'outside_live_crash'
    if a['clock_flags'] != str(0x01000000) or b['clock_flags'] != str(0x01000000):
        return None, 'clock_not_live'
    gap = (int(b['steady_ns'])-int(a['steady_ns']))/1e9
    dt = float(b['world_s'])-float(a['world_s'])
    if not 0 < gap <= .1:
        return None, 'gap_or_order'
    if not 0 < dt <= .1:
        return None, 'paused_or_rewound'
    try:
        pa, pb = pose(a), pose(b)
        if distance(pa['car_position'], pb['car_position']) > 8 or distance(pa['camera_position'], pb['camera_position']) > 8:
            return None, 'translation_cut'
        if angle_degrees(pa['camera_quaternion'], pb['camera_quaternion'], True) > 45:
            return None, 'camera_turn_cut'
        if 'car_quaternion' in pa and angle_degrees(pa['car_quaternion'], pb['car_quaternion'], True) > 45:
            return None, 'car_turn_cut'
        if angle_degrees(pa['car_forward'], pb['car_forward']) > 45:
            return None, 'car_turn_cut'
        result = {k: lerp(pa[k], pb[k], .5) for k in pa}
        result['car_forward'] = unit(result['car_forward'])
        for name in ('camera_quaternion', 'car_quaternion'):
            if name in pa:
                result[name] = slerp(pa[name], pb[name], .5)
        return result, None
    except ValueError:
        return None, 'invalid_pose'


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    return {'n': len(values), **{name: values[max(0, math.ceil(p*len(values))-1)]
                                for name, p in [('p50', .5), ('p95', .95), ('p99', .99)]}, 'max': values[-1]}


def experiment(trace, start, end):
    chosen = [r for r in trace if start <= float(r['session_s']) < end]
    # Prefer the actual camera render consumer; old traces can only test the
    # main-loop-boundary hypothesis, clearly identified in the report.
    kind = 'render_camera' if any(r['kind'] == 'render_camera' for r in chosen) else 'boundary'
    frames = [r for r in chosen if r['kind'] == kind]
    native_kind = 'native_update_end' if any(r['kind'] == 'native_update_end' for r in chosen) else None
    native = [r for r in chosen if r['kind'] == native_kind]
    native_times = [int(r['steady_ns']) for r in native]
    generated, rejected, errors = [], {}, {}
    lookback, wall_gaps = [], []
    # Input data stays byte-for-byte the same after generating candidates.
    before = hashlib.sha256(json.dumps(trace, sort_keys=True).encode()).hexdigest()
    for a, b in zip(frames, frames[1:]):
        mid, reason = midpoint(a, b)
        if reason:
            rejected[reason] = rejected.get(reason, 0)+1
            continue
        at = (int(a['steady_ns'])+int(b['steady_ns']))//2
        generated.append({'sample_ns': at, 'available_after_ns': int(b['steady_ns']),
                          'world_s': (float(a['world_s'])+float(b['world_s']))/2, **mid})
        lookback.append((int(b['steady_ns'])-at)/1e6)
        wall_gaps.append((int(b['steady_ns'])-int(a['steady_ns']))/1e6)
        # Compare only to native samples truly inside the two endpoints and
        # near the midpoint of game time; phase/time discontinuities excluded.
        lo, hi = bisect.bisect_right(native_times, int(a['steady_ns'])), bisect.bisect_left(native_times, int(b['steady_ns']))
        inside = [n for n in native[lo:hi]
                  if all(n.get(k) == a.get(k) for k in
                         ('phase', 'event_state', 'playback', 'camera_frame', 'camera_mode', 'clock_flags'))
                  and float(a['world_s']) < float(n['world_s']) < float(b['world_s'])]
        if not inside:
            continue
        target = generated[-1]['world_s']
        n = min(inside, key=lambda n: abs(float(n['world_s'])-target))
        dt = float(b['world_s'])-float(a['world_s'])
        if abs(float(n['world_s'])-target) > dt*.15:
            continue
        try:
            pn, pa = pose(n), pose(a)
        except ValueError:
            continue
        for key in ('car_position', 'camera_position'):
            errors.setdefault(key+'_interpolated', []).append(distance(mid[key], pn[key]))
            errors.setdefault(key+'_held', []).append(distance(pa[key], pn[key]))
        if 'car_quaternion' in mid:
            errors.setdefault('car_angle_interpolated_deg', []).append(angle_degrees(mid['car_quaternion'], pn['car_quaternion'], True))
            errors.setdefault('car_angle_held_deg', []).append(angle_degrees(pa['car_quaternion'], pn['car_quaternion'], True))
        errors.setdefault('camera_angle_interpolated_deg', []).append(angle_degrees(mid['camera_quaternion'], pn['camera_quaternion'], True))
        errors.setdefault('camera_angle_held_deg', []).append(angle_degrees(pa['camera_quaternion'], pn['camera_quaternion'], True))
    assert before == hashlib.sha256(json.dumps(trace, sort_keys=True).encode()).hexdigest()
    report = {
        'scope': 'offline render-pose candidates only; no extra game frames were presented',
        'native_comparison_boundary': native_kind,
        'source_boundary': kind, 'source_samples': len(frames), 'accepted_midpoints': len(generated),
        'rejected': rejected, 'source_gap_ms': distribution(wall_gaps),
        'minimum_midpoint_lookback_ms': distribution(lookback),
        'native_inner_sample_errors_world_units_or_degrees': {k: distribution(v) for k, v in errors.items()},
        'immutable_source_sha256': before,
        'simulation_writes': 0,
        'full_vehicle_rotation': bool(frames) and 'm0' in frames[0],
        'limits': ['not a button-to-photon latency measurement',
                   'vehicle scale/deformation is not synthesized; old traces lack full rotation',
                   'native post-update poses are not renderer-ground-truth images',
                   'only the player vehicle is synthesized; traffic is audited by hash',
                   'particles, deformed vertices, shadows, reflections, trails and HUD are not synthesized',
                   'native Replay is excluded; backward time/clock pauses/cuts reset history',
                   '60/120-Hz/ProMotion on-screen A/B still requires a second owned render pass'],
    }
    return report, generated


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('world', type=Path)
    p.add_argument('--start', type=float, required=True)
    p.add_argument('--end', type=float, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--poses', type=Path)
    a = p.parse_args()
    if not math.isfinite(a.start) or not math.isfinite(a.end) or a.end <= a.start:
        p.error('require finite start < end')
    result, candidates = experiment(rows(a.world), a.start, a.end)
    a.output.write_text(json.dumps(result, indent=2)+'\n')
    if a.poses:
        with a.poses.open('w') as f:
            for c in candidates:
                f.write(json.dumps(c)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
