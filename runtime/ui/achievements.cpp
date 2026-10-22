#include <user/localization.h>
// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#include <cfloat>
#include "achievements.h"
#include "ui.h"
#include "burnout_setup.h"
#include <user/achievements.h>
#include <SDL3/SDL.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <format>
#include <map>
#include <memory>

namespace ui
{
    namespace
    {
        struct Art { std::unique_ptr<ImTextureData> color, grey; };
        std::map<uint32_t, Art> s_art;
        uint32_t s_user = 0, s_selected = 0;
        std::optional<achievements::Notice> s_notice;
        std::chrono::steady_clock::time_point s_noticeSince;
        const achievements::Definition* Definition(uint32_t id)
        {
            const auto& all = achievements::Current().Definitions();
            auto it = std::find_if(all.begin(), all.end(), [&](auto& d) { return d.id == id; });
            return it == all.end() ? nullptr : &*it;
        }
        void Icon(const achievements::Definition& d, bool unlocked, ImVec2 at, float size)
        {
            auto [it, fresh] = s_art.try_emplace(d.id);
            if (fresh && !d.image.empty())
            {
                SDL_Surface* image = SDL_LoadPNG_IO(SDL_IOFromConstMem(d.image.data(), d.image.size()), true);
                SDL_Surface* rgba = image ? SDL_ConvertSurface(image, SDL_PIXELFORMAT_RGBA32) : nullptr;
                SDL_DestroySurface(image);
                if (rgba && rgba->w > 0 && rgba->w <= 512 && rgba->h > 0 && rgba->h <= 512)
                {
                    auto make = [&](bool grey) {
                        auto tex = std::make_unique<ImTextureData>();
                        tex->Create(ImTextureFormat_RGBA32, rgba->w, rgba->h); tex->UseColors = true;
                        for (int y = 0; y < rgba->h; ++y)
                        {
                            auto* in = static_cast<uint8_t*>(rgba->pixels) + y * rgba->pitch;
                            auto* out = static_cast<uint8_t*>(tex->GetPixelsAt(0, y));
                            for (int x = 0; x < rgba->w; ++x)
                            {
                                uint8_t value = uint8_t((54u * in[4*x] + 183u * in[4*x+1] + 19u * in[4*x+2]) >> 8);
                                for (int c = 0; c < 3; ++c) out[4*x+c] = grey ? uint8_t(value * 0.65f) : in[4*x+c];
                                out[4*x+3] = in[4*x+3];
                            }
                        }
                        ImGui::RegisterUserTexture(tex.get()); return tex;
                    };
                    it->second.color = make(false); it->second.grey = make(true);
                }
                SDL_DestroySurface(rgba);
            }
            auto& texture = unlocked ? it->second.color : it->second.grey;
            auto* draw = ImGui::GetWindowDrawList();
            if (texture) draw->AddImage(texture->GetTexRef(), at, {at.x + size, at.y + size});
            else
            {
                draw->AddRectFilled(at, {at.x + size, at.y + size}, IM_COL32(65, 64, 60, 255));
                draw->AddText({at.x + size * 0.35f, at.y + size * 0.22f}, IM_COL32(190, 187, 179, 255), "G");
            }
        }
        std::string When(int64_t timestamp)
        {
            if (!timestamp) return locale::Text("Locked");
            std::time_t t = std::time_t(timestamp); std::tm local{}; localtime_r(&t, &local);
            char date[32]; std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M", &local);
            return std::string(locale::Text("Unlocked ")) + date;
        }
    }

    void DrawAchievementSummary()
    {
        const auto& definitions = achievements::Current().Definitions();
        auto entries = achievements::Current().Entries(s_user);
        uint32_t earned = 0, score = 0, total = 0;
        for (size_t i = 0; i < definitions.size(); ++i)
        {
            total += definitions[i].score;
            if (i < entries.size() && entries[i].unlocked) { ++earned; score += definitions[i].score; }
        }
        ImGui::PushFont(burnout::LabelFont(), 22);
        ImGui::Text(locale::Text("%u / %zu unlocked     %u / %u G"), earned, definitions.size(), score, total);
        ImGui::PopFont();
    }
    void DrawAchievements(bool focusFirst)
    {
        const auto& definitions = achievements::Current().Definitions();
        auto entries = achievements::Current().Entries(s_user);
        if (definitions.empty()) { ImGui::TextWrapped("%s", locale::Text("Achievements are available after the game has loaded.")); return; }
        float s = Scale(), icon = 48 * s, row = 62 * s;
        for (size_t i = 0; i < definitions.size(); ++i)
        {
            const auto& d = definitions[i]; bool unlocked = i < entries.size() && entries[i].unlocked;
            ImGui::PushID(int(d.id));
            if (focusFirst && i == 0) ImGui::SetKeyboardFocusHere();
            ImVec2 at = ImGui::GetCursorScreenPos(); float width = ImGui::GetContentRegionAvail().x;
            bool clicked = ImGui::Selectable("##achievement", s_selected == d.id, 0, {0, row});
            // A stationary pointer must not fight the controller's focus.
            // ImGui hides its nav cursor on mouse movement and restores it
            // when keyboard/controller navigation resumes.
            const bool nav = ImGui::GetCurrentContext()->NavCursorVisible;
            const bool focused = nav && ImGui::IsItemFocused();
            if (clicked || focused || (!nav && ImGui::IsItemHovered()) || !s_selected) s_selected = d.id;
            if (focused) ImGui::SetScrollHereY();
            Icon(d, unlocked, {at.x + 8*s, at.y + 6*s}, icon);
            auto* draw = ImGui::GetWindowDrawList();
            float x = at.x + icon + 24*s;
            std::string points = locale::Format("{} G", d.score);
            float pointsWidth = ImGui::CalcTextSize(points.c_str()).x;
            if (s_selected == d.id)
                burnout::SelectorLines(draw, at, {at.x + width, at.y + row});
            draw->PushClipRect({x, at.y}, {at.x + width - pointsWidth - 24*s, at.y + row}, true);
            draw->AddText({x, at.y + 5*s}, s_selected == d.id ? IM_COL32(245,182,38,255) : unlocked ? IM_COL32(243,235,218,255) : IM_COL32(177,174,166,255), d.name.c_str());
            ImGui::PushFont(burnout::LabelFont(), 17);
            draw->AddText({x, at.y + 33*s}, IM_COL32(177,174,166,255), locale::Text(unlocked ? "Unlocked" : "Locked"));
            ImGui::PopFont(); draw->PopClipRect();
            draw->AddText({at.x + width - pointsWidth - 8*s, at.y + 18*s}, IM_COL32(243,235,218,255), points.c_str());
            burnout::RowRule(draw, at.x, at.x + width, at.y + row);
            ImGui::PopID();
        }
    }
    void AchievementHelp()
    {
        std::string error = achievements::Current().Error();
        if (!error.empty()) { ImGui::TextWrapped("%s", locale::Text(error.c_str())); return; }
        if (const auto* d = Definition(s_selected))
        {
            auto entries = achievements::Current().Entries(s_user);
            auto it = std::find_if(entries.begin(), entries.end(), [&](auto& e) { return e.id == d->id; });
            const bool earned = it != entries.end() && it->unlocked;
            ImGui::TextWrapped("%s", (earned ? d->description : d->lockedDescription).c_str());
            ImGui::TextDisabled("%s", When(earned ? it->unlocked : 0).c_str());
        }
        else ImGui::TextWrapped("%s", locale::Text("Earn achievements by playing. Select one to see its requirements."));
    }
    bool AchievementNoticePending() { return s_notice.has_value() || achievements::Current().HasNotices(); }
    bool AchievementNoticeVisible()
    {
        auto now = std::chrono::steady_clock::now();
        if (s_notice && now - s_noticeSince >= std::chrono::seconds(6)) s_notice.reset();
        if (!s_notice)
            if (auto notice = achievements::Current().PopNotice()) { s_notice = notice; s_noticeSince = now; }
        return s_notice.has_value();
    }
    void DrawAchievementNotice()
    {
        if (!s_notice) return;
        const auto* d = Definition(s_notice->id); if (!d) return;
        float s = Scale(); auto* viewport = ImGui::GetMainViewport();
        float width = std::min(viewport->WorkSize.x - 32*s, 500*s);
        ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + 28*s}, 0, {0.5f,0});
        ImGui::SetNextWindowSize({width, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 28*s);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {18*s, 12*s});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.12f, 0.09f, 0.97f));
        constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##achievement-earned", nullptr, flags))
        {
            ImVec2 at = ImGui::GetCursorScreenPos(); Icon(*d, true, at, 48*s);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 64*s);
            ImGui::BeginGroup();
            ImGui::TextColored({0.61f, 0.82f, 0.31f, 1}, "%s", locale::Text("Achievement unlocked"));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 100*s);
            ImGui::Text(locale::Text("%u G - %s"), d->score, d->name.c_str());
            ImGui::PopTextWrapPos();
            if (s_notice->user) ImGui::TextDisabled(locale::Text("Player %u"), s_notice->user + 1);
            ImGui::EndGroup();
            ImGui::Dummy({0, std::max(0.0f, at.y + 48*s - ImGui::GetCursorScreenPos().y)});
        }
        ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(2);
    }
    void ReleaseAchievementArt()
    {
        for (auto& [id, art] : s_art)
            for (auto* texture : {art.color.get(), art.grey.get()})
                if (texture) ImGui::UnregisterUserTexture(texture);
        s_art.clear(); s_notice.reset();
    }
}
