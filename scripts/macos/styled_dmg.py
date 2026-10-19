#!/usr/bin/env python3
# SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#
# styled_dmg.py <app> <output.dmg> <background.tiff> <volume icon.icns>: the
# download's disk image with its Finder window laid out: the background
# (make_dmg_background.py), the app and a shortcut to Applications at the
# centres the background draws, no toolbar, sidebar or status bar, the app's
# icon on the volume. Made with dmgbuild (Alastair Houghton, MIT:
# https://github.com/dmgbuild/dmgbuild, pinned to 1.6.5), which writes
# Finder's .DS_Store itself: nothing drives Finder. make_app.py --dmg runs
# it with the Python that has dmgbuild (DMG_PYTHON), then checks the image
# as it checks a plain one: HFS+, compressed (UDZO), the app and the
# Applications shortcut at the top, the app's signature whole.
import os
import sys

import dmgbuild

# The window and where the icons go; make_dmg_background.py draws for these.
# The height is the whole window's, its title bar and Finder's bars included:
# 456 points of picture under macOS 27's title bar and toolbar (68), the
# design's height; with the path and status bars on (56 more) the window
# shows 400, still past the icons' names, so it never scrolls.
WINDOW = (660, 524)
ICONS = {'Burnout Revenge.app': (165, 205), 'Applications': (495, 205)}


def main():
    if len(sys.argv) != 5:
        raise SystemExit('usage: styled_dmg.py <app> <output.dmg> <background.tiff> <volume icon.icns>')
    app, dmg, background, icon = sys.argv[1:]
    settings = {
        'format': 'UDZO',
        'compression_level': 9,
        'filesystem': 'HFS+',
        'files': [(app, 'Burnout Revenge.app')],
        'symlinks': {'Applications': '/Applications'},
        'icon': icon,
        'background': background,
        'window_rect': ((200, 140), WINDOW),
        'default_view': 'icon-view',
        'show_status_bar': False,
        'show_tab_view': False,
        'show_toolbar': False,
        'show_pathbar': False,
        'show_sidebar': False,
        'show_icon_preview': False,
        'icon_size': 128,
        'text_size': 13,
        'arrange_by': None,
        'label_pos': 'bottom',
        'icon_locations': ICONS,
        # No hide_extensions: it sets com.apple.FinderInfo on the app, which
        # its signature doesn't allow (codesign --strict); Finder hides ".app"
        # by default anyway.
    }
    if os.path.exists(dmg):
        os.remove(dmg)
    dmgbuild.build_dmg(dmg, 'Burnout Revenge', settings=settings)


if __name__ == '__main__':
    main()
