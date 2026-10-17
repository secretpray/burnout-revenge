"""Performance records from stdout or the runtime's timestamped session log."""
import re

PATTERNS = dict(
    fps=r'\[perf\] ([\d.]+) fps', worst_ms=r'worst frame ([\d.]+) ms',
    uploads=r'([\d.]+) uploads, GPU', stall_ms=r'([\d.]+) ms stalled',
    pool_full=r'\(([\d.]+) pool full\)', cp_ms=r'CP busy ([\d.]+) ms/frame',
    draws=r'per frame: ([\d.]+) draws', game_gpu_ms=r'executing ([\d.]+) ms',
    post_gpu_ms=r'post-processing GPU ([\d.]+) ms', aa_gpu_ms=r'\(AA ([\d.]+) ms\)',
    gpu_queue_ms=r'uploads, GPU busy ([\d.]+) ms',
)


def parse_sample(line, fallback_seconds=None):
    if '[perf]' not in line:
        return None
    timestamp = re.match(r'^\[\s*([\d.]+)\]\s+\[perf\]', line)
    seconds = float(timestamp[1]) if timestamp else fallback_seconds
    if seconds is None:
        return None  # an untimed historical record can't be assigned a phase
    sample = {'seconds': round(seconds, 3)}
    for key, pattern in PATTERNS.items():
        match = re.search(pattern, line)
        if match:
            sample[key] = float(match[1])
    return sample if 'fps' in sample else None


def parse_session(text, measure_after=0):
    samples = []
    for line in text.splitlines():
        sample = parse_sample(line)
        if sample and sample['seconds'] >= measure_after:
            samples.append(sample)
    return samples


def session_clock_warnings(text, runner_seconds):
    """Flag incomparable clocks (e.g. a suspended host); never diagnose a cause."""
    stamps = [float(m[1]) for m in re.finditer(r'^\[\s*([\d.]+)\]', text, re.MULTILINE)]
    if stamps and max(stamps) > runner_seconds + max(5.0, runner_seconds * .1):
        return [f'Session clock reached {max(stamps):.1f}s, runner measured '
                f'{runner_seconds:.1f}s; inspect clock discontinuities and '
                'scheduled phases before using these performance samples.']
    return []
