#include <user/localization.h>
// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See save_backups.h.
#include <stdafx.h>
#include "save_backups.h"
#include "ui.h"
#include "burnout_setup.h"
#include <user/paths.h>
#include <user/save_backup.h>
#include <user/save_import.h>

#include <imgui.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_misc.h>
#include <SDL3/SDL_timer.h>

namespace ui
{
    namespace
    {
        namespace sb = user::save_backup;
        namespace si = user::save_import;
        constexpr const char* kPopup = "save-backups";
        bool s_open = false;
        bool s_openPending = false;  // asked from a row; opened at the dialogs' level
        std::string s_confirm;  // the backup a restore is being asked about, or ""
        // The view changed: the cursor to its safe choice (Cancel, Close) from
        // this frame on; not in the frame of the press, whose view is the old one.
        int s_focusFrom = -1;
        void FocusSafeNext() { s_focusFrom = ImGui::GetFrameCount() + 1; }
        bool TakeFocusSafe()
        {
            if (s_focusFrom < 0 || ImGui::GetFrameCount() < s_focusFrom)
                return false;
            s_focusFrom = -1;
            return true;
        }

        std::filesystem::path Folder() { return GetUserPath() / "backups"; }

        // An import: the file or folder picked (from the dialog's callback,
        // which may come on another thread), then the saves found in it,
        // asked about before anything is recorded.
        std::mutex s_pickMutex;
        std::string s_picked;
        bool s_hasPick = false;
        std::filesystem::path s_importFrom;
        std::vector<si::Container> s_importing;

        void OnPick(void*, const char* const* files, int)
        {
            std::lock_guard lock(s_pickMutex);
            if (files && files[0])
            {
                s_picked = files[0];
                s_hasPick = true;
            }
            else if (!files)
                fprintf(stderr, "[saves] the file dialog failed: %s\n", SDL_GetError());
        }

        void PickImport(bool folder)
        {
            // BURNOUT_TEST_PICK_SAVE (automated runs): the path stands in for the dialog's pick.
            if (const char* test = std::getenv("BURNOUT_TEST_PICK_SAVE"); test && *test)
            {
                fprintf(stderr, "[saves] a test pick stands in for the file dialog: %s\n", test);
                const char* files[] = { test, nullptr };
                OnPick(nullptr, files, -1);
                return;
            }
            if (folder)
                SDL_ShowOpenFolderDialog(OnPick, nullptr, GetWindow(), std::getenv("HOME"), false);
            else
                SDL_ShowOpenFileDialog(OnPick, nullptr, GetWindow(), nullptr, 0, std::getenv("HOME"), false);
        }

        // A pick that came in: the saves in it, or a message why none.
        void TakePick()
        {
            std::string picked;
            {
                std::lock_guard lock(s_pickMutex);
                if (!s_hasPick)
                    return;
                s_hasPick = false;
                picked = std::move(s_picked);
            }
            std::string error;
            s_importing = si::Find(picked, error);
            s_importFrom = picked;
            fprintf(stderr, "[saves] import from %s: %s\n", picked.c_str(),
                s_importing.empty() ? error.c_str() : ("found " + si::Describe(s_importing)).c_str());
            if (s_importing.empty())
                Toast(error);
        }

        // The list and the pending restore, read from disk when the dialog
        // opens or changes something, and for the row every 2 s: not every frame.
        std::vector<sb::Backup> s_backups;
        std::string s_pending;
        uint64_t s_readAt = 0;
        void Refresh()
        {
            s_backups = sb::List(Folder());
            s_pending = sb::PendingRestore(Folder());
            s_readAt = SDL_GetTicks();
        }

        std::string Label(const sb::Backup& b)
        {
            std::string label = sb::DisplayName(b.name);
            if (b.beforeRestore)
                label += locale::Text("  (before a restore)");
            else if (b.imported)
                label += locale::Text("  (imported)");
            return label + "###" + b.name;
        }

        void QuitForRestore()
        {
            // The way out a window close takes: the next launch restores.
            fprintf(stderr, "[saves] restore recorded; quitting so the next launch can do it\n");
            SDL_Event e{};
            e.type = SDL_EVENT_QUIT;
            e.quit.timestamp = SDL_GetTicksNS();
            if (!SDL_PushEvent(&e))
                Toast("The restore happens at the next launch. Close the game's window now.");
        }
    }

    std::string SaveBackupsRowValue()
    {
        if (!s_readAt || SDL_GetTicks() - s_readAt > 2000)
            Refresh();
        if (!s_pending.empty())
            return "RESTORE PENDING";
        return s_backups.empty() ? std::string("NONE YET") : locale::Format("{} KEPT", s_backups.size());
    }

    const char* SaveBackupsHelp()
    {
        return "Each launch backs up your saves when they changed (the newest 10 are kept). Restore one here, or import "
               "saves from an Xbox 360 (a save package) or Xenia; your current saves are backed up first.";
    }

    void OpenSaveBackups()
    {
        s_openPending = true;
    }

    bool DrawSaveBackups()
    {
        if (s_openPending)
        {
            s_openPending = false;
            s_open = true;
            s_confirm.clear();
            s_importing.clear();
            s_focusFrom = ImGui::GetFrameCount();
            Refresh();
            ImGui::OpenPopup(kPopup);
        }
        if (!s_open || !ImGui::IsPopupOpen(kPopup))
        {
            s_open = false;
            return false;
        }
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        burnout::PushDialogStyle();
        const bool shown = ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);
        burnout::PopDialogStyle();
        if (!shown)
        {
            s_open = false;
            return false;
        }
        burnout::DialogFrame();
        const float s = Scale(), width = burnout::kDialogWidth * s;
        bool close = false;
        const std::string pending = s_pending;
        if (s_confirm.empty() && s_importing.empty())
        {
            TakePick();
            if (!s_importing.empty())
                FocusSafeNext();
        }
        if (!s_importing.empty())
        {
            burnout::DialogText(locale::Format("Import {} from {}? Saves of the same name are replaced; your current saves are "
                "backed up first. The game quits now; start it again to play the imported saves.", si::Describe(s_importing),
                s_importFrom.filename().string()).c_str(), width);
            ImGui::Dummy(ImVec2(0, 10 * s));
            if (burnout::DialogOption("Import and Quit", width))
            {
                std::string name, error;
                std::vector<si::Container> found;
                if (si::Request(s_importFrom, GetSavePath(true), Folder(), found, name, error))
                {
                    fprintf(stderr, "[saves] import of %s from %s recorded as %s\n", si::Describe(found).c_str(),
                        s_importFrom.string().c_str(), name.c_str());
                    Refresh();
                    close = true;
                    QuitForRestore();
                }
                else
                {
                    fprintf(stderr, "[saves] import from %s not recorded: %s\n", s_importFrom.string().c_str(), error.c_str());
                    Toast(error);
                }
                s_importing.clear();
            }
            if (TakeFocusSafe())
                ImGui::SetKeyboardFocusHere();
            if (burnout::DialogOption("Cancel", width) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                s_importing.clear();
                FocusSafeNext();
            }
        }
        else if (!s_confirm.empty())
        {
            // The question, before anything changes.
            burnout::DialogText(locale::Format("Restore the saves from {}? Your current saves are backed up first. The game "
                "quits now; start it again to play the restored saves.", sb::DisplayName(s_confirm)).c_str(), width);
            ImGui::Dummy(ImVec2(0, 10 * s));
            if (burnout::DialogOption("Restore and Quit", width))
            {
                std::string error;
                if (sb::RequestRestore(Folder(), s_confirm, error))
                {
                    Refresh();
                    close = true;
                    QuitForRestore();
                }
                else
                    Toast(error);
                s_confirm.clear();
            }
            if (TakeFocusSafe())
                ImGui::SetKeyboardFocusHere();
            if (burnout::DialogOption("Cancel", width) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                s_confirm.clear();
                FocusSafeNext();
            }
        }
        else
        {
            ImGui::PushFont(burnout::DisplayFont(), 34.0f);
            ImGui::TextUnformatted(locale::Text("SAVE BACKUPS"));
            ImGui::PopFont();
            const std::vector<sb::Backup> backups = s_backups;
            if (!pending.empty())
                burnout::DialogText(locale::Format("The saves from {} are restored at the next launch.", sb::DisplayName(pending)).c_str(), width);
            else if (backups.empty())
                burnout::DialogText("No backups yet: the next launch makes one once you have saved.", width);
            else
                burnout::DialogText("Choose a backup to restore.", width);
            ImGui::Dummy(ImVec2(0, 8 * s));
            if (!pending.empty() && burnout::DialogOption("Keep My Current Saves", width))
            {
                sb::CancelRestore(Folder());
                Refresh();
                fprintf(stderr, "[saves] the pending restore of %s was called off\n", pending.c_str());
            }
            for (const sb::Backup& b : backups)
                if (burnout::DialogOption(Label(b).c_str(), width))
                {
                    s_confirm = b.name;
                    FocusSafeNext();
                }
            if (burnout::DialogOption("Import Save File...", width))
                PickImport(false);
            if (burnout::DialogOption("Import Save Folder...", width))
                PickImport(true);
            if (burnout::DialogOption("Open Backups Folder", width))
            {
                std::error_code ec;
                std::filesystem::create_directories(Folder(), ec);
                SDL_OpenURL(("file://" + Folder().string()).c_str());
            }
            if (TakeFocusSafe())
                ImGui::SetKeyboardFocusHere();
            if (burnout::DialogOption("Close", width) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                close = true;
        }
        ImGui::Dummy(ImVec2(0, 6 * s));
        burnout::Hints({ { "Select", "A" }, { "Back", "B" } });
        if (close)
        {
            ImGui::CloseCurrentPopup();
            s_open = false;
        }
        burnout::EndDialog();
        return true;
    }
}
