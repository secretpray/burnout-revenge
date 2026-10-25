#!/usr/bin/env python3
"""Join developer pacing ledgers without treating presents as unique images.

Input: BURNOUT_VBLANK_LOG, its .frames.csv/.actual.csv companions and
BURNOUT_CRASH_PACING_LOG. All reported latencies are pipeline timestamps,
not button-to-photon measurements. Requires only the Python standard library.
"""
import argparse
import csv
import json
import math
import re
import sys
from pathlib import Path


def rows(path):
    text = Path(path).read_text()
    # The runtime's bounded process exit can leave its stdio buffer's final
    # row unfinished. Reject interior corruption; report a discarded tail.
    if text and not text.endswith("\n"):
        print(f"{path}: incomplete terminal row discarded", file=sys.stderr)
        text = text[:text.rfind("\n") + 1]
    result = list(csv.DictReader(line for line in text.splitlines() if not line.startswith("#")))
    if any(None in r or any(v is None for v in r.values()) for r in result):
        raise ValueError(f"{path}: malformed interior row")
    return result


def stats(values):
    """Nearest-rank percentiles, milliseconds; empty means unknown."""
    values = sorted(values)
    if not values:
        return None
    def percentile(p):
        return round(values[max(0, math.ceil(p * len(values)) - 1)] / 1e6, 6)
    return {"n": len(values), "p50_ms": percentile(.50), "p95_ms": percentile(.95),
            "p99_ms": percentile(.99), "min_ms": round(values[0] / 1e6, 6),
            "max_ms": round(values[-1] / 1e6, 6)}


def intervals(values):
    return stats([b - a for a, b in zip(values, values[1:])])


def analyze(presents, actual, sources, world, start, end, display, fixed_hz=None):
    if not world:
        raise ValueError("world trace is empty; Crash was not reached")
    # One runtime session, steady timestamps in all ledgers. SessionSeconds
    # has a different origin, measured at each game sample, not wall clock.
    origins = sorted(int(r["steady_ns"]) - float(r["session_s"]) * 1e9 for r in world)
    origin = origins[len(origins) // 2]
    low, high = origin + start * 1e9, origin + end * 1e9
    chosen = [r for r in presents if low <= int(r["presented"]) < high]
    source_rows = [r for r in sources if low <= int(r["submitted"]) < high]
    def key(r):
        return int(r["swapchain"]), int(r["id"])
    reports = {}
    duplicate_reports = 0
    for r in actual:
        k = key(r)
        duplicate_reports += k in reports
        reports[k] = r
    shown, never, missing = [], 0, 0
    for p in chosen:
        a = reports.get(key(p))
        if a is None:
            missing += 1  # Unknown, never silently counted as a dropped frame.
        elif int(a["actual_ns"]) == 0:
            never += 1
        else:
            at = int(a["actual_ns"]) + int(a["host_to_steady_ns"])
            shown.append((at, p))
    shown.sort(key=lambda pair: pair[0])
    source_ids = {int(p["source_id"]) for _, p in shown if int(p["source_id"])}
    new_times = []
    seen = set()
    for at, p in shown:
        source = int(p["source_id"])
        if source and source not in seen:
            new_times.append(at)
            seen.add(source)
    sampled = [r for r in world if low <= int(r["steady_ns"]) < high]
    kinds = {}
    for kind in sorted({r["kind"] for r in sampled}):
        group = [r for r in sampled if r["kind"] == kind]
        forward, equal, backward = 0, 0, 0
        deltas = []
        for a, b in zip(group, group[1:]):
            dt = float(b["world_s"]) - float(a["world_s"])
            forward += dt > 1e-6
            equal += abs(dt) <= 1e-6
            backward += dt < -1e-6
            if dt > 1e-6:
                deltas.append(dt * 1e9)
        kinds[kind] = {"calls": len(group), "calls_per_s": round(len(group) / (end - start), 4),
                       "wall_intervals": intervals([int(r["steady_ns"]) for r in group]),
                       "world_forward": forward, "world_equal": equal, "world_backward": backward,
                       "positive_world_deltas": stats(deltas),
                       "native_dt": stats([float(r["dt_s"]) * 1e9 for r in group])}
    holds = None
    if fixed_hz:
        # Only for a separately established fixed refresh, never ProMotion.
        spans = [b - a for a, b in zip(new_times, new_times[1:])]
        holds = {"refresh_hz": fixed_hz, "refreshes_between_new_sources": {},
                 "off_grid_intervals": 0}
        for span in spans:
            n = round(span * fixed_hz / 1e9)
            if abs(span * fixed_hz / 1e9 - n) > .15:
                holds["off_grid_intervals"] += 1
            bucket = str(n)
            h = holds["refreshes_between_new_sources"]
            h[bucket] = h.get(bucket, 0) + 1
    return {
        "display": display, "window_session_s": [start, end],
        "definitions": {
            "new_source": "new game front-buffer ID; pixel/content uniqueness is not established",
            "wait_return": "present_wait completion; not the physical on-screen timestamp",
            "driver_on_screen": "Metal presentedTime via VK_GOOGLE_display_timing; not photodiode measurement",
            "latency": "pipeline age; excludes physical input and panel scanout/response",
            "simulation": "dispatch/event/controls boundaries are separate; not a count of per-body physics substeps",
        },
        "source_submissions": len(source_rows), "source_rate_per_s": round(len(source_rows) / (end - start), 4),
        "pending_replacements": sum(int(r["replaced_source_id"]) != 0 for r in source_rows),
        "waited_presents": len(chosen), "driver_reported_shown": len(shown),
        "driver_reported_never_shown": never, "driver_reports_missing": missing,
        "duplicate_report_keys_in_file": duplicate_reports,
        "distinct_source_ids_shown": len(source_ids),
        "same_source_redraws_shown": sum(int(p["source_id"]) != 0 for _, p in shown) - len(source_ids),
        "ui_only_presents": sum(int(p["source_id"]) == 0 for _, p in shown),
        "submit_intervals": intervals([int(r["submitted"]) for r in source_rows]),
        "present_call_intervals": intervals([int(r["presented"]) for r in chosen]),
        "wait_return_intervals": intervals([int(r["on_screen"]) for r in chosen]),
        "driver_on_screen_intervals": intervals([at for at, _ in shown]),
        "new_source_on_screen_intervals": intervals(new_times),
        "submit_to_driver_on_screen": stats([at - int(p["submitted"]) for at, p in shown if int(p["fresh"]) and int(p["submitted"])]),
        "present_to_driver_on_screen": stats([at - int(p["presented"]) for at, p in shown]),
        "vblank_to_driver_on_screen": stats([at - int(p["vblank"]) for at, p in shown if int(p["vblank"])]),
        "fixed_refresh_holds": holds,
        "event_states": sorted({int(r["event_state"]) for r in sampled}),
        "playback_values": sorted({int(r["playback"]) for r in sampled}),
        "simulation_boundaries": kinds,
    }


def session_evidence(path, start, end, display):
    text = Path(path).read_text(errors="replace")
    matches = re.findall(r"\[pacing-display\] mode_hz=([\d.]+) min_hz=([\d.]+) max_hz=([\d.]+) external_fixed=(\d)", text)
    evidence = {"display_log": None, "focus_at_window_start": "unknown", "focus_changes_in_window": []}
    if matches:
        hz, low, high, fixed = matches[-1]
        evidence["display_log"] = dict(mode_hz=float(hz), min_hz=float(low), max_hz=float(high), external_fixed=fixed == "1")
        if display == "promotion" and fixed == "1":
            raise ValueError("session reports a fixed external display; refusing ProMotion label")
        expected = {"fixed-60": 60, "fixed-120": 120}.get(display)
        if expected and fixed == "1" and (abs(float(low)-expected) > 1 or abs(float(high)-expected) > 1):
            raise ValueError("fixed refresh label conflicts with the session display")
    for line in text.splitlines():
        m = re.match(r"\[\s*([\d.]+)\] .*window focus (gained|lost)", line)
        if not m:
            continue
        at, state = float(m[1]), "foreground" if m[2] == "gained" else "background"
        if at <= start:
            evidence["focus_at_window_start"] = state
        elif at < end:
            evidence["focus_changes_in_window"].append({"session_s": at, "state": state})
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("presents", type=Path)
    parser.add_argument("world", type=Path)
    parser.add_argument("--start", type=float, required=True)
    parser.add_argument("--end", type=float, required=True)
    parser.add_argument("--display", choices=("promotion", "fixed-60", "fixed-120", "other", "unknown"), required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--session-log", type=Path)
    args = parser.parse_args()
    if not math.isfinite(args.start) or not math.isfinite(args.end) or args.end <= args.start:
        parser.error("require finite start < end")
    fixed = {"fixed-60": 60, "fixed-120": 120}.get(args.display)
    result = analyze(rows(args.presents), rows(str(args.presents) + ".actual.csv"),
                     rows(str(args.presents) + ".frames.csv"), rows(args.world),
                     args.start, args.end, args.display, fixed)
    if args.session_log:
        result["session_evidence"] = session_evidence(args.session_log, args.start, args.end, args.display)
    result["driver_timestamp_caveat"] = "MoltenVK may substitute a runtime timestamp when Metal presentedTime is unavailable; nonzero reports alone do not prove scanout"
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
