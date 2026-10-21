#!/usr/bin/env python3
"""Bounded local smoke run with isolated profiles and optional scripted input.

Example: python3 scripts/macos/smoke_game.py burnout --seconds 90
Logs and captures are private build artifacts. This does not assert that a
menu or race was reached: inspect the captured frame and game log.
"""
import argparse
import os
from pathlib import Path
import subprocess
import datetime
import json
import re
import time
import threading
import statistics
from zoneinfo import ZoneInfo
from perf_log import parse_sample, parse_session, session_clock_warnings

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('game', choices=['burnout', 'burnout'])
parser.add_argument('--seconds', type=float, default=55)
parser.add_argument('--input', default='8-50/3:START,10-50/3:A')
parser.add_argument('--name', default='smoke')
parser.add_argument('--capture-after', type=float, default=2)
parser.add_argument('--profile-name', help='explicit automated response to the Xbox keyboard (Burnout only)')
parser.add_argument('--sound', action='store_true', help='enable audio for the smoke run')
parser.add_argument('--ui-keys', help='runtime UI test keys and captures, e.g. 25:Shot,60:Shot')
parser.add_argument('--async-shaders', action='store_true', help='use normal asynchronous pipeline compilation (default: wait for all draws)')
parser.add_argument('--no-virtual-pad', action='store_true', help='allow a real controller instead of the isolated test pad')
parser.add_argument('--data-dir', type=Path, help='separate benchmark profile directory')
parser.add_argument('--cache-dir', type=Path, help='separate benchmark cache directory')
parser.add_argument('--binary', type=Path, help='test a packaged executable instead of the development build')
parser.add_argument('--installed-game', action='store_true', help='use normal installed-game discovery, without a development content override')
parser.add_argument('--defaults', action='store_true', help='leave audio, graphics and welcome-screen settings at their first-run defaults')
parser.add_argument('--measure-after', type=float, default=0, help='seconds to skip before collecting periodic performance samples')
parser.add_argument('--resolution', choices=['auto', '1x', '1.5x', '2x', '3x'], default='1x')
parser.add_argument('--scaling', choices=['bicubic', 'linear', 'fsr1', 'metalfx'])
parser.add_argument('--graphics-at', help='Burnout diagnostic schedule, e.g. 0:720-bicubic,100:720-metalfx,130:1080-bicubic')
args = parser.parse_args()
if args.seconds <= 0 or Path(args.name).name != args.name:
    parser.error('use a positive duration and a single output-directory name')
build = ROOT / 'build' / f'{args.game}-mac'
binary = build / 'runtime' / ('BurnoutRevenge' if args.game == 'burnout' else 'BurnoutRevenge')
if args.binary:
    binary = args.binary.resolve()
if not binary.is_file():
    parser.error(f'build the executable first: {binary}')
out = ROOT / 'build' / f'{args.game}-runs' / args.name
out.mkdir(parents=True, exist_ok=True)
# A dedicated working directory keeps the renderer's build/front_check files
# separate between games and runs. All content paths are absolute.
(out / 'build').mkdir(exist_ok=True)
env = os.environ.copy()
if args.profile_name is not None:
    if args.game != 'burnout':
        parser.error('--profile-name is only supported by Burnout')
    env['BR_TEST_KEYBOARD_TEXT'] = args.profile_name
if args.ui_keys:
    env['BURNOUT_UI_KEYS'] = args.ui_keys
env.update(BR_DATA_DIR=str((args.data_dir or build / 'data').resolve()),
           BR_CACHE_DIR=str((args.cache_dir or build / 'cache').resolve()),
           BURNOUT_WHATS_NEW='0', BURNOUT_MUTE='0' if args.sound else '1', BURNOUT_RESOLUTION='1x',
           BURNOUT_ASYNC_PIPELINES='1' if args.async_shaders else '0',
           BURNOUT_CHECK_FRONT_SEC=str(args.capture_after), BURNOUT_INPUT_SCRIPT=args.input)
env['BURNOUT_RESOLUTION'] = args.resolution
if args.scaling:
    env['BURNOUT_SCALING'] = args.scaling
if args.graphics_at:
    if args.game != 'burnout':
        parser.error('--graphics-at is only supported by Burnout')
    env['BR_GRAPHICS_AT'] = args.graphics_at
    env.pop('BURNOUT_RESOLUTION', None)
    env.pop('BURNOUT_SCALING', None)
if args.defaults:
    for key in ('BURNOUT_WHATS_NEW', 'BURNOUT_MUTE', 'BURNOUT_RESOLUTION', 'BURNOUT_ASYNC_PIPELINES'):
        env.pop(key, None)
if not args.no_virtual_pad:
    env['BURNOUT_VIRTUAL_PAD'] = '1'
else:
    env.pop('BURNOUT_VIRTUAL_PAD', None)
game_env = 'BURNOUT_GAME_DIR' if args.game == 'burnout' else 'BURNOUT_GAME_DIR'
if args.installed_game:
    env.pop(game_env, None)
else:
    env[game_env] = str(ROOT / ('game/burnout/files' if args.game == 'burnout' else 'game/files'))
samples = []
def collect_output(pipe, log):
    for line in pipe:
        log.write(line)
        elapsed = time.monotonic() - started
        sample = parse_sample(line, elapsed)
        if sample and sample['seconds'] >= args.measure_after:
            samples.append(sample)

session_dir = Path(env['BR_DATA_DIR']) / 'logs'
previous_logs = {p: (p.stat().st_mtime_ns, p.stat().st_size) for p in session_dir.glob('*.log')}
with (out / 'run.log').open('w') as log:
    started = time.monotonic()
    timed_out = False
    process = subprocess.Popen([str(binary)], cwd=out, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, errors='replace')
    reader = threading.Thread(target=collect_output, args=(process.stdout, log))
    reader.start()
    (out / 'pid').write_text(str(process.pid))
    print(f'{args.game}: PID {process.pid}, log {out / "run.log"}', flush=True)
    try:
        code = process.wait(timeout=args.seconds)
    except subprocess.TimeoutExpired:
        timed_out = True
        process.terminate()
        try:
            code = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            code = process.wait()
    reader.join()
    print(f'Exit: {code}; captures: {out / "build"}', flush=True)
# Keep the work log current even when an import aborts before a capture.
log_text = (out / 'run.log').read_text(errors='replace')
session_logs = sorted((p for p in session_dir.glob('*.log')
                       if previous_logs.get(p) != (p.stat().st_mtime_ns, p.stat().st_size)),
                      key=lambda p: p.stat().st_mtime_ns)
if session_logs:
    log_text = '\n'.join(p.read_text(errors='replace') for p in session_logs)
    (out / 'session.log').write_text(log_text)
    samples = parse_session(log_text, args.measure_after)
missing = sorted(set(re.findall(r'UNIMPLEMENTED (\w+)', log_text)))
addons = [dict(id=m[0], name=m[1]) for m in re.findall(r'\[addons\] connected (\w+): (.*?) \(\d+ files\)', log_text)]
addon_queries = [int(n) for n in re.findall(r'\[addons\] game enumerates (\d+) pack', log_text)]
captures = sorted(p.name for p in (out / 'build').glob('*.ppm'))
now = datetime.datetime.now(ZoneInfo('Europe/Kyiv')).strftime('%Y-%m-%d %H:%M:%S %Z')
result = dict(game=args.game, run=args.name, date=now, exit_code=code,
              executable=str(binary), installed_game=args.installed_game, defaults=args.defaults,
              duration=round(time.monotonic() - started, 1), bounded_stop=timed_out,
              missing_imports=missing, captures=captures, sound=args.sound,
              async_shaders=args.async_shaders, virtual_pad=not args.no_virtual_pad,
              data_dir=env['BR_DATA_DIR'], cache_dir=env['BR_CACHE_DIR'],
              snapshot_slots=env.get('BR_WATCH_SNAPSHOT_SLOTS', 'default'),
              input_script=args.input, ui_keys=args.ui_keys,
              connected_addons=addons, addon_enumeration_counts=addon_queries,
              measure_after=args.measure_after, performance_samples=samples)
result.update(session_logs=[str(p) for p in session_logs], graphics_schedule=args.graphics_at,
              resolution_override=None if args.graphics_at or args.defaults else args.resolution,
              scaling_override=None if args.graphics_at else args.scaling)
result['measurement_warnings'] = session_clock_warnings(log_text, result['duration']) if session_logs else []
if samples:
    keys = sorted(set().union(*(s.keys() for s in samples)) - {'seconds'})
    result['performance_means'] = {
        key: round(statistics.mean(s[key] for s in samples if key in s), 3)
        for key in keys
    }
    result['performance_medians'] = {key: round(statistics.median(s[key] for s in samples if key in s), 3) for key in keys}
(out / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
# A run diary kept outside the repository (private/ is ignored by git).
progress = ROOT / 'private/docs/PROGRESS.md'
if progress.exists():
    status = 'stopped at the time limit' if timed_out else f'exited with code {code}'
    relative = out.relative_to(ROOT)
    log_name = "session.log" if session_logs else "run.log"
    entry = (f'\n### {now} — {args.game}, {args.name}\n\n'
             f'- Run {status}; duration {result["duration"]} s.\n'
             f'- Log: [`{relative}/{log_name}`](../../{relative}/{log_name}).\n'
             f'- Frames: {len(captures)}; folder `{relative}/build/`.\n'
             f'- Sound: {"default" if args.defaults else "on" if args.sound else "forced off for the check"}.\n'
             f'- Shaders: {"default" if args.defaults else "asynchronous" if args.async_shaders else "wait for compilation"}; '
             f'controller: {"virtual test pad" if not args.no_virtual_pad else "normal input"}.\n'
             f'- Fatal imports: {", ".join(missing) if missing else "none registered in the log"}.\n'
             '- The menu/race stage needs a visual check; the automatic report does not confirm it.\n')
    if samples:
        entry += (f'- Periodic figures after {args.measure_after:g} s '
                  f'({len(samples)} samples, means of the logger values): '
                  f'`{json.dumps(result["performance_means"])}`; '
                  f'snapshot pool: `{result["snapshot_slots"]}`. '
                  'This is not the distribution of every frame time.\n')
    for warning in result['measurement_warnings']:
        entry += f'- **Measurement warning:** {warning}\n'
    if args.game == 'burnout':
        entry += (f'- Add-ons: {len(addons)} registered; '
                  f'the game\'s enumeration answers: `{addon_queries}`. '
                  'Whether each car is usable needs a check in the garage.\n')
    with progress.open('a') as doc:
        doc.write(entry)
raise SystemExit(code if code >= 0 else 128 - code)
