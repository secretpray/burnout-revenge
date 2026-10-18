# Changelog

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
