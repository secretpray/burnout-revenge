#include <user/localization.h>
// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include <stdafx.h>
#include "guest_keyboard.h"
#include "ui.h"
#include "burnout_setup.h"
#include <imgui.h>
#include <kernel/xam.h>
#include <SDL3/SDL.h>

namespace ui
{
    namespace
    {
        struct Request
        {
            std::string title, description;
            std::vector<char> text;
            std::function<void(bool, std::string)> complete;
            size_t maxUnits = 0;
            bool firstFrame = true, showing = false;
            bool upper = true, replaceInitial = true;
        };
        std::mutex mutex;
        std::shared_ptr<Request> pending;

        void Finish(const std::shared_ptr<Request>& request, bool accept)
        {
            SetModal(nullptr);
            XamNotifySystemUi(false);
            {
                std::lock_guard lock(mutex);
                pending.reset();
            }
            request->complete(accept, request->text.data());
        }

        void DrawKeyboard(const std::shared_ptr<Request>& request)
        {
            const auto* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(.5f, .5f));
            float s = Scale();
            ImGui::SetNextWindowSize(ImVec2(std::min(860.0f * s, viewport->WorkSize.x - 32 * s), 0));
            if (request->firstFrame)
                ImGui::SetNextWindowFocus();
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {28 * s, 24 * s});
            ImGui::PushStyleColor(ImGuiCol_WindowBg, {.11f, .098f, .086f, .99f});
            ImGui::PushStyleColor(ImGuiCol_Text, {.95f, .92f, .86f, 1});
            ImGui::Begin("Player name", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::PushFont(nullptr, 18);
            // The game asks for a profile's name and a Burnout Clip's: its
            // own title says which, so nothing here names one.
            burnout::Eyebrow("BURNOUT REVENGE");
            burnout::Heading(request->title.empty() ? "PLAYER NAME" : request->title.c_str(), 32);
            ImGui::TextWrapped("%s", request->description.c_str());
            bool pad = SDL_HasGamepad();
            if (request->firstFrame && !pad)
                ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-1);
            bool accept = ImGui::InputText("##name", request->text.data(), request->text.size(),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::IsItemEdited()) request->replaceInitial = false;
            size_t units = 0;
            for (const unsigned char* p = reinterpret_cast<const unsigned char*>(request->text.data()); *p; ++p)
                if ((*p & 0xC0) != 0x80) units += *p >= 0xF0 ? 2 : 1;
            bool valid = units > 0 && units <= request->maxUnits;
            ImGui::TextDisabled(locale::Text("%zu / %zu characters"), units, request->maxUnits);
            if (units > request->maxUnits) ImGui::TextUnformatted(locale::Text("Name is too long."));
            auto append = [&](char c) {
                if (request->replaceInitial) { request->text[0] = 0; request->replaceInitial = false; units = 0; }
                size_t bytes = std::strlen(request->text.data());
                if (units < request->maxUnits && bytes + 1 < request->text.size())
                { request->text[bytes] = c; request->text[bytes + 1] = 0; ++units; }
            };
            auto erase = [&] {
                size_t bytes = std::strlen(request->text.data());
                if (!bytes) return;
                do { --bytes; } while (bytes && (static_cast<unsigned char>(request->text[bytes]) & 0xc0) == 0x80);
                request->text[bytes] = 0;
                request->replaceInitial = false;
            };
            ImGui::Spacing();
            // Real buttons: mouse, D-pad and normal keyboard navigation all
            // use the same focus system. A physical keyboard still edits
            // the text field directly, including UTF-8 input.
            const char* rows[] = { "ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "1234567890-_" };
            float gap = ImGui::GetStyle().ItemSpacing.x;
            float cell = (ImGui::GetContentRegionAvail().x - gap * 12) / 13;
            for (int row = 0; row < 3; ++row)
            {
                ImGui::PushID(row);
                for (int col = 0; rows[row][col]; ++col)
                {
                    if (col) ImGui::SameLine();
                    char c = rows[row][col];
                    if (!request->upper && c >= 'A' && c <= 'Z') c += 'a' - 'A';
                    char label[] = {c, 0};
                    if (burnout::Button(label, {cell, 42 * s})) append(c);
                    if (request->firstFrame && pad && row == 0 && col == 0)
                    { ImGui::SetItemDefaultFocus(); ImGui::SetKeyboardFocusHere(-1); }
                }
                ImGui::PopID();
            }
            float third = (ImGui::GetContentRegionAvail().x - gap * 2) / 3;
            if (burnout::Button(request->upper ? "abc" : "ABC", {third, 40 * s}) || ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) request->upper = !request->upper;
            ImGui::SameLine();
            if (burnout::Button("SPACE", {third, 40 * s})) append(' ');
            ImGui::SameLine();
            if (burnout::Button("DELETE", {third, 40 * s}) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, true)) erase();
            ImGui::Spacing();
            valid = request->text[0] && units <= request->maxUnits;
            float half = (ImGui::GetContentRegionAvail().x - gap) * .5f;
            ImGui::BeginDisabled(!valid);
            accept |= burnout::Button("DONE", {half, 52 * s}, true) || ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false);
            ImGui::EndDisabled();
            accept &= valid;
            ImGui::SameLine();
            bool cancel = burnout::Button("CANCEL", {half, 52 * s}) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
            ImGui::TextDisabled("%s", locale::Text("D-pad: choose   A: type   X: delete   Start: done   B: cancel"));
            ImGui::TextDisabled("%s", locale::Text("Or type with your keyboard. Enter: done. Esc: cancel."));
            request->firstFrame = false;
            ImGui::PopFont();
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            if (cancel || accept)
                Finish(request, !cancel);
        }
    }

    bool RequestGuestKeyboard(std::string title, std::string description, std::string initial,
        size_t maxUnits, std::function<void(bool, std::string)> complete)
    {
        std::lock_guard lock(mutex);
        if (pending)
            return false;
        auto request = std::make_shared<Request>();
        request->title = std::move(title);
        request->description = std::move(description);
        request->text.resize(maxUnits * 4 + 1);
        request->maxUnits = maxUnits - 1;
        initial.copy(request->text.data(), request->text.size() - 1);
        request->complete = std::move(complete);
        pending = std::move(request);
        return true;
    }

    void TickGuestKeyboard()
    {
        std::shared_ptr<Request> request;
        {
            std::lock_guard lock(mutex);
            request = pending;
        }
        if (request && !request->showing && Ready() && !HasModal())
        {
            request->showing = true;
            XamNotifySystemUi(true);
            SetModal([request] { DrawKeyboard(request); });
        }
    }

    void CancelGuestKeyboard()
    {
        std::shared_ptr<Request> request;
        {
            std::lock_guard lock(mutex);
            request = pending;
        }
        if (request)
            Finish(request, false);
    }
}
