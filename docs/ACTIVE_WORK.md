# Active work only

Updated 2026-10-18. This is the compact working context. Historical evidence lives
in PROGRESS.md; completed stages are not carried as active tasks.

## Release blocker

- [ ] Magic Keyboard fails system-wide after returning to Burnout over Bluetooth
  (run 93, user-confirmed power cycle). App logs with zero pad removals do not prove
  keyboard health. USB short comparison passed; root cause remains unknown.
- [ ] v0.1.1 remains a draft. Applications contains the downloaded cda797a package;
  new focus changes are a development candidate, not that installed package.
- [ ] Validate both directions of physical Cmd+Tab/typing, hide/restore and exit;
  record keyboard and gamepad separately. No OS UI automation.

## Current tranche

- [x] Read the three requested projects and actual underlying input layers.
- [x] Compare official SDL 3.4.16 -> 3.4.18 changes; no identified keyboard fix.
- [x] Implement background-output gate + Command shortcut isolation locally.
- [x] Builds and full suites: standalone 61/61, shared Burnout 63/63, NFSMW 62/62.
- [x] Virtual-only focus regression (run 95), 24.1 s, exit 0, no fatal imports.
- [ ] Physical comparison (run 96 in progress); not a confirmed keyboard fix.
- [ ] Publish source/docs checkpoint without publishing the failed release.

## Queued work, in order

1. Optional shader/pipeline preparation from the user's disc, coverage manifest,
   cancellation/resume and measured cold/prepared comparison.
2. SMAA/CAS evaluation; bloom downsample/LOD audit, same-scene GPU comparison.
3. Safe MetalFX history consumer and compare draw-state interpolation; 30->60
   Crash first, then 60->120. Live FG remains disabled.
4. Save backup/restore, controller prompts, custom music and add-on coverage.
5. Fatal-import audit (57 remain), broader career/Crash testing; later regions/GOD.

See [source evidence, integration decisions and acceptance gates](EXTERNAL_PORTS_PLAN.md)
and [release validation](RELEASE_VALIDATION_2026-10-18.md). No proprietary assets,
saves or raw device identifiers belong in the public repository.

Isolated fallback: [SDL keyboard/controller matrix](INPUT_DIAGNOSTICS.md).
The probe is built; physical probe runs are pending.
