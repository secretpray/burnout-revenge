// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The installer on the command line, for scripts, SSH sessions and machines
// without the first-run UI:
//   BurnoutRevenge --install <image|folder> [--dest <dir>]   copy and verify (default dest: GetUserPath()/game)
//   BurnoutRevenge --verify <image|folder>                   hash-check every file without copying
//   BurnoutRevenge --find-images [folder...]                 list disc images in the usual places (or these)
//   BurnoutRevenge --where                                   print where the game install was found, and the data folders
//   BurnoutRevenge --portable on|off                         copy the data and the game into BurnoutRevenge's folder and keep
//                                                          them there (portable mode), or back (install/portable_switch.h)
// Ctrl-C cancels an install cleanly (the partial copy is removed).
#pragma once
#include <optional>

namespace install
{
    // If argv holds one of the commands above, runs it and returns the exit
    // code for main to return (0 success, 1 failure, 2 usage, 130 cancelled);
    // otherwise returns nullopt and the game starts as usual.
    std::optional<int> RunInstallCommand(int argc, char** argv);
}
