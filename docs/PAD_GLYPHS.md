# The game's button prompts for the device in use

Settings > Input > **Button Prompts** chooses Auto, Xbox, PlayStation or
Keyboard. Auto follows the device used last. The choice applies to the port's
menus and the game's own prompts, including the race and replay HUD.

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
  with identical bytes. FNV-1a 64 of its
  131072 bytes on the NTSC-U disc: `0x37B710FB6B3237E7`.
- The race HUD's "PRESS (A) TO BOOST!" has its own picture: a 128x128 A disc, DXT3,
  tiled, endian 1 (8-in-16), no mips. FNV-1a 64 of its 16384 bytes:
  `0xE38D9BD804031E1B`.

## Implementation

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
   reaches `Use()` again. `Refresh()` redraws each
   known texture whose family is stale, after checking that its memory still holds the
   picture last written (write sequence unchanged, else the hash); one that doesn't
   is forgotten. The write itself makes the cached lookups miss.

The pictures are drawn when first needed from the original (only redrawn cells change):
shapes by signed distance, labels in the setup screens' Barlow Condensed Bold (OFL,
already in the tree) through ImGui's vendored stb_truetype, and DXT3 re-encoded by a
plain endpoint-fit encoder for the boost disc.

- PlayStation: cross, circle, square, triangle in their colours on dark discs (as
  `ui/burnout_setup.cpp`'s `ButtonGlyph`), L1 R1 L2 R2, an Options pill. The stick and
  the d-pad stay the game's own (the same on both pads).
- Keyboard: key caps for the keys bound in Settings > Controls. A cell is 30 texels and
  the game scales it to its own fixed size, so keys
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

## Scope

Other disc revisions have different texture hashes and are left unchanged.
Only known prompt textures are replaced. Button names written into ordinary
text remain the game's text, and the stick labels printed by the game retain
its terminology.
