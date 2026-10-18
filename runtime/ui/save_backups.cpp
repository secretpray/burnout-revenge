// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See save_backups.h.
#include <stdafx.h>
#include "save_backups.h"
#include "ui.h"
#include "burnout_setup.h"
#include <user/paths.h>
#include <user/save_backup.h>

#include <imgui.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_misc.h>
#include <SDL3/SDL_timer.h>

namespace ui
{
    namespace
    {
        namespace sb = user::save_backup;
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
                label += "  (before a restore)";
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
        return s_backups.empty() ? std::string("NONE YET") : std::format("{} KEPT", s_backups.size());
    }

    const char* SaveBackupsHelp()
    {
        return "Each launch backs up your saves when they changed (the newest 10 are kept). Restore one here; your "
               "current saves are backed up first.";
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
        if (!s_confirm.empty())
        {
            // The question, before anything changes.
            burnout::DialogText(std::format("Restore the saves from {}? Your current saves are backed up first. The game "
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
            ImGui::TextUnformatted("SAVE BACKUPS");
            ImGui::PopFont();
            const std::vector<sb::Backup> backups = s_backups;
            if (!pending.empty())
                burnout::DialogText(std::format("The saves from {} are restored at the next launch.", sb::DisplayName(pending)).c_str(), width);
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
