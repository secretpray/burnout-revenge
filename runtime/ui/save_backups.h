// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Settings > Advanced > Save Backups: the backups the launches made
// (user/save_backup.h), as the game's own prompt. Choosing one asks first;
// then the restore is recorded, the game quits, and the next launch puts
// those saves in place (the current ones backed up first). A pending
// restore can be called off. Main thread, inside the settings window.
#pragma once
#include <string>

namespace ui
{
    std::string SaveBackupsRowValue();  // "4 KEPT", "RESTORE PENDING", "NONE YET"
    const char* SaveBackupsHelp();
    void OpenSaveBackups();
    bool DrawSaveBackups();             // true while open (B is its)
}
