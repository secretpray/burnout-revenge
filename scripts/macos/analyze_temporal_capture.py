#!/usr/bin/env python3
"""Validate opt-in renderer captures; summarize depth/2D/motion input evidence.

Not a motion-vector generator. --previews uses Pillow only for diagnostic PNGs.
"""
import argparse
import array
import collections
import json
import math
from pathlib import Path
import struct
import sys


def inspect_capture(path, previews=False):
    events = [json.loads(line) for line in (path / 'events.jsonl').read_text().splitlines()]
    headers = [e for e in events if e['type'] == 'frame']
    ends = [e for e in events if e['type'] == 'end']
    errors = []
    if len(headers) != 1 or headers[0].get('schema') != 1 or len(ends) != 1:
        raise ValueError(f'{path}: incomplete or unsupported manifest')
    constants = (path / 'vs_constants.bin').read_bytes()
    draws = [e for e in events if e['type'] == 'draw']
    if len(draws) != ends[0]['draws'] or len(constants) != len(draws) * 4096:
        errors.append('draw count / constant byte count mismatch')
    for name, key in [('ps_constants.bin', 'ps_bytes_per_draw'), ('texture_fetches.bin', 'fetch_bytes_per_draw')]:
        if key in headers[0]:
            file = path/name
            if not file.is_file() or file.stat().st_size != len(draws)*headers[0][key]:
                errors.append(f'{name} byte count mismatch')
    by_shader = collections.defaultdict(list)
    for draw in draws:
        offset = draw['constants_offset']
        if offset < 0 or offset + 4096 > len(constants):
            errors.append(f'constant range outside file: draw {draw["draw"]}')
            continue
        if draw['depth_control'] & 2 and not draw['screen_pixels']:
            by_shader[draw['vs']].append(struct.unpack_from('<1024I', constants, offset))
    shaders = {e['vs']: e for e in events if e['type'] == 'shader'}
    matrix_counts = collections.Counter()
    matrix_draws = {}
    for draw in draws:
        meta = shaders.get(draw['vs'])
        if (meta and not meta['relative_constants'] and int(meta['const_used'][0], 16) & 15 == 15
                and draw['depth_control'] & 2 and not draw['screen_pixels']
                and draw['color_info'] & 4095 == 0 and draw['surface'] & 0x3fff >= 640):
            raw = constants[draw['constants_offset']:draw['constants_offset']+64]
            if len(raw) == 64:
                matrix_counts[raw] += 1
                matrix_draws[raw] = draw['draw']
    stable = {}
    for vs, rows in by_shader.items():
        metadata = shaders.get(vs)
        if not metadata or metadata['relative_constants']:
            continue
        used = [i for i in range(256) if int(metadata['const_used'][i // 64], 16) & (1 << (i % 64))]
        # Candidates only: a common value can be lighting, projection or an
        # object constant shared by repeated draws, not necessarily a camera.
        stable[vs] = {str(i): list(rows[0][i*4:i*4+4]) for i in used
                      if all(row[i*4:i*4+4] == rows[0][i*4:i*4+4] for row in rows[1:])}
    images = []
    for event in [e for e in events if e['type'] == 'image']:
        entry = dict(event)
        image_path = path / event['file']
        raw = image_path.read_bytes() if image_path.is_file() else b''
        width, height = event['width'], event['height']
        bpp = 8 if event['format'] in (91,97) else 4
        if width <= 0 or height <= 0 or len(raw) != width * height * bpp or len(raw) != event['bytes']:
            errors.append(f'image byte count mismatch: {event["file"]}')
            images.append(entry)
            continue
        if event['format'] in (100,130):  # R32 float or D32_SFLOAT_S8_UINT depth aspect
            values = array.array('f')
            values.frombytes(raw)
            if sys.byteorder != 'little':
                values.byteswap()
            valid = [v for v in values if math.isfinite(v) and 0 <= v <= 1]
            if len(valid) != len(values):
                errors.append(f'depth contains nonfinite/out-of-range samples: {event["file"]}')
            positive = sorted(v for v in valid if v > 0)
            entry['depth'] = {'finite_01_fraction': len(valid) / len(values),
                              'nonzero_fraction': len(positive) / len(values),
                              'min': min(valid) if valid else None,
                              'max': max(valid) if valid else None}
            if previews and positive and positive[0] != positive[-1]:
                from PIL import Image
                lo, hi = positive[len(positive)//100], positive[min(len(positive)-1, len(positive)*99//100)]
                if hi > lo:
                    pixels = bytes(max(0, min(255, int((v-lo)*255/(hi-lo)))) if math.isfinite(v) else 0 for v in values)
                    Image.frombytes('L', (width, height), pixels).save(image_path.with_suffix('.png'))
                    entry['preview_range_p01_p99'] = [lo, hi]
        elif previews and event['format'] == 37:  # VK_FORMAT_R8G8B8A8_UNORM
            from PIL import Image
            Image.frombytes('RGBA', (width, height), raw).convert('RGB').save(image_path.with_suffix('.png'))
        images.append(entry)
    if ends[0]['omitted'] or ends[0]['stream_error']:
        errors.append('capture reports omitted records or stream error')
    matrices = [{'count': n, 'representative_draw': matrix_draws[raw],
                 'rows': [list(struct.unpack_from('<4f', raw, row*16)) for row in range(4)]}
                for raw, n in matrix_counts.most_common(3)]
    summary = {'path': str(path), 'header': headers[0], 'end': ends[0],
               'errors': errors, 'draw_count': len(draws),
               'screen_pixel_draws': sum(d['screen_pixels'] for d in draws),
               'world_shader_draws': {vs: len(rows) for vs, rows in by_shader.items()},
               'stable_used_constants_bits': stable, 'images': images,
               'common_c0_c3_matrix_candidates': matrices,
               'motion_vectors_validated': False, 'hud_separation_validated': False}
    (path / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', type=Path, nargs='+')
    parser.add_argument('--previews', action='store_true')
    args = parser.parse_args()
    summaries = [inspect_capture(p, args.previews) for p in args.captures]
    for s in summaries:
        print(f'{s["path"]}: {s["draw_count"]} draws, {len(s["images"])} images, errors={s["errors"]}')
    if len(summaries) == 2:
        a, b = summaries
        candidates = {}
        for vs, current in b['stable_used_constants_bits'].items():
            previous = a['stable_used_constants_bits'].get(vs, {})
            changed = [i for i, value in current.items() if i in previous and previous[i] != value]
            if changed:
                candidates[vs] = changed
        pair = {'frames': [s['header']['frame'] for s in summaries],
                'adjacent_gpu_frames': b['header']['frame'] == a['header']['frame'] + 1,
                'changed_frame_stable_used_constants': candidates,
                'note': 'Candidates for inspection, not identified camera matrices or motion vectors.'}
        (args.captures[0].parent / 'temporal-pair.json').write_text(json.dumps(pair, indent=2) + '\n')
        print(json.dumps(pair, indent=2))
    return int(any(s['errors'] for s in summaries))


if __name__ == '__main__':
    raise SystemExit(main())
