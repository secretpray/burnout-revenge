// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See ui.h.
//
// The one-time "What's new" note: the first launch of a newer version shows
// this version's CHANGELOG.md entry, compiled in at build time
// (generated/whats_new.inc, runtime/CMakeLists.txt), over the game's first
// frames until the player closes it. Offline: the note is built in and
// nothing is sent (Check for Updates, update/updater.h, goes online only when
// pressed). main.cpp decides when (user/version_check.h); the version that
// ran last lives in settings.toml, so it shows once.
#include <stdafx.h>
#include "ui.h"
#include "burnout_setup.h"

#include <user/version_check.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <cctype>
#include <cfloat>

namespace ui
{
    namespace
    {
#if __has_include("whats_new.inc")
#include "whats_new.inc"
#else
        constexpr const char kWhatsNewEntry[] = "";
#endif

        struct Note
        {
            std::string title;
            std::vector<version::NoteLine> lines;
            bool firstFrame = true;
        };

        void Close(const char* how)
        {
            fprintf(stderr, "[ui] What's new closed (%s)\n", how);
            SetModal(nullptr);
        }

        void Draw(Note& note)
        {
            // The game's prompt (burnout::PushDialogStyle): its heading face,
            // the entry in its description face, OK on a yellow band.
            float s = Scale();
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            float width = std::min(viewport->WorkSize.x - 2 * kSpaceL * s, 760.0f * s);
            float maxHeight = std::min(viewport->WorkSize.y - 2 * kSpaceL * s, 640.0f * s);
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, maxHeight));
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_AlwaysAutoResize;
            if (note.firstFrame || !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetNextWindowFocus();
            burnout::PushDialogStyle();
            const bool shown = ImGui::Begin("What's new", nullptr, flags);
            burnout::PopDialogStyle();
            if (!shown)
            {
                ImGui::End();
                return;
            }
            burnout::DialogFrame();
            {
                std::string heading = note.title;
                for (char& c : heading)
                    c = char(std::toupper(static_cast<unsigned char>(c)));
                ImGui::PushFont(burnout::DisplayFont(), 40.0f);
                ImGui::TextUnformatted(heading.c_str());
                ImGui::PopFont();
                ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.13f, 1), "[WHAT'S NEW]");
            }
            ImGui::Dummy(ImVec2(0, 6 * s));

            // The entry, scrolled by the D-pad, the stick or the arrow keys
            // when it doesn't fit (the button below keeps the focus).
            float footer = 44 * s + ImGui::GetFrameHeightWithSpacing() * 2;
            float room = maxHeight - ImGui::GetCursorPosY() - footer - ImGui::GetStyle().WindowPadding.y;
            float lineHeight = ImGui::GetTextLineHeightWithSpacing();
            ImGui::SetNextWindowSizeConstraints(ImVec2(0, lineHeight), ImVec2(FLT_MAX, std::max(room, lineHeight * 3)));
            if (ImGui::BeginChild("entry", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY))
            {
                ImGui::PushTextWrapPos(0.0f);
                for (const version::NoteLine& line : note.lines)
                    switch (line.kind)
                    {
                    case version::NoteLine::Kind::Gap:
                        ImGui::Spacing();
                        break;
                    case version::NoteLine::Kind::Heading:
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.6f, 0.13f, 1));
                        ImGui::TextWrapped("%s", line.text.c_str());
                        ImGui::PopStyleColor();
                        break;
                    case version::NoteLine::Kind::Bullet:
                    {
                        // The game's orange marker, the text beside it.
                        const ImVec2 at = ImGui::GetCursorScreenPos();
                        const float h = ImGui::GetFontSize();
                        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x + 2 * s, at.y + h * 0.38f),
                            ImVec2(at.x + 2 * s + h * 0.28f, at.y + h * 0.66f), IM_COL32(242, 154, 34, 255));
                        ImGui::Indent(h * 0.9f);
                        ImGui::TextWrapped("%s", line.text.c_str());
                        ImGui::Unindent(h * 0.9f);
                        break;
                    }
                    case version::NoteLine::Kind::Text:
                        ImGui::TextWrapped("%s", line.text.c_str());
                        break;
                    }
                ImGui::PopTextWrapPos();
                float step = lineHeight * 0.5f;
                if (ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown) || ImGui::IsKeyDown(ImGuiKey_GamepadLStickDown) || ImGui::IsKeyDown(ImGuiKey_DownArrow))
                    ImGui::SetScrollY(ImGui::GetScrollY() + step);
                if (ImGui::IsKeyDown(ImGuiKey_GamepadDpadUp) || ImGui::IsKeyDown(ImGuiKey_GamepadLStickUp) || ImGui::IsKeyDown(ImGuiKey_UpArrow))
                    ImGui::SetScrollY(ImGui::GetScrollY() - step);
            }
            ImGui::EndChild();

            ImGui::Dummy(ImVec2(0, 8 * s));
            if (note.firstFrame)
                ImGui::SetKeyboardFocusHere();
            bool close = burnout::DialogOption("OK", ImGui::GetContentRegionAvail().x);
            if (close)
            {
                // Which input pressed it: a stray press closes the note unseen.
                static const char* const kSources[] = { "none", "mouse", "keyboard", "gamepad" };
                const int source = int(ImGui::GetCurrentContext()->ActiveIdSource);
                fprintf(stderr, "[ui] What's new: OK pressed (%s)\n", source >= 0 && source < 4 ? kSources[source] : "other");
            }
            ImGui::Dummy(ImVec2(0, 4 * s));
            burnout::Hints({ { "OK", "A" }, { "Close", "B" } });
            note.firstFrame = false;
            const char* how = close ? "OK" : nullptr;
            if (!how && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                how = "B or Esc";
            else if (!how && ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false))
                how = "Start";
            burnout::EndDialog(false);
            if (how)
                Close(how);
        }
    }

    std::string_view WhatsNewEntry()
    {
        return kWhatsNewEntry;
    }

    void ShowWhatsNew(std::string_view version)
    {
        if (!Ready())
        {
            fprintf(stderr, "[ui] What's new: no UI to show it in\n");
            return;
        }
        auto note = std::make_shared<Note>();
        note->title = "Updated to v" + std::string(version);
        note->lines = version::ParseNote(WhatsNewEntry());
        if (note->lines.empty())
            return;
        fprintf(stderr, "[ui] What's new: v%.*s (%zu lines)\n", int(version.size()), version.data(), note->lines.size());
        SetModal([note] { Draw(*note); });
    }
}
