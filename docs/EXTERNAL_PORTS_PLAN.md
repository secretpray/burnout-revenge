# External port research and adoption plan

Reviewed 2026-10-18. Repository snapshots were read locally; donor games were not
built or benchmarked. A README claim is not a result on this M1 Pro. No donor
scripts were executed, game assets imported, or runtime architecture replaced.

## Source snapshots

| Project / revision | Relevant evidence | Transfer decision |
| --- | --- | --- |
| [shipa-2/Xerenge](https://github.com/shipa-2/Xerenge/tree/22dad21de4dd81eec048c8333ca86fa91de910b3), `22dad21` (2026-10-17) | ReXGlue/Plume; EU and USA builds; disc-derived shader recipes; fixed-step logic; post effects; draw-based frame interpolation | Best source of Burnout-specific research. Own runtime differs from our Xenos command-processor path; adapt concepts and verify USA sites. |
| [xenophobentx/Xerenge](https://github.com/xenophobentx/Xerenge/tree/98aba269aec49489e1cafe09f7669c18a5eefad2), `98aba26` (2026-10-06) | Earlier ReXGlue/Plume tree, EU-focused README | Not an independent newer feature set. Compare individual fixes, prefer current upstream evidence. |
| [Takedawn](https://github.com/xenophobentx/takedawn/tree/5a00fc10f9548a189e9043c5a55975863ad49175), `5a00fc1` (2026-10-17) | Original Xbox/x86/NV2A, native Metal; shader coverage manifests; SMAA/CAS; save backups; custom music; controller glyphs | Strong product/runtime reference. Its Xbox 1 patches, addresses, graphics pipeline and surround DSP are not Xbox 360 drop-ins. |
| [Takedawn input toolkit](https://github.com/xenophobentx/xboxrecomp/tree/5ae8ad90d984425e834dee759592c15592679880), `5ae8ad9` | SDL controller lifetime on event thread; keyboard shortcut handling; background rumble gating; stop/drain on exit | Adapt the main-thread and focus/lifetime rules to SDL3, with local tests. |
| [Xerenge SDK input](https://github.com/shipa-2/rexglue-xerenge/blob/e029bb71baee4f0698ce6b487c97c6ba04aecc79/src/input/sdl/sdl_input_driver.cpp), `e029bb7` | SDL3 subsystem initialization dispatched to UI thread; hotplug table protected by mutex; `OnLostFocus`/`OnGotFocus` empty | Useful lifecycle comparison, not an existing solution to this Magic Keyboard failure. |
| [fabioap-cpu/Burnout-Revenge-Recomp](https://github.com/fabioap-cpu/Burnout-Revenge-Recomp/tree/0c15c98a1a235384304b228afa6e0a6e004fd3ec), `0c15c98` (2026-09-03) | Windows/ReXGlue, real-present counter and controlled rendering diagnostics; known missing world behind frontend | Reuse diagnostic methodology. It does not demonstrate a Mac input fix or eliminate our unsupported imports. |

Additional primary references found: [Xenia title patches](https://github.com/xenia-canary/game-patches/blob/main/patches/454107DC%20-%20Burnout%20Revenge.patch.toml)
(boma's bloom/blur patches), [Burnout CRASH! recompilation](https://github.com/mpkonmbk/burnout-crash-recomp)
(different XBLA title), and [sp00nznet/burnout3](https://github.com/sp00nznet/burnout3)
(Takedawn's original Xbox foundation). The latter two are context, not preferred
Burnout Revenge integration targets. FreeBurn PS2 appeared in search results;
no public source snapshot was verified, so it is not treated as reusable code.

## Licensing and integration boundaries

At the checked Xerenge main/fork revisions there is no top-level LICENSE/COPYING;
only the vendored SMAA license was found in tracked paths. Do not infer a grant
for the project's own Plume/game files from its public visibility. No such code
has been copied. Its separate SDK input file explicitly retains the Xenia BSD
notice; check that file's upstream license and notices before an actual port.
Takedawn is MIT; its toolkit has component-specific notices (including LGPL audio),
so select files by provenance rather than assigning the whole dependency one
license. We can obtain SMAA/CAS from their explicitly licensed original projects.
The input changes in this tranche are our implementation of lifecycle rules,
with research attribution; no source block was copied from these projects.

Switching the whole port to ReXGlue/Plume would replace working CPU/kernel/GPU
paths, build tooling and Mac presentation. It is an alternate backend project,
not a feature merge. Preserve our working USA pipeline and Most Wanted build.

## Stage 0 — physical input and release gate (in progress)

**Problem:** Magic Keyboard fails system-wide after returning to Burnout with a
Bluetooth DualSense, including downloaded v0.1.1 run 93. The user had to power-cycle
it. No gamepad-removal events in that run; this is not a passing keyboard check.
USB short comparison passed. Audio initialization on the main thread is corrected,
but does not solve the keyboard issue. v0.1.1 stays a draft.

Takedawn's `src/input/keyboard_sdl.c` clears held keys on focus/hide/minimize and
excludes Command shortcuts. Its `xinput_device.c` keeps SDL device operations on
the event thread, gates motors for background windows, and drains pending output
before close. These rules are useful; the donor's unmeasured Magic Keyboard
behavior is not evidence that any one rule solves our failure.

Implemented locally in the first tranche:

- Independent focus and app-suspension gates for rumble. Previously focus loss
  sent one stop, but the following guest request could restart motors in the
  background. Every subsequent request is now filtered until both gates allow it.
- Hide/minimize clear local key state and suppress output too. Show/restore
  recheck actual focus; showing a window does not imply keyboard focus.
- Command shortcuts do not also become driving/menu keys in the keyboard map.
  Custom scancode bindings remain intact. No global keyboard hook is added.
- Connection transport (wired/wireless/unknown) and rumble errors are logged;
  no hardware serial or Bluetooth address is collected by the new diagnostics.
- Regression test exercises new guest requests while inactive and both orders
  of independent focus/suspension changes.

All SDL gamepad open/close/poll operations were already on our main thread. No
benefit is claimed from moving them again. SDL itself deduplicates unchanged
motor levels: do not label repeated guest requests a proven Bluetooth flood.
Takedawn uses SDL3 3.4.18 through sdl2-compat; ours is 3.4.16. The official
[3.4.16...3.4.18 comparison](https://github.com/libsdl-org/SDL/compare/release-3.4.16...release-3.4.18)
contains a Cocoa cursor-main-thread fix and macOS 27 Steam-controller duplicate
fix, but no identified DualSense/Magic Keyboard correction. Do not blindly upgrade
and call the issue fixed. A separate SDL-version comparison remains an experiment.

Virtual-only event regression passed in run 95 (24.1 s, exit 0, two focus cycles).
Full suites passed: standalone 61/61, shared Burnout 63/63, Most Wanted 62/62.
Next: a manual
Bluetooth test with real typing after each direction of Cmd+Tab, repeated hide /
restore and normal exit. Compare USB separately. Keep logs private and report
keyboard and pad outcomes independently. Only passing physical checks can clear
the release gate. Stop/drain on exit and short renewable rumble leases are a
follow-up experiment, not a confirmed Bluetooth repair. An isolated SDL probe
without game/Vulkan has now been built for the next controlled comparison; its
physical checks remain pending.

## Stage 1 — optional graphics preparation (next performance feature)

Xerenge's `rexglue/tools/runtime_shaders.recipe` reconstructs runtime-patched
shaders from executable/graphics templates and hashes. Takedawn's
[prebuild design](https://github.com/xenophobentx/takedawn/blob/5a00fc10f9548a189e9043c5a55975863ad49175/docs/shader-prebuild.md)
adds coverage keys plus device-local warmup, not a downloadable foreign driver
cache. This directly addresses first-visit hitches, not steady-state FPS.

Our renderer already persists SPIR-V and `VkPipelineCache`; it does not persist
complete draw-pipeline descriptions for preparing every known pipeline before
its first draw. Three distinct steps are needed:

1. Record shader/pipeline coverage and compile-time misses privately, with the
   supported USA executable identity. Export a reviewed metadata-only manifest.
2. Reconstruct shader bytes only from the user's verified disc; check every
   template/result hash. Xerenge's shader translator/hash scheme differs from
   ours, and its recipe's USA coverage must be measured, not assumed.
3. Prepare actual Vulkan pipeline state variants (formats, vertex layout,
   topology, blending/depth), not only shader modules. Cache identity includes
   schema/translator, game build and driver/GPU as appropriate. Allow cancellation,
   resume and normal first-use fallback. No use or mutation of the player's save.

Then add optional **Prepare graphics** after ISO/add-on selection and **Prepare
again** in Settings. Show real completed/total counts, not a fabricated percentage.
Acceptance: cold versus prepared identical race + first crash + menus, p50/p95/p99
frame times, compilation counts and duration; warm-cache restart and a rejected
foreign/truncated cache. No promise of zero hitches for unvisited content.

## Stage 2 — image quality without new temporal history

- SMAA 1x (official MIT implementation) as an alternative to the existing FXAA.
  Three passes and lookup textures; preserve correct color space and 16:9 band.
- Compare dedicated CAS against existing sharpening/FSR RCAS before adding a
  duplicate control. Match sharpness perceptually and test halos/HUD readability.
- Audit low-resolution bloom/downsample taps at 1080p. Xerenge identifies shimmer
  when 720p offsets sample a larger target; our different resolve path needs a
  same-scene test before changing it. Do not simply disable bloom: it also supplies
  sky brightness. Keep original look as default.
- Optional LOD/detail/blur controls only after confirming USA function signatures
  against the executable and testing multiple tracks. Blind Xenia byte patches
  do not execute in a statically recompiled function.

Acceptance: same scene/camera at 720p/1080p, lossless crops, GPU execution time,
frame-time tails, and separate HUD inspection. Our current 1080p/Bicubic recommendation
remains until a measured alternative wins. HDR and HUD artwork upscaling are later
work, not automatic consequences of SMAA.

## Stage 3 — frame interpolation / smooth Crash mode

Xerenge has two distinct techniques: uncapped rendering with fixed logic steps,
and `plume_framegen.cpp` replaying matched draws between two complete states.
The latter interpolates placement constants, not DLSS/FSR optical flow. It keeps
logic unchanged and adds roughly one source-frame of look-behind latency. Draw
identity includes pass/target, shader, buffers and index range; identical traffic
meshes require disambiguation. HUD/particles and unmatched draws take the newer
state; camera cuts bypass interpolation. Default 30-to-60 Crash interpolation in
that backend is not proof of correctness or speed in ours.

Evaluate two paths against the same captures:

1. Continue our MetalFX Frame Interpolation: GPU history leases/events, producer /
   consumer ownership, depth/motion/HUD validity, cut/resize resets, pacing and
   fallback. Existing object-motion/history work is retained.
2. Prototype our own draw-state interpolation using verified object identities
   and transforms. Re-rendering may be more expensive than MetalFX and introduces
   ambiguities for reflection passes, transparent/skinned meshes and particles.
   It cannot be copied as a small Plume shader patch into our renderer.

Start with 30-to-60 Crash on a 60 Hz display, then 60-to-120 on real 120 Hz output.
Measure visual defects, actual distinct presented frames, input latency, GPU/memory
cost and stable speed of physics/music. No duplicated-frame FPS counter as success.
No 144 FPS promise. Live FG remains OFF until this gate passes.

## Stage 4 — product features with independent value

Ordered after input and shader preparation: bounded atomic save backups with
restore/validation; controller-family prompts and last-used input detection;
custom local soundtracks with loudness/headroom control; more complete add-on/car
coverage. Existing installer, FSR 1, settings, add-on import, bug-report zip and
basic AF/sharpening already exist: do not add them again as donor features.
Surround/HDR, broader career testing and region-specific builds are separate
projects. Takedawn's Xbox 1 save format and DSP cannot be imported directly.

## Stage 5 — compatibility and release

Keep the 57 fatal import stubs visible. Map actual newly reached paths against
ReXGlue/Xenia semantics, implement complete handlers with tests instead of success
stubs. Test Crash/replays, car selection, saves and several tracks. EU/other disc
revisions require independent generated code/manifests and verification. GOD,
network play and matchmaking remain outside the next stable-input release.

A new public release requires resolved/clearly scoped input behavior, complete
suites for both development games, downloaded-package first-run/profile/race
validation, updated source/licenses/checksums, and documentation after every run.

### Latest input checkpoint

Run 96 completed at 313.5 s, exit 0. User confirms the physical keyboard remains
stable in game and after returning; zero pad removals/fatal imports in the log.
This is a positive short check of the separate focus candidate. Installed/draft
cda797a still lacks that correction. Next gate: the actual packaged/downloaded
candidate, with controller/audio separately confirmed. Current name-entry UI is
retained unless the genuine Burnout APT screen can be verified; see the keyboard
research note. Full stages and handoff are in the active roadmap/prompt documents.

## Online, HUD, ultrawide and audio (reviewed 2026-10-18, after v0.1.12)

Same snapshots: Xerenge `22dad21` (v0.4.7), its SDK fork rexglue-xerenge `e029bb7`,
Takedawn `5a00fc1` (v0.2.0). Read through the GitHub API; nothing built or copied.

### Xerenge online (`--online`, GitHub issue #19 closed)

How it works, from its comments (rexglue/src/online_lobby.cpp, online_stub.cpp,
src/ealobby/, tools/ealobby/; ~4,000 lines plus the SDK half):

1. **SDK half** (rexglue-xerenge: xam_net.cpp, xam_user.cpp, xlivebase_app.cpp,
   include/rex/kernel/xam/online.h): the console answers as signed in to Live with
   a working connection; XOnlineGetServiceInfo(0x45410004) returns the lobby
   server's address; XNADDR carries the LAN address; sockets bind to it;
   XSESSION_INFO is filled for a host. That is what DirtySock needs to report
   'onln'.
2. **Title half**: ~80 hooks on the login state machine (CB4XenonLoginMenuState
   +0x140, states 0-11), CGtLobbyDirtySock status/login, LobbyApi steps, Revenge
   Rivals download (skipped), news/terms downloads (skipped: they fault without a
   server reply), connection-dropped callback, voice (VoipGetRef null guard),
   session creation ('sess' at 0x8240C908), kick/host checks (0x82366CF8,
   0x82366F78), peer redirection for 3+ players (ports 8192/6000 must reach the
   player, only 31859-31861 go to the lobby).
3. **Lobby server** (`ealobby`, C++17, three files): EA's Aries protocol on TCP
   31860/31861, LAN discovery on UDP 31859 (broadcast + multicast), every copy runs
   one unless `--lobby_server=<host>` names a shared one; it relays game traffic
   ("seamless multiplayer"). Burnout Clips sharing: EA Locker over HTTP on 31862
   (Top 20 / recent players), thumbnails still black.

Licence: Xerenge has no licence, so the hooks and the server are reference only
(ideas, addresses, protocol facts, re-implemented by us with attribution, as
game/movies.cpp did). ReXGlue SDK is BSD-3 (Xenia-derived); the fork's SDK files
can be adapted with the BSD notice kept, after checking each file's header.

Fit for us: our network layer is now an honest offline console (v0.1.12). Online
means: a Live-signed-in mode behind a setting, the SDK half (XOnline service info,
XNet addresses, XSession*, QoS answering for real peers, voice off), the login
hooks on the USA build (addresses must be re-verified: Xerenge supports USA and EU),
and our own Aries-compatible server (or interoperability with theirs, which would
let Mac and Linux/Windows players meet). Weeks of work; testable only with two
copies (two Macs, or two loopback addresses on one Mac as Xerenge does).

### Fukami (Ridge Racer V, PS2; GPL-3.0)

[danisandoval/fukami](https://github.com/danisandoval/fukami/tree/2ecb281), `2ecb281`
(2026-10-14, v1.1.0). Static recompilation of the PS2 game onto PCSX2's GS; macOS, Steam
Deck, Windows. GPL-3.0, so code is license-compatible with ours (keep its notices); its
PS2/PCSX2 paths and addresses don't apply. Reviewed 2026-10-19 from settings, docs and
selected sources; not built or run.

| Feature | Ours today | Decision (order set by the user; online after these) |
| --- | --- | --- |
| `car_lod`, `draw_distance`: LOD distances multiplied up to the game's cull distance; larger display lists, a car that doesn't fit drawn at stock detail that frame; cost measured | Stock LOD | 1st: find USA LOD/visibility code and buffer limits, measure |
| Pad slots (`rrv_pad_slots.h`): P1/P2 by connection, failover, a pad seen twice (BT + USB) never stuck as P2 | User 0 only | 2nd: slots for users 0–3, offline Multiplayer on two pads |
| Memory card writes: temp file, fsync, rename (power-loss safe) | Settings and install are atomic; game save path not checked | 3rd: check XContent/file writes, make them atomic if in place |
| `refresh_rate = auto`: fullscreen switches the display to a multiple of 60 Hz and back | One-time notice for 144/165 Hz externals | 4th: switch mode on Mac (`CGDisplaySetDisplayMode`), never ProMotion |
| `fast_unpack`: the game's unpacker run natively | Not measured | 5th: profile loads first; only if decompression dominates |
| Present pacing, cursor idle, real widescreen | Done (present_schedule, ui/cursor_idle.h, hud_anchor/ultrawide) | Nothing to add |
| AA1, mipmap emulation, VU AOT, split_gs/pacer_spin, D3D11/VRR, car_depth_fix | PS2/PCSX2/Deck/Windows specific | Not applicable |
| Session pad record and replay with fast-forward | Scripted pad input only | Maybe later as a debug tool; exact replay unlikely (multithreaded game) |

### Takedawn (Burnout 3: Takedown, original Xbox; MIT)

| Feature | Ours today | Decision |
| --- | --- | --- |
| Setup window that checks the disc file by file, no half-installed game, folder of choice | Done: install/ (manifest SHA-256 of 802 files, `.partial` then rename, placement/portable) | Nothing to add |
| Shader prebuild on first start | Measured not worth it (SHADER_PREP_BASELINE: hitches are loads) | Keep deferred |
| Menu/HUD upscaling 2x/4x (Super-xBR, MIT, by Hyllian; texture scaled on a worker, disk cache, swapped in later) | No; 360 2D art is 720p-class, still bilinear at render scale > 1 | Candidate: pick 2D textures by how they are drawn (no depth, single mip, <= 512, not a target), Super-xBR from its MIT source |
| 5.1 surround with per-speaker mixer | The game's own 5.1 mix goes to SDL as 6 channels (apu/audio.cpp); a 5.1 device gets it as is | Only the mixer/presets would be new; low priority |
| Ultrawide 21:9 / 32:9 (frames classified per flip: world frames wide, menus/films/loading 16:9 centred; 2D over the world kept at 16:9 size; hor+ camera) | Hor+ and Vert+ done (game/ultrawide.h); the whole HUD is squeezed into a centred 16:9 block | Improve the HUD: Takedawn's widescreen-HUD fix moves the HUD layout rectangle so each component keeps its shape but sits as far from the *visible* edge as in 16:9 (CB3HUD::GetAlignPos). Find Revenge's equivalent (CB4HUD align) and anchor per component; keep menus/films/pause at 16:9 |
