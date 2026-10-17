#!/usr/bin/env python3
# Derived from SpeedBreaker packaging. GPL-3.0-or-later (see COPYING).
"""Generate the macOS cover icon from the supplied cover (requires Pillow)."""
from pathlib import Path
import sys
from PIL import Image, ImageDraw
root = Path(__file__).resolve().parents[2]
source = Image.open(root / 'assets/cover.jpg').convert('RGBA').resize((824,824))
mask = Image.new('L',(824,824)); ImageDraw.Draw(mask).rounded_rectangle((0,0,823,823),radius=185,fill=255)
icon = Image.new('RGBA',(1024,1024)); icon.paste(source,(100,100),mask)
icon.save(sys.argv[1] if len(sys.argv)>1 else root / 'scripts/macos/AppIcon.png')
