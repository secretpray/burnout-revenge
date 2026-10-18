# The game's button prompts for the device in use

Developer note, 2026-10-18. Our own menus already draw their hints for the device
used last (`hid::LastGlyphs`: Xbox, PlayStation or keyboard). The game's own prompts
("Select (A)", "Back (B)", "Exit Replay (A)", "Camera (LB)", "PRESS (A) TO BOOST!")
always showed the Xbox 360 pad. This is where those pictures live and how they are
drawn for the device in use: Settings > Input > Button Prompts (`input.button_prompts`),
Auto by default.

## Where the glyphs live

- The front end is EA's APT (Flash-like) movies in `fe/*.bin`. Their ActionScript still
  carries the multi-platform icon table: `PS2`, `XBOX`, `XENON`; `icons/PS2/cross.swf`,
  `circle.swf`, `triangle.swf`; `icons/Xenon/A-button.swf`, `B-button.swf`; ids such as
  `XBOX301` -> `B-button`, `XBOX303` -> `A-button`, `PS2303` -> `circle`. The PS2 movies are
  not on the 360 disc, so switching the script's platform would not give PlayStation art.
- What the prompts draw is **one texture**: a 128x256 sheet, guest format 6
  (`k_8_8_8_8`), tiled, endian 2 (8-in-32: each texel's guest bytes are A R G B), no
  mips. It holds the Xbox 360 pad: the A B X Y discs, LB RB LT RT, the Start pill, a grey
  thumbstick (the game prints "LS"/"RS" over it in its own font, e.g. "Camera (RS)" in
  Car Select) and the d-pad. Texel cells: `kCells` in `runtime/gpu/pad_glyphs.cpp`.
  The menu bar ("Back (B) / Burnout Clips (X) / Driver Details (Y) / Select (A)",
  "Continue (A)") and the in-race HUD (Replay: "Camera (LB) / Exit Replay (A) /
  Hide (B)") both sample it.
- The front end and each race load their own copy at an address that changes
  (0x0868E000 and 0x0C87D000 in these runs), byte for byte the same. FNV-1a 64 of its
  131072 bytes on the NTSC-U disc: `0x37B710FB6B3237E7`.
- The race HUD's "PRESS (A) TO BOOST!" has its own picture: a 128x128 A disc, DXT3,
  tiled, endian 1 (8-in-16), no mips (0x1517B000 here). FNV-1a 64 of its 16384 bytes:
  `0xE38D9BD804031E1B`.

Found by dumping each small texture's guest bytes at its first load and decoding them
offline (Xenia's 2D untile, endian swap, DXT1/3/5, 8888) into a contact sheet. The
dump hook was temporary and is not in the tree.

## The prototype

`runtime/gpu/pad_glyphs.{h,cpp}`; two calls from `runtime/gpu/renderer.cpp`:

1. `Use()` in `LookupTexture`, before a texture's validity check (it returns at once
   for any shape but the two above). A texture of a known shape whose bytes hash to
   the disc's picture is remembered by address with a copy of the original bytes; if
   the wanted family differs, the picture for it is written over the guest copy
   (`writewatch::GpuThreadStore` + `MarkWritten`), so its pages count as written and the
   renderer's usual reload untiles the new picture. Other textures of those shapes are
   remembered with their write sequence and not hashed again until written.
2. `Refresh()` in `OnSwap`, once a frame. The renderer's `GetTexture` keeps a lookup
   while the texture's memory is unwritten, so a texture already on screen never
   reaches `Use()` again: without this, a change of device showed only after the next
   screen change (3.4 s late in one run, never in another). `Refresh()` redraws each
   known texture whose family is stale, after checking that its memory still holds the
   picture last written (write sequence unchanged, else the hash); one that doesn't
   is forgotten. The write itself makes the cached lookups miss. In the runs below the
   switch lands in the same frame as the input.

The pictures are drawn when first needed from the original (only redrawn cells change):
shapes by signed distance, labels in the setup screens' Barlow Condensed Bold (OFL,
already in the tree) through ImGui's vendored stb_truetype, and DXT3 re-encoded by a
plain endpoint-fit encoder for the boost disc.

- PlayStation: cross, circle, square, triangle in their colours on dark discs (as
  `ui/burnout_setup.cpp`'s `ButtonGlyph`), L1 R1 L2 R2, an Options pill. The stick and
  the d-pad stay the game's own (the same on both pads).
- Keyboard: key caps for the keys bound in Settings > Controls. A cell is 30 texels and
  the game scales it to its own fixed size (65 px at 4K), so words ran together: keys
  with a longer name get the symbol a Mac keyboard prints (␣ ⌫ ⇧ ⌃ ↵ ⇥ ⌥, arrows), the
  rest their name cut to three letters (Q, E, S, W, ESC by default); a d-pad cap with four arrows, and
  a blank cap for the stick (the game prints LS/RS on it: a stick is four keys and its
  click is unbound by default). A settings change redraws the keyboard set; it is
  written only if the picture changed.

Button Prompts: **Auto** follows `hid::PublishedGlyphs()`, the same choice as our menus,
published as an atomic for the command processor thread; **Xbox**, **PlayStation**,
**Keyboard** always show that set. The command processor reads the setting lock-free
(`settings::GetInt`) each time it checks a known texture and once a frame in `Refresh()`,
so a change in the menu shows in the next frame. `BURNOUT_PAD_GLYPHS=auto|xbox|playstation|keyboard`
overrides the setting for a run, as the other settings' variables do;
`BURNOUT_PAD_GLYPHS=off` (or `0`) is a diagnostic: nothing is hooked beyond one
`Enabled()` check per texture lookup.

Idea credit: Takedawn (Burnout 3 port, MIT) draws its own prompt art over the game's
known prompt textures as they are uploaded (`src/game/glyphs.c`). No code or artwork of
it is used here.

## Open points

- Strings that name a button in words ("press A") would still say A; none were seen.
- A texture freed by the game and not yet written again keeps being redrawn on a
  device change (harmless picture data in memory nothing uses yet); one written again
  is dropped by the hash check before any write.
- Other discs (PAL, other revisions) hash differently and are left alone.
- The PS stick: "RS" printed on a grey stick reads fine, but PlayStation calls the
  click R3.

## Validation

Runs from `speedbreaker/build/glyphs-research` (`run.py` as in the 0.1.8 validation;
own data and cache copies; the game from the default install; virtual pad and
`BURNOUT_INPUT_SCRIPT`; captures with `BURNOUT_UI_KEYS` Shot only, plus one F8 key press
for the keyboard switch). Captures and logs stay there, never in the repository.

| Run | `BURNOUT_PAD_GLYPHS` | What the captures show |
| --- | --- | --- |
| 09-off | `0` | The game's own prompts: "Continue (A)", "Camera (LB) / Exit Replay (A) / Hide (B)". No `[glyphs]` line. |
| 02, 04, 08, 10 | `playstation` | Menus "Continue ✕", "Driver Details △ / Select ✕"; Replay "L1 Camera / Exit Replay ✕ / Hide ○". The sheet found at its front-end and race addresses. |
| 05 | `playstation` | "PRESS ✕ TO BOOST!" (the DXT3 disc redrawn and re-encoded). |
| 03 | `keyboard` (word labels, since replaced by symbols) | "Continue SPACE", "Driver Details CTRL / Select SPACE", Replay "Q Camera / Exit Replay SPACE / Hide BKSP". |
| 11-kb | `keyboard` | Symbol caps: "Continue ␣", "Back ⌫ / Burnout Clips ⇧ / Driver Details ⌃ / Select ␣", Car Select "RS Camera / Change Color ⌃". |
| 12-xbox, 12-playstation, 12-keyboard | unset; `input.button_prompts` = xbox / playstation / keyboard in the profile's settings.toml | Each set from the setting alone: "Continue (A)", "Back (B) / Burnout Clips (X) / Driver Details (Y) / Select (A)"; the same with ✕ ○ □ △ (Car Select "RS Camera / Change Color △ / Back ○ / Select ✕"); key caps ␣ ⌫ ⇧ ⌃. |
| 13-live | unset; stored Xbox | Settings > Input > Button Prompts changed to PlayStation with Right while open (logged 23.4 s): "Continue (A)" behind the menu became "Continue ✕" in the next frame (23.5 s), and stayed after the menu closed. |
| 07-auto | `1` | PlayStation at start (a DualSense paired), F8 at 18 s: keyboard caps; the virtual pad's RS at 26 s: Xbox; F8 at 32 s: keyboard again (Car Select: "RS" printed on the blank stick cap). Each switch logged in the frame of its input. |

Every run exited cleanly when bounded, with no crash and no unimplemented import.
