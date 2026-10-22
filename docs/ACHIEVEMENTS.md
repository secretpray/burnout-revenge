# Xbox 360 achievements

Open **Settings > Achievements** (F1, Cmd+, or Back + Start). The list contains
Burnout Revenge's 36 original achievements, worth 1000 G. Use the D-pad, arrow
keys or mouse to select one; its requirements appear below the list. Locked
icons are grey. Earned icons are in color, with the date and time recorded.

The game awards achievements under its original conditions. An unlocked
achievement appears over play for six seconds, without taking input or pausing
the game. Several awards take turns. Opening the menu or restarting the game
does not show old awards again.

Progress is local to this installation's console profile. Offline and online
play use the same record, `achievements.txt` in the data folder, separate from
the game's Player Files. Starting a new World Tour does not erase achievements.
Save backups and imports currently cover Player Files, not this console record.
There is no Xbox Live, Steam or RetroAchievements account synchronization.
Importing a career save does not automatically grant its achievements: the game
must send its normal award requests. It may re-evaluate a loaded career and
award its completed milestones itself. Online achievements also require those
requests; joining a lobby alone is not treated as earning them by the port.

Names, conditions, scores and PNG icons are read from the XDBF/SPA resource in
the player's own XEX at launch. They are not included in this repository. The
screen currently uses English, matching the rest of the port's menus.

## Implementation references

The implementation is written for this runtime. The console contracts were
checked against these projects:

- [Xenia Canary's XGI dispatcher](https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/kernel/xam/apps/xgi_app.cc):
  app `0xFB`, message `0x000B0008`, a count and pointer to `{user, id}` pairs.
- [Xenia's XDBF reader](https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/util/xdbf_utils.h):
  SPA entry tables, English strings and achievement metadata.
- [Xenia Canary's achievement flags](https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/kernel/xam/xdbf/gpd_info.h)
  and [enumerator](https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/kernel/xenumerator.h):
  36-byte details, UTF-16BE strings, unlock flag and FILETIME.
- [Unleashed Recompiled's manager](https://github.com/hedge-dev/UnleashedRecomp/blob/main/UnleashedRecomp/user/achievement_manager.cpp):
  persistent IDs and timestamps, duplicate suppression and an overlay for new awards.

The local store uses the port's existing flush-and-rename save writer. A failed
write returns an error to the game and does not display a successful unlock.
A malformed record is kept intact and reported in the menu. Guest enumeration
returns a snapshot, including stored flags and times, so the game can see what
has already been earned. The profile's Gamerscore and achievement count reflect
that same record. Notification and render work stay on the UI thread.

## Tests

From a configured build directory, run:

```sh
ctest -R '^(achievements_test|kernel_compat_test)$' --output-on-failure
```

The tests use synthetic metadata and scratch profiles. They check catalogue
bounds, concurrent duplicate requests, persistence, corruption, failed writes,
guest enumeration and synchronous/asynchronous XGI calls without game files.
