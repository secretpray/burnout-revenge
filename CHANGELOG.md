# Changelog

## Unreleased

- Optional Hold Crashbreaker in Settings > Mods > Cheat Codes replaces
  repeated charge presses in solo World Tour Crash. Release stops the repeats;
  the native charge timer keeps running. Launch and explosion mechanics are preserved.

## v0.1.27

- Internal Resolution shows its render dimensions, including the size
  currently selected by Auto. Window size and upscaling do not change this
  readout. Render resolution and screen-shape behavior are unchanged.
- Tap Home / Xbox Guide / PS and release to open or close Settings. Hold it
  for three seconds to open the quit confirmation instead. Cancel is selected
  first; B / Circle or Escape dismisses it. Releasing a long hold cannot open
  Settings, and continuing to hold cannot repeat the prompt.
- A hold is cancelled if the window loses focus, the controller disconnects,
  or another dialog takes over. The quit dialog follows the controller that
  invoked it and preserves an open Settings page.
- On macOS, Burnout Revenge > Settings… and Cmd+, use the same settings
  screen. Holding the shortcut does not repeatedly open and close it; other
  dialogs are not interrupted. Adapted from SpeedBreaker PR #47 by SOHAM4884.

## v0.1.27-linux-beta.1

- Experimental Linux x86-64 build with a shared runtime, Vulkan rendering,
  XDG data paths and FFmpeg support for My Music.
- Install for your user with `./install.sh`, then launch from the applications
  menu. Updates and app removal preserve the installed game, saves and settings.
- Linux testing uses UTM on an Apple silicon Mac, scripted controls and muted
  audio. Native Linux PC GPUs, Steam Deck performance, physical controls and
  listening have not been verified. See the Linux release's validation matrix
  and known limitations before using it.
- Invalid Vulkan/window initialization now stops startup before the game runs.
  The stable Mac download remains v0.1.26.

## v0.1.26

- Home / Xbox Guide / PS opens the quit confirmation while the game window
  is active. Cancel is selected first, and B / Circle returns to the game or
  the settings page that was open. Holding the button cannot repeat the prompt.
  Availability depends on the controller driver and system shortcuts.
- The dialog follows the controller that opened it, including player 2, and
  keeps an open Settings page intact. Other dialogs, control assignment and
  calibration are not interrupted.
- While the game window is focused, macOS is asked to give Home to the quit
  dialog instead of its system gesture. System handling resumes when focus
  leaves; your Game Center / Games shortcut settings stay unchanged. macOS
  may still reserve the button, and Settings > Quit Game remains available.

## v0.1.25

- Mods now has Cheat Codes and Modifications sections. Unlock Madden Van uses
  the game's original trailer reward without requiring 100% completion. Save
  your Player File afterwards, then select the van in a Crash event's garage.
- Classic Crash Launch works in solo World Tour and network Crash. Each network
  player can choose the classic meter or the Xbox 360 start. Changes apply on
  event Retry or the next event, without restarting the app. Tap and release
  A/Cross to start, then press separately to set speed and control.
- Solo driving Codes, Rewind and camera controls work while signed in to Live.
  Restrictions follow actual network and local multiplayer sessions; the
  Online Play switch no longer disables solo options.
- Controller disconnects preserve player slots in split screen. A replacement
  joins the first missing player with a fresh Start press. Release the controls
  before resuming; reconnecting a spare cannot silently swap the cars.
- A one-time notice explains the affected Xbox USB vibration route on macOS
  and suggests Bluetooth. It includes player 2 and remembers dismissal.
- My Music indexes folders and song metadata in the background. Audio settings
  and playback share the ready list; EA Trax plays while it loads. Missing
  folders and scan limits are shown in all nine interface languages.
- Leaving a reward video resumes the soundtrack through the game's own routine,
  including when skipped with Start. Adapted from Xerenge's v0.4.8 fix for #29.

## v0.1.24

- Classic Crash Launch in Settings > Mods: the PS2 and original Xbox launch
  meter for offline solo World Tour Crash. It is off by default and requires
  a restart. Press the launch button to start, again to set speed, then again
  to set control. The original nonlinear meter rules, per-car thresholds,
  launch outcomes, engine controls and failure deadlines replace the Xbox 360
  countdown. Time and traffic continue while the meter is active.
- Reconstruct the curved meter with flame animation and artwork read from
  your installed game. Launch prompts follow the selected interface language
  and input glyphs; outcome messages use the game's own localized HUD.
  The port uses Xbox 360 fonts and effects; exact PS2/Xbox visual and sound
  parity is not claimed.
- Preserve launch controls in the current event's Replay, including Stall and
  Blown outcomes and seeking back to the start. Rewind becomes available after
  the meter finishes. The launch recording is held in memory and is not stored
  in saved Burnout Clips; their playback can differ.
- Rename Settings > Codes to Mods, with Classic Crash Launch after the five
  existing cheats. Existing cheat settings remain compatible.

## v0.1.23

- Dutch, Swedish and Finnish for all port menus and original game text.
  Import verified language files from either supported European disc, select
  Nederlands, Svenska or Suomi under Advanced > Language, then restart.
  The USA executable, saves and original audio are preserved. Achievement
  names and requirements use English where the original database has no text.

- Settings > Display: a live field-of-view offset from -20 to +20 degrees
  for ordinary solo World Tour driving, with the original view at 0.
  Native speed/boost FOV changes and aspect-ratio correction are preserved.
  Crash/Aftertouch cameras, Replay, mirrors, menus and multiplayer retain
  their original view. The option does not alter saved camera state.

- Display > Chase Camera Distance: 75–150% of the native longitudinal
  offset during ordinary solo World Tour chase-camera driving. 100% keeps
  the original. Changes take effect when play resumes, with the original
  obstacle checks; other camera modes keep their native position.

- Display > Camera Shake: reduce native additive position/rotation effects
  in the solo chase view, from the original 100% down to 0%. Steering response,
  obstacle checks, vibration and FOV effects remain native. Crash/Aftertouch,
  Replay and other camera modes retain their original effects.

## v0.1.22

- Fix guest memory release/decommit on hosts with pages larger than 4 KiB
  clearing adjacent allocations. Physical memory mirrors remain shared.
- Experimental Rewind for solo World Tour Race, Road Rage, Traffic Attack,
  Burning Lap, Eliminator and Crash, off by default.
  Settings > Rewind provides 10/20/30-second requests, editable keyboard bindings
  and separate controller combinations for each duration. Hold a combination for
  0.3 seconds and release it fully before another request. Ambiguous combinations
  are refused. Immediate notices show progress, actual restored time or the
  reason a request is unavailable. In road events, targets inside a wreck/Aftertouch
  use the preceding driving checkpoint; requests during the crash camera wait for
  driving to resume. Streamed track collision is prepared before restoring cars.
  See [current controls and limits](docs/REWIND_EXPERIMENT.md).

## v0.1.21

- Six interface languages: English, Japanese, German, French, Spanish and Italian.
  Choose a language during first-run setup or under Settings > Advanced, then
  restart to apply a change made in Settings. Labels, help, dialogs, Codes,
  achievement status, update checks and server checks share one catalogue.
- Import verified game translations and the original Japanese font from your own
  European Xbox 360 disc image in Advanced settings. The port keeps its USA
  executable and your saves. Missing or damaged language resources fall back
  to English game text, with a notice. Achievement names and requirements use
  the language tables already in the game executable.
- Environment overrides show their active value in Settings, including Language,
  and disable editing until the override is removed.
- Long translated labels fit the settings rows and action buttons. Setup pages
  can scroll on short windows, and Japanese glyphs are available in all menu fonts.

## v0.1.20

- Settings > Achievements: the 36 original Xbox 360 achievements (1000 G),
  with requirements for the selected entry, earned dates and local Gamerscore.
  Locked icons are grey; earned icons are in color. Names and artwork come
  from your own copy of the game.
- The game awards achievements under its original conditions, offline and
  online. New awards appear over play without taking input, are saved
  immediately and are not announced again after a restart. Progress is local;
  there is no Xbox Live, Steam or RetroAchievements account synchronization.
- A wider settings menu gives the tabs more room. Small windows scroll the
  tabs instead of shortening their names.

## v0.1.19

- Settings > Codes: five offline modifiers for solo World Tour, all off by
  default and applied live. Infinite Boost keeps a full level 4 boost bar
  (turning it off restores the earlier level). Infinite Time stops the event
  countdown; turning it off resumes from the frozen time. Quick Money raises
  each Traffic Attack award to at least $30,000. Quick Takedowns sets the
  event's Takedown count to 99 after the first real Takedown, so the HUD,
  medal and results agree. Easy Boost fills the current bar on a Near Miss
  and lowers the Near Miss and drift thresholds.
- Each Code is checked in the game's own hook: local Multiplayer, Crash,
  Burnout Clips, AI cars and any run with Online Play enabled are rejected,
  even when an environment variable forces a Code on. Career progress made
  with Codes is saved normally.

## v0.1.18

- Fix an invalid memory write when the game requests an Xbox Live gamer
  picture: a missing argument made the picture height look like a completion
  pointer. Pictures remain unavailable without Xbox Live.
- Online sessions report allocation failure correctly and reject null outputs
  and the invalid handle sentinel. Friendship queries report missing users
  and complete asynchronous results without claiming anyone is a friend.

## v0.1.17

- Ranked matchmaking: Xbox Live > Quick Match > Ranked can join a room,
  and Create Match > Ranked: Yes can host one. Matches use the same
  player-to-player protocol as Player Match. There is no global ranking,
  TrueSkill service or saved leaderboard; World Rank stays unavailable.
- The bundled server and LAN lobbies keep Ranked and Player Match searches
  separate. With no matching room, Quick Match offers to create one instead
  of showing a connection problem.

## v0.1.16

- Online play, off by default: Settings > Online > Online Play. Races,
  Road Rage, Crash Party, Crash Tour and Crash Battle with up to four
  players, and Burnout Clips shared, watched and downloaded. Players meet
  on the Xerenge community server (offered with its author's agreement), a
  server of your own (the app has it: burnout-lobby), or the local
  network with no server at all. The lobby protocol and server are
  Xerenge's (shipa-2, GPL-3.0-or-later), so Mac players meet Xerenge's.
  Choose a gamertag; Check Server says whether the server is up. After a
  failed sign-in the server isn't asked again for a while (5 s, growing to
  a minute). Ranked matches don't work yet. Tested with up to four copies
  on one Mac and on the community server; not yet between two Macs, so
  reports are welcome. Ports, firewall and your own server: docs/ONLINE.md
  with the game's source.
- The game no longer quits when OK is pressed on Xbox Live's "Connection
  problem": it closed a handle it had never opened, which ended the game.

## v0.1.15

- Settings > Graphics > Vehicle Detail: how far away cars and traffic keep
  their detailed models. Original is the game's (the full model within 30 m,
  the simplest beyond 90 m); Far doubles the distances and Farthest makes
  them four times as long, so cars ahead stop changing shape as you close on
  them. Far is the default. Applies from the next frame. Measured on an M1
  Pro: 60 fps either way, up to 7% more draws with every rival in view and
  no GPU time to measure at 1x or 2x. The idea is Takedawn's and Fukami's.
- Two controllers, two players: each controller is a player in the order
  they connect (up to four), so Multiplayer's split screen works with a
  second pad ("Player 2 please press START"), and each player's vibration
  goes to their own pad. The keyboard and a wheel stay player 1's. If
  player 1's controller goes and no other is waiting, the next one becomes
  player 1, so one controller seen twice (Bluetooth and USB at once) is
  never left as player 2. After Fukami's controller slots.
- Saves survive a crash or power cut mid-save. The game empties its save
  file and then writes it again; now it writes to a copy, which is flushed
  to the disk and swapped in whole when the game closes the file, so the
  previous save stays intact until then. A copy a crash left behind is
  thrown away at the next start. After Fukami's memory card writes.
- An install that doesn't finish leaves nothing behind. Cancelled or
  failed, it already removed its partial copy; now the folders it made for
  the game go too. Cut short by a crash, a forced quit or a power cut, it
  left a partial copy of up to 8 GB that only a new install into the same
  folder cleared; the next start now removes it, wherever it was, with the
  folders made for it (a folder with anything of yours in it is kept). An
  add-on pack copy that fails, or that a crash stopped, is removed too.

- The download opens on a window in the game's look: hazard stripes, the
  name in heavy italic capitals, and chevrons running from the game to
  Applications. The mounted disk shows the game's icon.
- The finished save (BurnoutRevenge-100-percent-save.zip) comes with this
  release too.

## v0.1.14

- USB steering wheels (the Logitech G29 and its family; other wheels SDL
  sees follow): the wheel steers, the pedals are gas and brake, the face
  buttons, D-pad and Share + Options (the settings) work as on a DualSense,
  and the paddles are RB and LB. Settings > Wheel appears while a wheel is
  plugged in: Steering Range, Deadzone, Response and a Calibrate row that
  learns the wheel's ends, each pedal and every button. Force feedback
  (the game's knocks and a centring force) goes through SDL's haptics where
  the system drives the wheel's motors; where it can't, the page says so
  and everything else works. Adapted from SpeedBreaker's wheel support
  (PR #29); tested on a virtual G29, not yet on a real wheel.
- Screens narrower than 16:9 (16:10, 4:3): the race HUD goes to the top and
  bottom edges as it goes to an ultrawide's sides - the title, clock and
  TAKEDOWNS at the top, the rating and speed at the bottom, the crash
  camera's strips and CRASHBREAKER at the edges - while the event's messages
  stay in the middle (Settings > Display > HUD at Screen Edges). The menus'
  frame reaches the top and bottom too.
- The finished save (BurnoutRevenge-100-percent-save.zip) comes with this
  release too.

## v0.1.13

- Ultrawide screens (21:9, 32:9): the race HUD sits at the screen's corners -
  takedowns and boost on the left, the target, rating and speed on the right,
  the clock in the middle - each part at its own size and shape (Settings >
  Display > HUD at Screen Edges; off puts the whole HUD in a 16:9 block in the
  middle as before). The crash camera's tyre-track strips reach the screen's
  edges, and so does the menus' grimy frame. On a 16:9 screen nothing changes.
- The finished save (BurnoutRevenge-100-percent-save.zip) comes with this
  release too.

## v0.1.12

- A rare black screen at launch (about 1 in 100, with Skip Intro Movies) is
  fixed: the film player's helper threads could miss a wake-up and leave the
  game waiting for them.
- Choosing Xbox Live in the main menu no longer closes the game: offline, the
  game's network calls now answer as a console without a connection, and the
  menu stays.
- The finished save (BurnoutRevenge-100-percent-save.zip) comes with this
  release too.

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
