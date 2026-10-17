// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See ui.h.
//
// The page shown instead of the game when it can't start: today, when the
// guest's 4 GB of address space couldn't be reserved (kernel/memory_status.h:
// on iOS, an app signed without Extended Virtual Addressing). Before the
// game, like the installer: nothing of the game runs behind it. On iOS it
// stays up (an app doesn't quit itself; the player swipes it away); elsewhere
// OK, A, B, Start or Esc closes it, and the window's close button too.
#include <stdafx.h>
#include "ui.h"
#include "quit_game.h"

#include <imgui.h>

namespace ui
{
    namespace
    {
        constexpr bool kCanQuit = quit::kOffered;  // not on iPhone or iPad

        struct ErrorPage
        {
            std::string title;
            std::vector<std::string> paragraphs;
            std::string details;
            bool firstFrame = true;
            bool quit = false;
        };

        void Draw(ErrorPage& page)
        {
            float s = Scale();
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            float width = std::min(viewport->WorkSize.x - 2 * kSpaceL * s, 760.0f * s);
            float maxHeight = viewport->WorkSize.y - 2 * kSpaceL * s;
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, maxHeight));
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
            if (page.firstFrame || !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetNextWindowFocus();
            if (!ImGui::Begin("Can't start", nullptr, flags))
            {
                ImGui::End();
                return;
            }
            TitleText(page.title.c_str());
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            for (size_t i = 0; i < page.paragraphs.size(); i++)
            {
                if (i > 0)
                    ImGui::Dummy(ImVec2(0, kSpaceS * s));
                ImGui::TextWrapped("%s", page.paragraphs[i].c_str());
            }
            if (!page.details.empty())
            {
                ImGui::Dummy(ImVec2(0, kSpaceM * s));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", page.details.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::PopTextWrapPos();
            if (kCanQuit)
            {
                ImGui::Dummy(ImVec2(0, kSpaceS * s));
                if (page.firstFrame)
                    ImGui::SetKeyboardFocusHere();
                bool quit = ImGui::Button("Quit");
                if (quit || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)
                    || ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false))
                    page.quit = true;
            }
            page.firstFrame = false;
            ImGui::End();
        }
    }

    void RunStartupError(const std::string& title, const std::vector<std::string>& paragraphs, const std::string& details,
        const std::function<bool()>& runFrame)
    {
        if (!Ready())
            return;
        ErrorPage page{ title, paragraphs, details };
        SetModal([&page] { Draw(page); });
        fprintf(stderr, "[ui] showing \"%s\" (%s)\n", title.c_str(),
            kCanQuit ? "until it is closed" : "until the app is closed");
        while (!page.quit && runFrame())
            ;
        SetModal(nullptr);
        fprintf(stderr, "[ui] \"%s\" closed\n", title.c_str());
    }
}
