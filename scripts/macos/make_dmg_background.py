#!/usr/bin/env python3
# SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#
# make_dmg_background.py [output folder]: the picture behind the download's
# Finder window (make_app.py --dmg), in the look of the first-run screens:
# dark asphalt, yellow and black hazard stripes, heavy condensed italic
# capitals (Barlow Condensed, OFL, runtime/thirdparty/fonts), slanted slabs
# and running chevrons from the app to Applications. All drawn here: nothing
# of the game's own art. Writes dmg-background.png (660x560) and
# dmg-background@2x.png (1320x1120) into the folder (default: assets/);
# make_app.py joins them into one TIFF for Finder.
#
# The window's layout lives in styled_dmg.py (WINDOW, ICONS): the two icons
# sit at the centres drawn here. Finder writes the icons' names under them in
# black (light appearance) or white (dark), so the band under the icons is a
# middle grey either reads on. How much of the picture a window shows
# depends on the player's Finder: its toolbar (always there on macOS 27),
# path bar and status bar (global settings a disk image can't turn off). The
# design is whole in the first 456 points (a Finder without path and status
# bars), still reads down to 400 (with them: the instruction is the last
# thing), and the asphalt goes on below for a Finder that shows more.
import os
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONTS = os.path.join(ROOT, 'runtime', 'thirdparty', 'fonts')

WIDTH, HEIGHT = 660, 560  # the picture; the design is in the first DESIGN points
DESIGN = 456
APP_X, APPS_X, ICONS_Y = 165, 495, 205  # icon centres (make_app.py's DMG_ICONS)

YELLOW = (247, 181, 0)
ORANGE = (255, 122, 0)
BLACK = (14, 14, 14)
WHITE = (244, 242, 236)


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, f'BarlowCondensed-{name}.ttf'), size)


def asphalt(w, h, s):
    """Near-black, a little lighter in the middle, with a fine grain."""
    rng = random.Random(2006)
    base = Image.new('L', (w, h), 0)
    px = base.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = rng.randint(0, 255)
    grain = base.filter(ImageFilter.GaussianBlur(0.6 * s)).point(lambda v: 18 + (v - 128) * 10 // 128)
    glow = Image.new('L', (w, h), 0)
    ImageDraw.Draw(glow).ellipse((-w * 0.2, h * 0.05, w * 1.2, h * 1.1), fill=22)
    glow = glow.filter(ImageFilter.GaussianBlur(90 * s))
    shade = ImageChops.add(grain, glow)
    return Image.merge('RGB', (shade.point(lambda v: v + 2), shade.point(lambda v: v + 1), shade))


def hazard(draw, y0, y1, w, s, offset=0):
    """Yellow and black diagonal stripes between y0 and y1."""
    draw.rectangle((0, y0, w, y1), fill=YELLOW)
    step, h = 26 * s, y1 - y0
    for x in range(-int(h) - step + offset * s, w + step, step):
        draw.polygon([(x, y1), (x + step // 2, y1), (x + step // 2 + h, y0), (x + h, y0)], fill=BLACK)


def slab(draw, x0, y0, x1, y1, fill, slant):
    """A parallelogram leaning right, as the game's menu bars."""
    draw.polygon([(x0 + slant, y0), (x1 + slant, y0), (x1, y1), (x0, y1)], fill=fill)


def chevron(draw, x, y, size, width, fill):
    """A ">" centred on (x, y)."""
    h = size / 2
    draw.polygon([(x - h, y - size), (x - h + width, y - size), (x + h + width, y), (x - h + width, y + size),
                  (x - h, y + size), (x + h, y)], fill=fill)


def render(s):
    w, h = WIDTH * s, HEIGHT * s
    img = asphalt(w, h, s)
    d = ImageDraw.Draw(img)

    # Hazard stripes, top and bottom, with a thin dark edge.
    hazard(d, 0, 12 * s, w, s)
    d.rectangle((0, 12 * s, w, 15 * s), fill=BLACK)
    bottom = DESIGN * s
    hazard(d, bottom - 12 * s, bottom, w, s, offset=13)
    d.rectangle((0, bottom - 15 * s, w, bottom - 12 * s), fill=BLACK)

    # The heading: running chevrons, then the name on a dark slab.
    slab(d, -20 * s, 34 * s, 470 * s, 92 * s, (24, 24, 24), 18 * s)
    for i, c in enumerate([(120, 80, 0), (170, 110, 0), (215, 145, 0), YELLOW]):
        chevron(d, (32 + 22 * i) * s, 63 * s, 20 * s, 9 * s, c)
    d.text((126 * s, 61 * s), 'BURNOUT REVENGE', font=font('ExtraBoldItalic', 46 * s), fill=WHITE, anchor='lm')
    d.text((128 * s, 104 * s), '[FOR MAC]', font=font('BoldItalic', 20 * s), fill=ORANGE, anchor='lm')

    # The band the icons' names fall on: middle grey, between two yellow rules.
    band = (ICONS_Y + 66) * s, (ICONS_Y + 98) * s
    d.rectangle((0, band[0], w, band[1]), fill=(118, 118, 118))
    d.rectangle((0, band[0] - 3 * s, w, band[0]), fill=YELLOW)
    d.rectangle((0, band[1], w, band[1] + 3 * s), fill=YELLOW)

    # Running chevrons from the app to Applications, brightest at the end.
    span = APPS_X - APP_X - 150
    for i in range(5):
        x = APP_X + 75 + span * (i + 0.5) / 5
        shade = [(90, 60, 0), (140, 90, 0), (190, 120, 0), (230, 150, 0), YELLOW][i]
        chevron(d, x * s, ICONS_Y * s, 22 * s, 10 * s, shade)

    # What to do, on an orange slab under the band.
    slab(d, 196 * s, 352 * s, 464 * s, 384 * s, ORANGE, 10 * s)
    d.text((335 * s, 368 * s), 'DRAG THE GAME TO APPLICATIONS', font=font('ExtraBoldItalic', 21 * s), fill=BLACK,
           anchor='mm')
    return img


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'assets')
    os.makedirs(out, exist_ok=True)
    render(1).save(os.path.join(out, 'dmg-background.png'), optimize=True)
    render(2).save(os.path.join(out, 'dmg-background@2x.png'), optimize=True)
    print(f'make_dmg_background.py: {out}/dmg-background.png and @2x')


if __name__ == '__main__':
    main()
