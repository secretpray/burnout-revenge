// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include "burnout_setup.h"
#include "ui.h"
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>

namespace ui::burnout
{
    namespace
    {
        std::unique_ptr<ImTextureData> s_cover;
        constexpr ImU32 kInk = IM_COL32(19, 18, 17, 255);
        constexpr ImU32 kPaper = IM_COL32(243, 235, 218, 255);
        constexpr ImU32 kYellow = IM_COL32(245, 182, 38, 255);
        void Slab(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 color, float cut)
        {
            const ImVec2 points[] = { {a.x + cut, a.y}, {b.x, a.y}, {b.x - cut, b.y}, {a.x, b.y} };
            draw->AddConvexPolyFilled(points, 4, color);
        }
    }

    void LoadArt()
    {
        if (s_cover) return;
        const char* base = SDL_GetBasePath();
        if (!base) return;
        // The player's artwork is supplied when packaging, never downloaded
        // or compiled into the source tree. No access to Desktop at runtime.
        auto file = std::filesystem::path(base) / "../Resources/BurnoutCover.bmp";
        SDL_Surface* loaded = SDL_LoadBMP(file.string().c_str());
        if (!loaded) return; // a source build has a typographic fallback
        SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(loaded);
        if (!rgba) return;
        s_cover = std::make_unique<ImTextureData>();
        s_cover->Create(ImTextureFormat_RGBA32, rgba->w, rgba->h);
        s_cover->UseColors = true;
        for (int y = 0; y < rgba->h; ++y)
            std::memcpy(s_cover->GetPixelsAt(0, y), static_cast<unsigned char*>(rgba->pixels) + y * rgba->pitch, rgba->w * 4);
        SDL_DestroySurface(rgba);
        ImGui::RegisterUserTexture(s_cover.get());
    }

    void ReleaseArt()
    {
        if (!s_cover) return;
        ImGui::UnregisterUserTexture(s_cover.get());
        s_cover.reset();
    }

    void Backdrop()
    {
        auto* v = ImGui::GetMainViewport();
        auto* d = ImGui::GetBackgroundDrawList();
        ImVec2 a = v->WorkPos, b(a.x + v->WorkSize.x, a.y + v->WorkSize.y);
        d->AddRectFilled(a, b, kInk);
        float s = Scale();
        // Static tyre-mark rhythm. Motion belongs to state transitions, so
        // the screen remains readable while a player chooses a folder.
        for (float x = a.x - 50 * s; x < b.x; x += 56 * s)
        {
            Slab(d, {x, a.y}, {x + 35 * s, a.y + 6 * s}, IM_COL32(178, 54, 25, 255), 5 * s);
            Slab(d, {x, b.y - 6 * s}, {x + 35 * s, b.y}, kYellow, 5 * s);
        }
    }

    void Poster(ImVec2 p, ImVec2 size)
    {
        auto* d = ImGui::GetWindowDrawList();
        float s = Scale();
        d->AddRectFilled(p, {p.x + size.x, p.y + size.y}, IM_COL32(31, 28, 24, 255));
        if (s_cover)
        {
            float side = std::min(size.x, size.y);
            float y = p.y + (size.y - side) * 0.5f;
            d->AddImage(s_cover->GetTexRef(), {p.x, y}, {p.x + side, y + side});
        }
        else
        {
            d->AddText(ImGui::GetFont(), 46 * s, {p.x + 18 * s, p.y + 80 * s}, kPaper, "BURNOUT");
            d->AddText(ImGui::GetFont(), 32 * s, {p.x + 18 * s, p.y + 132 * s}, kYellow, "REVENGE");
        }
        d->AddRectFilled(p, {p.x + size.x, p.y + 40 * s}, IM_COL32(26, 23, 20, 255));
        d->AddText(ImGui::GetFont(), 13 * s, {p.x + 20 * s, p.y + 12 * s}, kPaper, "BURNOUT REVENGE  /  XBOX 360");
        float bottom = p.y + size.y - 62 * s;
        d->AddRectFilled({p.x, bottom}, {p.x + size.x, p.y + size.y}, IM_COL32(26, 23, 20, 255));
        d->AddText(ImGui::GetFont(), 17 * s, {p.x + 20 * s, bottom + 11 * s}, kYellow, "TRAFFIC. TAKEDOWNS. REVENGE.");
        d->AddText(ImGui::GetFont(), 12 * s, {p.x + 20 * s, bottom + 35 * s}, IM_COL32(184, 173, 151, 255), "NATIVE ON APPLE SILICON");
    }

    bool Button(const char* label, ImVec2 size, bool primary)
    {
        float s = Scale();
        ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
        bool hit = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
        ImGui::PopStyleColor();
        bool focus = ImGui::IsItemFocused(), hover = ImGui::IsItemHovered();
        bool disabled = ImGui::GetItemFlags() & ImGuiItemFlags_Disabled;
        auto* d = ImGui::GetWindowDrawList();
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImU32 background = primary ? kYellow : IM_COL32(48, 44, 37, 255);
        if (hover || focus) background = primary ? IM_COL32(255, 205, 78, 255) : IM_COL32(79, 65, 39, 255);
        if (ImGui::IsItemActive()) background = primary ? IM_COL32(223, 144, 25, 255) : IM_COL32(98, 70, 31, 255);
        if (disabled) background = IM_COL32(46, 42, 36, 255);
        Slab(d, a, b, background, 10 * s);
        if (focus)
        {
            const ImVec2 outline[] = { {a.x + 10 * s, a.y}, {b.x, a.y}, {b.x - 10 * s, b.y}, {a.x, b.y} };
            d->AddPolyline(outline, 4, kPaper, ImDrawFlags_Closed, 2 * s);
        }
        const char* end = ImGui::FindRenderedTextEnd(label);
        ImVec2 text = ImGui::CalcTextSize(label, end);
        ImU32 color = disabled ? IM_COL32(135, 125, 107, 255) : primary ? kInk : kPaper;
        d->AddText({a.x + 22 * s, a.y + (size.y - text.y) * .5f}, color, label, end);
        // A broad hit area and normal ImGui navigation remain underneath
        // the clipped silhouette. No custom keyboard or controller scheme.
        return hit;
    }

    void Heading(const char* text, float size)
    {
        ImGui::PushFont(nullptr, size);
        ImGui::TextWrapped("%s", text);
        ImGui::PopFont();
    }

    void Eyebrow(const char* text)
    {
        ImGui::PushFont(nullptr, 13);
        ImGui::TextColored(ImVec4(.96f, .71f, .15f, 1), "%s", text);
        ImGui::PopFont();
    }
}
