# Changelog

## Unreleased

- A rare black screen at launch (about 1 in 100, with Skip Intro Movies) is
  fixed: the film player's helper threads could miss a wake-up and leave the
  game waiting for them.

## v0.1.11

- Bloom and Motion Blur are sliders now, 0 to 100% (Settings > Graphics), in
  steps of 10. Bloom at 100% is the game's glow, at 0% v0.1.9's Bloom off (the
  glow kept on the bright areas, no haze, the sky as bright); between, a mix.
  Motion Blur at 100% is the game's, at 50% its streaks half as long, at 0%
  v0.1.9's Motion Blur off. Your v0.1.9 choices carry over: on reads as 100%,
  off as 0%.
- Menu sounds: the Settings menu and the first-run screens click as the game's
  front end does, with its own sounds read from your copy (sound/fe.xwb), at
  the game's Volume and Mute. Nothing of the game is shipped.
- The finished save (BurnoutRevenge-100-percent-save.zip) comes with this
  release too.

## v0.1.10

- Burnout Clips save: "Save Clip" in the replay failed every time ("continue
  without saving?"), leaving empty clips behind. A clip now saves, shows in
  Burnout Clips > My Clips with its picture, and plays in the Clip Viewer.
- The name dialog for a clip no longer says "Driver Profile" and "Start your
  story"; it says what the game asks and Done.
- Our dialogs show the same buttons as the game's prompts (Settings > Input >
  Button Prompts).
- Track skip (RB) is no longer lost in a frame where the game runs two steps,
  as Crash mode does every frame (and a race after a late frame).
- Import saves made elsewhere: Settings > Advanced > Save Backups > Import Save
  File or Import Save Folder, or `--import-save <path>` with the game closed. It
  takes an Xbox 360 save package (CON, as a USB stick's Content folder holds it),
  a folder with such packages, Xenia's content folder, or the game's Player Files.
  Your current saves are backed up first; the import applies at the next launch.
- A finished save comes with the release (BurnoutRevenge-100-percent-save.zip):
  Elite rank, every event, all 86 cars. Unzip it and import its Player Files.

## v0.1.9

- The game's own button prompts ("Select", "Exit Replay", "Camera", "PRESS TO
  BOOST!") show the buttons of the device you use: PlayStation shapes with a
  DualSense, your keys as Mac key caps with the keyboard, Xbox buttons otherwise.
  Settings > Input > Button Prompts can keep one set instead (Auto, Xbox,
  PlayStation, Keyboard).
- Bloom and Motion Blur switches (Settings > Graphics). Bloom off removes the
  game's haze and the glow round edges but keeps its bright sky and colours: the
  glow stays where it comes from instead of being switched off (which turns the
  sky grey). Motion Blur off keeps the picture sharp at speed. Both apply at once.
- Skip Intro Movies (Settings > Advanced, on by default): the EA and Criterion
  logos and the attract film at launch end at once; the title screen shows in
  about 8 seconds instead of 25.
- Settings > Audio > Music Folder is greyed out and skipped while Music is EA
  Trax.
- A note, once, on an external monitor that runs at a fixed rate 60 fps can't
  divide evenly (144 or 165 Hz), where motion stutters: it says how to set the
  screen to 60 or 120 Hz, or to Variable, in System Settings > Displays. It waits
  for a menu (never mid-race), pauses the game while open, and doesn't come back
  for that rate once closed. Never on a MacBook's ProMotion screen. From
  SpeedBreaker v0.1.7.
- Eleven system functions that stopped the game if ever reached now work as on
  the console: the CRT's sprintf, _vsnprintf and DbgPrint, the L2 lock, a
  spinlock try, the title's execution id, XAM's own memory, the "disc
  unreadable" screen (a message, then quit) and the extended message box.

## v0.1.8

- Button hints in our menus and dialogs follow the controller you use: PlayStation
  shapes (cross, circle, square, triangle; L1/R1) with a DualSense, Xbox buttons
  with an Xbox-style pad, key caps (Enter, Esc, Q/E, arrows) with the keyboard.
  The game's own hints are unchanged.
- The "What's new" note looks like the game's own prompts.
- My Music: Settings > Audio > Music plays your own MP3, AAC, ALAC, FLAC, WAV or
  AIFF files instead of EA Trax, shuffled, with "Now playing" naming each song.
  Put them in the Music Folder (Settings > Audio > Music Folder opens it). The
  game's pause, fades and crash silence apply to them as to EA Trax.
- Song names and other text outside the game's alphabet (Cyrillic, Greek, CJK)
  show in our menus and notices.

## v0.1.7

- Save backups: each launch backs up your saves when they changed (the newest 10
  are kept, in the data folder's backups/). Settings > Advanced > Save Backups
  restores one: your current saves are backed up first, the game quits, and the
  next launch puts the chosen saves in place.

## v0.1.6

- Dialogs (Quit Game, Reset page, add-ons, update notes) look like the game's own
  prompts: a white notched frame, centred text, stacked choices with the selected
  one on a yellow band, and Select/Back button hints.
- Notices appear on the game's yellow-brown description bar.

## v0.1.5

- New Settings > Graphics > Anti-Aliasing Type: SMAA 1x beside the existing FXAA.
  SMAA smooths geometry edges while keeping text and textures sharper, for about
  0.4 ms more GPU time at 1080p on M1 Pro. SMAA is the new default.
- The settings menu's help line, its dialogs and notices use the game's
  description face (condensed bold italic) instead of the generic UI font.

## v0.1.4

- Cmd+H now works in fullscreen: macOS doesn't hide an app in a fullscreen
  Space, so the game leaves fullscreen, hides, and returns to fullscreen when
  you click it in the Dock. Windowed, Cmd+H hides as usual.
- The game's sound steps back while the settings menu is open (to about a
  third, faded over 0.3 s) and returns when it closes.

The game's own menu clicks for our menus remain experimental and off: the
sound bank entries tried so far did not decode to usable sounds.

## v0.1.3

Settings menu restyled after the game's own Settings screen.

- Orange upright labels, the chosen row white between yellow lines with yellow
  arrows, volume-style bars for numbers, a SETTINGS [BURNOUT REVENGE FOR MAC]
  heading with a running chevron strip (still for Reduce motion) and
  button-glyph hints (Back B, Select A).
- Key names on the Controls page stay Latin (W, A, S, D) under Cyrillic and
  other non-Latin layouts; bindings were always physical keys.
- Advanced: version and legal notices moved into an About row.

## v0.1.2

Input focus hotfix. Includes the v0.1.1 audio-startup correction.

- Opts out of macOS Game Mode. On the test Mac, Game Mode's Bluetooth retuning
  preceded DualSense drops and the system-wide Magic Keyboard failure.
- Controller vibration requests are dropped while the game is unfocused,
  hidden, minimized or suspended; pending output is stopped on focus loss.
- Hiding or minimizing the window releases held keys; restore re-checks focus.
- Keys held with Command (Cmd+Tab, Cmd+H and other shortcuts) no longer drive the car.
- First-run screens follow one path: 01 your game, 02 install folder and car
  packs (asked once), 03 install. Burnout-style condensed italic type, hazard
  stripes and step progress; Reduce motion keeps them still.
- Once your disc checks out, the setup plays EA Trax from your own copy
  (M switches it off; Mute and volume settings apply). Nothing is shipped.

Tested on one M1 Pro with its own Bluetooth peripherals; not a guarantee for
every accessory or Mac. USB remains a fallback. Live frame generation is still off.

## v0.1.1

Audio startup hotfix after clean-install validation.

- Safer audio startup on macOS; engine, collision and music output preserved.
- Verified new-profile creation, profile reload, add-ons and active races.
- Re-tested DualSense and Magic Keyboard over USB and Bluetooth on M1 Pro.

Bluetooth passed the short candidate test after the startup correction. Broader
hardware and longer sessions still need testing; USB remains a fallback.

## v0.1.0

First experimental Apple silicon release of Burnout Revenge USA.

- Native recompilation with working menus, player profiles and tested races.
- Burnout first-run installer, cover icon and separate game/settings/save folders.
- Add-on car-pack import and automatic mounted-drive discovery.
- Engine, collisions and EA Trax audio, with empty-XMA-buffer click fixes.
- Internal rendering up to 4K, FXAA, anisotropic filtering and mipmaps.
- Bicubic, Bilinear, FSR 1 and experimental MetalFX Spatial upscaling.
- Keyboard remapping and controller support.
- SpeedBreaker 0.1.6 audio-stall, swapchain-lifetime and clean-exit improvements.
- Explicit input release on focus loss and native DualSense compatibility backend.

Live frame generation is not enabled. GPU history and offline MetalFX
interpolation are experimental developer work. Only the verified USA ISO is
supported; complete career and Bluetooth accessory reliability need more testing.
