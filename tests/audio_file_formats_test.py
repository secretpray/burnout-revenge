# Burnout Revenge. GPL-3.0-or-later (see COPYING).
import pathlib
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory(prefix='burnout-audio-') as temp:
    folder = pathlib.Path(temp)
    for name in ('tone.mp3', 'tone.m4a', 'tone.flac', 'tone.ogg', 'tone.opus', 'tone.wav', 'tone.aiff'):
        extra = ['-write_id3v2', '1'] if name.endswith('.aiff') else []
        subprocess.run([sys.argv[2], '-v', 'error', '-f', 'lavfi', '-i',
                        'sine=frequency=440:sample_rate=44100:duration=0.5',
                        '-metadata', 'title=Linux fixture', '-metadata', 'artist=Synthetic artist',
                        *extra, str(folder / name)], check=True, timeout=30)
    (folder / 'corrupt.mp3').write_bytes(b'not audio\x00' * 17)
    subprocess.run([*sys.argv[3:], sys.argv[1], str(folder)], check=True, timeout=30)
