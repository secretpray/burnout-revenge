#!/usr/bin/env python3
# Burnout Revenge packaging. GPL-3.0-or-later (see COPYING).
"""Validate the canonical app icon and optionally copy it to a packaging path."""
from pathlib import Path
import shutil, sys, subprocess, tempfile
from PIL import Image
root = Path(__file__).resolve().parents[2]
source = root / 'assets/AppIcon.png'
with tempfile.TemporaryDirectory() as tmp:
    renderer = Path(tmp)/'render_icon'
    subprocess.run(['xcrun','--sdk','macosx','clang++','-fobjc-arc',str(root/'scripts/macos/render_icon.mm'),
        '-framework','AppKit','-o',str(renderer)],check=True)
    subprocess.run([str(renderer),str(root/'assets/icon-artwork.png'),str(source)],check=True)
image = Image.open(source).convert('RGBA')
assert image.size == (1024, 1024), 'Icon must be 1024x1024'
alpha = image.getchannel('A')
assert alpha.getextrema() == (0,255), 'Icon must retain transparent margins'
assert all(alpha.getpixel(p) == 0 for p in ((0,0),(1023,0),(0,1023),(1023,1023))), 'Corners must be transparent'
assert alpha.getpixel((512,512)) == 255, 'Artwork must stay opaque'
if len(sys.argv)>1 and Path(sys.argv[1]).resolve()!=source.resolve():shutil.copy2(source,sys.argv[1])
print(f'{source}: RGBA, transparent rounded silhouette verified')
