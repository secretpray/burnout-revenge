#include <user/localization.h>
// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See ui.h.
//
// A one-time notice over the game (the refresh-rate one, video/
// refresh_notice.h, from SpeedBreaker v0.1.7): a heading, a paragraph and
// OK, as the game's own prompts (burnout::PushDialogStyle), like the "What's
// new" note. The game pauses while it shows, as for the settings menu (the
// 360's Guide), and goes on when it closes.
#include <stdafx.h>
#include "ui.h"
#include "burnout_setup.h"

#include <kernel/xam.h>

#include <imgui.h>

#include <cctype>
#include <memory>

namespace ui
{
    namespace
    {
        struct Notice
        {
            std::string title, text;
            std::function<void(const char*)> closed;
            bool firstFrame = true;
        };

        void Close(Notice& notice, const char* how)
        {
            fprintf(stderr, "[ui] notice \"%s\" closed (%s)\n", notice.title.c_str(), how);
            auto closed = std::move(notice.closed);
            SetModal(nullptr);  // destroys `notice`
            XamNotifySystemUi(false);
            if (closed)
                closed(how);
        }

        void Draw(Notice& notice)
        {
            const float s = Scale(), width = burnout::kDialogWidth * s;
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_AlwaysAutoResize;
            if (notice.firstFrame || !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetNextWindowFocus();
            burnout::PushDialogStyle();
            const bool shown = ImGui::Begin("Notice", nullptr, flags);
            burnout::PopDialogStyle();
            if (!shown)
            {
                ImGui::End();
                return;
            }
            burnout::DialogFrame();
            {
                std::string heading = locale::Text(notice.title.c_str());
                for (char& c : heading)
                    c = char(std::toupper(static_cast<unsigned char>(c)));
                ImGui::PushFont(burnout::DisplayFont(), 34.0f);
                ImGui::TextUnformatted(heading.c_str());
                ImGui::PopFont();
            }
            burnout::DialogText(locale::Text(notice.text.c_str()), width);
            ImGui::Dummy(ImVec2(0, 10 * s));
            if (notice.firstFrame)
                ImGui::SetKeyboardFocusHere();
            const bool ok = burnout::DialogOption("OK", width);
            ImGui::Dummy(ImVec2(0, 4 * s));
            burnout::Hints({ { "OK", "A" }, { "Close", "B" } });
            notice.firstFrame = false;
            const char* how = ok ? "OK" : nullptr;
            if (!how && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                how = "B or Esc";
            else if (!how && ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false))
                how = "Start";
            burnout::EndDialog(false);
            if (how)
                Close(notice, how);
        }
    }

    bool ShowNotice(std::string title, std::string text, std::function<void(const char*)> closed)
    {
        if (!Ready() || HasModal() || SettingsOpen())
            return false;
        auto notice = std::make_shared<Notice>();
        notice->title = std::move(title);
        notice->text = std::move(text);
        notice->closed = std::move(closed);
        fprintf(stderr, "[ui] notice \"%s\": %s\n", notice->title.c_str(), notice->text.c_str());
        SetModal([notice] { Draw(*notice); });
        // As the settings menu: the game pauses under it.
        XamNotifySystemUi(true);
        return true;
    }
}
