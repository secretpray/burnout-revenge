#!/usr/bin/env python3
# Burnout Revenge port. GPL-3.0-or-later (see COPYING).
"""Package exact LGPL library sources alongside a binary release, without game data."""
from pathlib import Path
import argparse, hashlib, shutil, subprocess, tarfile, tempfile
root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
p.add_argument('--ffmpeg-archive', type=Path, required=True)
a = p.parse_args()
expected = '733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1'
if hashlib.sha256(a.ffmpeg_archive.read_bytes()).hexdigest() != expected:
    p.error('FFmpeg source archive is not the pinned 7.1.1 source')
libmspack = root/'tools/XenonRecomp/thirdparty/libmspack'
revision = subprocess.check_output(['git','-C',str(libmspack),'rev-parse','HEAD'],text=True).strip()
a.output.parent.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
    stage = Path(tmp)/'BurnoutRevenge-third-party-source';stage.mkdir()
    shutil.copy2(a.ffmpeg_archive, stage/'ffmpeg-7.1.1.tar.xz')
    shutil.copytree(libmspack,stage/'libmspack',ignore=shutil.ignore_patterns('.git','build','*.o','*.a'))
    shutil.copytree(root/'patches/ffmpeg',stage/'patches/ffmpeg')
    (stage/'scripts/macos').mkdir(parents=True)
    shutil.copy2(root/'scripts/build_ffmpeg_xma.sh',stage/'scripts')
    shutil.copy2(root/'scripts/macos/build_deps.sh',stage/'scripts/macos')
    shutil.copy2(root/'CMakeLists.txt',stage/'CMakeLists.txt')
    shutil.copy2(root/'COPYING',stage/'COPYING')
    (stage/'README.txt').write_text('Complete FFmpeg 7.1.1 archive and local patches/configure scripts.\n'
        +f'Complete libmspack checkout at {revision}; LGPL source and COPYING.LIB included.\n'
        +'These are the statically linked LGPL parts of the macOS application.\n'
        +'Runtime/relink instructions: https://github.com/secretpray/burnout-revenge\n'
        +'Regenerate game code from your own supported disc; no game data is included.\n')
    with tarfile.open(a.output,'w:xz') as tar:tar.add(stage,arcname=stage.name)
print(a.output)
