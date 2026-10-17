// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Switching an existing setup into portable mode (user/portable.h) and back,
// with a copy: burnout-revenge.sh --portable on|off (install_cli.cpp). Making or
// deleting portable.txt by hand switches too, with nothing copied (a fresh
// start, or this computer's data as it was).
//
//   on:  this computer's data folder (settings, saves, logs; not the game
//        folder or its record) is copied into BurnoutRevenge's data/, then the
//        installed game (the default one or the recorded one) into
//        data/game, verified (GameCopier: the installer's copy, every file
//        hashed against the manifest), then portable.txt is written.
//   off: data/ (not the game) is copied into this computer's data folder,
//        and the game into its game/ when it's in BurnoutRevenge's folder; a
//        game recorded outside it stays recorded where it is. A data folder
//        already there (usually the one from before "on") is renamed aside
//        to <name>.before-portable-off-<date>-<time>, then portable.txt is
//        removed.
// The command line refuses both on Apple platforms (portable::kSupported);
// the functions themselves work anywhere (tests run them on a Mac).
// Copies, never moves: the source is left as it was, and the messages say
// what can be deleted once the player is happy. Each copy is built in a
// tagged "<folder>.partial" beside its destination and renamed into place at
// the end, with the marker changed last, so an interruption leaves the
// previous mode working; the next attempt replaces its own unfinished copy
// (one with the tag), and refuses anything else of that name.
#pragma once
#include "result.h"

#include <user/portable.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace install
{
    struct Manifest;
    const Manifest& GameManifest();

    // Copies the installed game in `from` into `to` (which doesn't exist),
    // verified. The command line's: OpenDiscSource + Install, with progress.
    using GameCopier = std::function<Result(const std::filesystem::path& from, const std::filesystem::path& to)>;

    // The tag file in an unfinished copy (see above).
    inline constexpr const char* kPortablePartialTag = ".burnout-revenge-portable-partial";

    // --portable on. `root`: BurnoutRevenge's folder (absolute; portable.txt
    // not in it yet); `home`: this computer's data folder (HomeUserPath()).
    // `said` gets what was done, a line each, for the terminal.
    Result MakePortable(const std::filesystem::path& root, const std::filesystem::path& home, const GameCopier& copyGame,
        std::vector<std::string>& said, const Manifest& manifest = GameManifest());

    // --portable off. `layout`: portable mode's (active); `home` as above.
    Result LeavePortable(const portable::Layout& layout, const std::filesystem::path& home, const GameCopier& copyGame,
        std::vector<std::string>& said, const Manifest& manifest = GameManifest());
}
