# Credits and provenance

- **SpeedBreaker**, project(u) and contributors: the GPL-3.0-or-later runtime,
  Xbox 360 kernel/XAM implementation, Vulkan renderer, audio, installer, settings,
  packaging and tools. Baseline v0.1.4 (`d4c206a`), integrated v0.1.5 and v0.1.6
  (`9d2cb93`). https://github.com/SpeedBreakerProject/speedbreaker
- **Xbox360-Native-Ports**, CrownParkComputing: Burnout Revenge USA function and
  switch-discovery research and add-on reference. Donor release `burnoutrevenge-v1`;
  inspected metadata snapshot `2675c16edd4b0854badb30f9cd519ab899798388`. Only address,
  boundary and switch metadata were used; generated game implementation files are
  not redistributed here.
  https://github.com/CrownParkComputing/Xbox360-Native-Ports
- **XenonRecomp**, hedge-dev and contributors (MIT): static recompilation;
  local patches retain their own authorship.
- **Xenia** contributors (BSD): console GPU, kernel and audio research, plus
  instruction tests. Source comments identify implementation references.
- **AMD FidelityFX Super Resolution 1** (MIT): unchanged EASU and RCAS headers,
  incorporated through SpeedBreaker v0.1.5.
- **aleszczynskig**: transparent macOS title bar (SpeedBreaker PR #23).
- **love-gamer**: keyboard remapping contributions to SpeedBreaker.
- **byteofmydream**: USB steering wheel support (SpeedBreaker PR #29, branch
  `claude/macos-usb-steering-wheel-dca9ae` at `ae418a8`): `hid/wheel.*`,
  `hid/wheel_map.h`, Settings > Wheel and `wheel_map_test`, adapted here to
  Burnout's controls (no gear shift).
- **Xerenge**, shipa-2: the Burnout Revenge research this port leans on most —
  the music-skip finding (`sub_8210F940`), boma's motion-blur patch as Xerenge
  applies it, the Takedown-camera and rendering notes, and the online protocol
  facts studied for the planned online mode. No source is copied so far; the
  code here is our own, with attribution in its comments. Xerenge is
  GPL-3.0-or-later since `c023338` (2026-10-19), and its author agreed to our
  adapting its lobby server (`ealobby`) and online notes, with credit kept.
  https://github.com/shipa-2/Xerenge
- **Fukami**, danisandoval (GPL-3.0; a Ridge Racer V port): the ideas behind
  Settings > Graphics > Vehicle Detail (its `car_lod`), players by controller
  with failover (`rrv_pad_slots.h`, here `hid/pad_slots.h` for four users),
  crash-safe saves (its memory card's temporary file, fsync and rename, here
  `user/save_commit.h`) and the load-time measurement (`docs/LOADING.md`).
  The code here is our own. https://github.com/danisandoval/fukami
- **Takedawn**, xenophobentx (MIT; Burnout 3): research behind Vehicle Detail
  (Burnout 3's model switch distances), save import, the race HUD at a wide
  screen's edges, and more. https://github.com/xenophobentx/takedawn
- **secretpray**: Burnout Revenge macOS integration, testing and project direction.

Additional bundled components, their authors and licenses are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `LICENSES/`.

Changing the product's name does not remove inherited copyrights. The upstream
project name in source license headers is retained deliberately.

## External port research

[Xerenge](https://github.com/shipa-2/Xerenge), its
[xenophobentx fork](https://github.com/xenophobentx/Xerenge),
[Takedawn](https://github.com/xenophobentx/takedawn) and its
[xboxrecomp toolkit](https://github.com/xenophobentx/xboxrecomp) were studied for
input lifecycle, graphics preparation and interpolation approaches, and
[Fukami](https://github.com/danisandoval/fukami) (`2ecb281`, v1.1.0) for car
detail, controller slots, safe saves and loading. No source blocks are copied
from them. See the pinned revisions and boundaries in
[the adoption plan](docs/EXTERNAL_PORTS_PLAN.md).

Settings > Graphics > Motion Blur follows boma's Burnout Revenge game patch for
Xenia, as Xerenge applies it (the motion blur's frame counter reads as finished);
the code is our own. Bloom off does not use that patch set's bloom half (it greys
the sky): it is our own composite change, `runtime/gpu/burnout_bloom.h`.

The track skip lost between two pad polls of a frame was found by Xerenge (its
`music_trace.cpp`, the addresses `sub_8210F940` and `0x82846670`); our hook is
`runtime/game/music_skip.cpp`. Save import follows Takedawn's "Import Xbox save"
(MIT) as an idea; the STFS reader `runtime/user/stfs.h` is ours, after Xenia's
STFS layout (BSD-3-Clause), as the add-on reader is.
