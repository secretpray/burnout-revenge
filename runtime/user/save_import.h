// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Importing Burnout Revenge saves made elsewhere (Settings > Saves > Save
// Backups, or --import-save <path> with the game closed). The idea is
// Takedawn's (its "Import Xbox save" for Burnout 3, MIT); this code is ours.
// What can be imported:
//   - a console's save package (STFS, CON), as a USB stick's Content folder
//     or a save tool exports it: user/stfs.h;
//   - a folder holding such packages or saves already unpacked: a copy of a
//     console drive's Content folder, Xenia's content folder
//     (<title>/00000001/<save>/<file>), our own save folder or a backup;
//   - the progress file itself ("Player Files").
// Only the game's saves (title 454107DC, content type 1) are taken. They go
// in by a restore (user/save_backup.h): the current saves with the imported
// ones added, or put in place of those of the same name, become a backup,
// restored at the next launch, before the game runs, the current saves
// backed up first. Filesystem only, so tests/save_import_test.cpp runs it.
#pragma once
#include "stfs.h"

#include <filesystem>
#include <string>
#include <vector>

namespace user::save_import
{
    inline constexpr uint32_t kTitleId = 0x454107DC;

    // A save container: save/<name>/ with its files, <name>.xcontent and
    // <name>.thumbnail.png beside it (kernel/xam.cpp).
    struct Container
    {
        std::string name;
        std::u16string displayName;
        std::vector<uint8_t> thumbnail;
        std::vector<stfs::File> files;
        std::filesystem::path from;
    };

    // The saves in `source` (a file or a folder). Empty, with `error`, when
    // there are none.
    std::vector<Container> Find(const std::filesystem::path& source, std::string& error);

    // "Player Files, Save And Share 0".
    std::string Describe(const std::vector<Container>& containers);

    // `containers` into `folder` (a save folder's copy), replacing any of the
    // same names.
    bool Write(const std::filesystem::path& folder, const std::vector<Container>& containers, std::string& error);

    // Find, then the import recorded for the next launch (the restore's
    // backup is `backupName`). False, with `error`, when nothing was recorded.
    bool Request(const std::filesystem::path& source, const std::filesystem::path& saveDir, const std::filesystem::path& backupsDir,
        std::vector<Container>& found, std::string& backupName, std::string& error);
}
