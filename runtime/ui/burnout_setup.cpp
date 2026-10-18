// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The first-run screens' look, after the game's own menus: heavy condensed
// italic capitals (Barlow Condensed, OFL, standing in for the game's font,
// which isn't ours to ship), yellow and black hazard stripes, slanted slabs.
// Motion follows state (a page opening, a button lit, a copy in progress)
// and stops for Reduce motion. Navigation stays ImGui's own.
#include "burnout_setup.h"
#include "ui.h"
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && !TARGET_OS_IOS
#include <platform/macos_window.h>
#endif

namespace ui::burnout
{
    namespace
    {
        const unsigned char kDisplayFont[] = {
#include "barlow_ExtraBoldItalic.inc"
        };
        const unsigned char kLabelFont[] = {
#include "barlow_BoldItalic.inc"
        };
        const unsigned char kRowFont[] = {
#include "barlow_Bold.inc"
        };
        ImFont* s_display = nullptr;  // headings
        ImFont* s_label = nullptr;    // buttons, eyebrows, steps
        ImFont* s_row = nullptr;      // the settings rows (upright)

        std::unique_ptr<ImTextureData> s_cover;
        constexpr ImU32 kInk = IM_COL32(19, 18, 17, 255);
        constexpr ImU32 kPaper = IM_COL32(243, 235, 218, 255);
        constexpr ImU32 kYellow = IM_COL32(245, 182, 38, 255);
        constexpr ImU32 kRed = IM_COL32(205, 62, 26, 255);
        constexpr ImU32 kSteel = IM_COL32(48, 44, 37, 255);

        // A colour at the current style alpha (BeginReveal fades through it).
        ImU32 Faded(ImU32 color) { return ImGui::GetColorU32(color); }

        void Slab(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 color, float cut)
        {
            const ImVec2 points[] = { {a.x + cut, a.y}, {b.x, a.y}, {b.x - cut, b.y}, {a.x, b.y} };
            draw->AddConvexPolyFilled(points, 4, color);
        }

        // Diagonal yellow on ink between a and b, shifted by `phase` pixels.
        void Stripes(ImDrawList* draw, ImVec2 a, ImVec2 b, float pitch, float phase, ImU32 stripe, ImU32 ground)
        {
            draw->AddRectFilled(a, b, ground);
            draw->PushClipRect(a, b, true);
            const float h = b.y - a.y;
            for (float x = a.x - h - pitch + std::fmod(phase, pitch); x < b.x; x += pitch)
                Slab(draw, {x, a.y}, {x + pitch * .5f + h, b.y}, stripe, h);
            draw->PopClipRect();
        }

        ImFont* Or(ImFont* font) { return font ? font : ImGui::GetFont(); }
    }

    void LoadFonts()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        s_display = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kDisplayFont), int(sizeof(kDisplayFont)), 0, &config);
        s_label = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kLabelFont), int(sizeof(kLabelFont)), 0, &config);
        s_row = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kRowFont), int(sizeof(kRowFont)), 0, &config);
    }

    ImFont* DisplayFont() { return Or(s_display); }
    ImFont* LabelFont() { return Or(s_label); }
    ImFont* RowFont() { return Or(s_row); }

    void SelectorLines(ImDrawList* draw, ImVec2 a, ImVec2 b)
    {
        float s = Scale(), h = 3 * s, fade = std::min(60 * s, (b.x - a.x) * .2f);
        const ImU32 solid = Faded(kYellow), clear = Faded(kYellow & ~IM_COL32_A_MASK);
        for (float y : { a.y + h * .5f, b.y - h * .5f })
        {
            draw->AddRectFilledMultiColor({a.x, y - h * .5f}, {a.x + fade, y + h * .5f}, clear, solid, solid, clear);
            draw->AddRectFilled({a.x + fade, y - h * .5f}, {b.x - fade, y + h * .5f}, solid);
            draw->AddRectFilledMultiColor({b.x - fade, y - h * .5f}, {b.x, y + h * .5f}, solid, clear, clear, solid);
        }
    }

    void RowRule(ImDrawList* draw, float x0, float x1, float y)
    {
        float s = Scale(), fade = std::min(60 * s, (x1 - x0) * .2f);
        const ImU32 dark = Faded(IM_COL32(0, 0, 0, 170)), clear = Faded(IM_COL32(0, 0, 0, 0));
        draw->AddRectFilledMultiColor({x0, y}, {x0 + fade, y + 2 * s}, clear, dark, dark, clear);
        draw->AddRectFilled({x0 + fade, y}, {x1 - fade, y + 2 * s}, dark);
        draw->AddRectFilledMultiColor({x1 - fade, y}, {x1, y + 2 * s}, dark, clear, clear, dark);
    }

    float TitleBand(ImDrawList* draw, ImVec2 a, ImVec2 b)
    {
        float s = Scale(), h = b.y - a.y, cut = h * .35f;
        // Six chevrons, a strip as long as three of the old ones were wide
        // twice over, scrolling towards the heading.
        constexpr int kChevrons = 6;
        const float w = h * .30f, pitch = w * 1.1f, strip = pitch * kChevrons;
        const float x0 = a.x - 30 * s, text = x0 + strip + 12 * s;
        Slab(draw, {a.x - 40 * s, a.y}, {std::max(b.x, text + (b.x - a.x)) + 60 * s, b.y}, Faded(IM_COL32(0, 0, 0, 150)), cut);
        const float phase = ReducedMotion() ? 0.0f : std::fmod(float(ImGui::GetTime()) * pitch * 1.6f, pitch);
        draw->PushClipRect({x0, a.y}, {x0 + strip, b.y}, true);
        for (int i = -1; i < kChevrons; i++)
        {
            float x = x0 + i * pitch + phase;
            // Dim at the far end, full yellow nearest the heading.
            float t = std::clamp((x - x0) / strip, 0.0f, 1.0f);
            ImU32 color = IM_COL32(int(110 + 135 * t), int(80 + 102 * t), int(22 + 16 * t), int(90 + 165 * t));
            const ImVec2 points[] = { {x, a.y + h * .18f}, {x + w * .55f, a.y + h * .18f}, {x + w, a.y + h * .5f},
                {x + w * .55f, b.y - h * .18f}, {x, b.y - h * .18f}, {x + w * .45f, a.y + h * .5f} };
            draw->AddConcavePolyFilled(points, 6, Faded(color));
        }
        draw->PopClipRect();
        return text;
    }

    float ButtonGlyph(ImDrawList* draw, ImVec2 at, float height, const char* button)
    {
        float s = Scale();
        ImFont* font = Or(s_label);
        float size = height * .72f;
        ImVec2 text = font->CalcTextSizeA(size, FLT_MAX, 0, button);
        // The Xbox 360 pad's colours, as the game's own hints show them.
        ImU32 fill = IM_COL32(70, 66, 60, 255);
        if (!std::strcmp(button, "A")) fill = IM_COL32(96, 168, 36, 255);
        else if (!std::strcmp(button, "B")) fill = IM_COL32(200, 40, 32, 255);
        else if (!std::strcmp(button, "X")) fill = IM_COL32(32, 104, 196, 255);
        else if (!std::strcmp(button, "Y")) fill = IM_COL32(232, 180, 24, 255);
        bool disc = std::strlen(button) == 1;
        float w = disc ? height : text.x + height * .7f;
        if (!draw)
            return w;  // measured only
        ImVec2 b(at.x + w, at.y + height);
        draw->AddRectFilled(at, b, Faded(fill), height * .5f);
        draw->AddRect(at, b, Faded(IM_COL32(255, 255, 255, 70)), height * .5f, 0, 1.2f * s);
        draw->AddText(font, size, {at.x + (w - text.x) * .5f, at.y + (height - text.y) * .5f}, Faded(kPaper), button);
        return w;
    }

    void HazardDashes(ImDrawList* draw, ImVec2 a, ImVec2 b)
    {
        float s = Scale();
        float h = 4 * s;
        for (float y : { a.y, b.y })
            Stripes(draw, {a.x, y - h * .5f}, {b.x, y + h * .5f}, 28 * s, 0, Faded(kYellow), Faded(IM_COL32(30, 27, 22, 255)));
    }

    bool ReducedMotion()
    {
        const char* reduced = std::getenv("BR_REDUCED_MOTION");
        if (reduced && *reduced)
            return *reduced != '0';
#if defined(__APPLE__) && !TARGET_OS_IOS
        // Asked of AppKit once a second, not every frame.
        static double checked = -10;
        static bool system = false;
        if (double now = ImGui::GetTime(); now - checked >= 1)
        {
            checked = now;
            system = platform::macos::ReduceMotion();
        }
        return system;
#else
        return false;
#endif
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

    void Backdrop(bool racing)
    {
        auto* v = ImGui::GetMainViewport();
        auto* d = ImGui::GetBackgroundDrawList();
        ImVec2 a = v->WorkPos, b(a.x + v->WorkSize.x, a.y + v->WorkSize.y);
        d->AddRectFilled(a, b, kInk);
        float s = Scale();
        // Faint speed lines, still: the page stays readable.
        for (float x = a.x - v->WorkSize.y; x < b.x; x += 120 * s)
            d->AddLine({x, b.y}, {x + v->WorkSize.y * .6f, a.y}, IM_COL32(255, 240, 210, 6), 18 * s);
        // The hazard bands of the game's pause menu, top and bottom. They
        // drift only while the game installs.
        float phase = racing && !ReducedMotion() ? float(ImGui::GetTime() * 40 * s) : 0;
        Stripes(d, {a.x, a.y}, {b.x, a.y + 10 * s}, 36 * s, phase, kYellow, kInk);
        Stripes(d, {a.x, b.y - 10 * s}, {b.x, b.y}, 36 * s, -phase, kYellow, kInk);
        d->AddRectFilled({a.x, a.y + 10 * s}, {b.x, a.y + 12 * s}, kRed);
        d->AddRectFilled({a.x, b.y - 12 * s}, {b.x, b.y - 10 * s}, kRed);
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
            d->AddText(Or(s_display), 64 * s, {p.x + 18 * s, p.y + 80 * s}, kPaper, "BURNOUT");
            d->AddText(Or(s_display), 44 * s, {p.x + 22 * s, p.y + 142 * s}, kYellow, "REVENGE");
        }
        d->AddRectFilled(p, {p.x + size.x, p.y + 40 * s}, IM_COL32(26, 23, 20, 255));
        d->AddText(Or(s_label), 16 * s, {p.x + 20 * s, p.y + 11 * s}, kPaper, "BURNOUT REVENGE  /  XBOX 360");
        float bottom = p.y + size.y - 62 * s;
        d->AddRectFilled({p.x, bottom}, {p.x + size.x, p.y + size.y}, IM_COL32(26, 23, 20, 255));
        d->AddRectFilled({p.x, bottom}, {p.x + 6 * s, p.y + size.y}, kRed);
        d->AddText(Or(s_display), 24 * s, {p.x + 20 * s, bottom + 7 * s}, kYellow, "TRAFFIC. TAKEDOWNS. REVENGE.");
        d->AddText(Or(s_label), 14 * s, {p.x + 20 * s, bottom + 36 * s}, IM_COL32(184, 173, 151, 255), "NATIVE ON APPLE SILICON");
    }

    bool Button(const char* label, ImVec2 size, bool primary)
    {
        float s = Scale();
        ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
        bool hit = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
        ImGui::PopStyleColor();
        bool focus = ImGui::IsItemFocused(), hover = ImGui::IsItemHovered();
        bool disabled = ImGui::GetItemFlags() & ImGuiItemFlags_Disabled;
        bool lit = (hover || focus) && !disabled;
        // How lit, 0..1, eased in over ~0.12 s and kept per button.
        float* glow = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetItemID(), 0.0f);
        if (ReducedMotion())
            *glow = lit ? 1.0f : 0.0f;
        else
            *glow = std::clamp(*glow + (lit ? 1 : -1) * ImGui::GetIO().DeltaTime / .12f, 0.0f, 1.0f);
        float ease = 1 - (1 - *glow) * (1 - *glow);

        auto* d = ImGui::GetWindowDrawList();
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        float cut = 12 * s;
        ImU32 background = primary ? kYellow : kSteel;
        if (lit) background = primary ? IM_COL32(255, 205, 78, 255) : IM_COL32(70, 60, 42, 255);
        if (ImGui::IsItemActive()) background = primary ? IM_COL32(223, 144, 25, 255) : IM_COL32(98, 70, 31, 255);
        if (disabled) background = IM_COL32(46, 42, 36, 255);
        Slab(d, a, b, Faded(background), cut);
        // The game's highlight: a wedge that boosts in from the left.
        if (ease > 0)
        {
            float w = cut + 6 * s + ease * 12 * s;
            Slab(d, a, {a.x + w, b.y}, Faded(primary ? kRed : kYellow), cut);
        }
        if (focus && !disabled)
        {
            const ImVec2 outline[] = { {a.x + cut, a.y}, {b.x, a.y}, {b.x - cut, b.y}, {a.x, b.y} };
            d->AddPolyline(outline, 4, Faded(kPaper), ImDrawFlags_Closed, 2 * s);
        }
        const char* end = ImGui::FindRenderedTextEnd(label);
        float textSize = std::clamp(size.y * .46f, 17 * s, 27 * s);
        ImFont* font = Or(s_label);
        ImVec2 text = font->CalcTextSizeA(textSize, FLT_MAX, 0, label, end);
        ImU32 color = disabled ? IM_COL32(135, 125, 107, 255) : primary ? kInk : kPaper;
        d->AddText(font, textSize, {a.x + 28 * s + ease * 8 * s, a.y + (size.y - text.y) * .5f}, Faded(color), label, end);
        // A broad hit area and normal ImGui navigation remain underneath
        // the clipped silhouette. No custom keyboard or controller scheme.
        return hit;
    }

    void Heading(const char* text, float size)
    {
        ImGui::PushFont(Or(s_display), size);
        ImGui::TextWrapped("%s", text);
        ImGui::PopFont();
    }

    void Eyebrow(const char* text)
    {
        ImGui::PushFont(Or(s_label), 17);
        ImGui::TextColored(ImVec4(.96f, .71f, .15f, 1), "%s", text);
        ImGui::PopFont();
    }

    void Figure(const char* text, float size)
    {
        ImGui::PushFont(Or(s_display), size);
        ImGui::TextColored(ImVec4(.96f, .71f, .15f, 1), "%s", text);
        ImGui::PopFont();
    }

    bool Link(const char* label)
    {
        ImGui::PushFont(Or(s_label), 15);
        ImVec2 size = ImGui::CalcTextSize(label, ImGui::FindRenderedTextEnd(label));
        ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
        bool hit = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
        ImGui::PopStyleColor();
        bool lit = ImGui::IsItemHovered() || ImGui::IsItemFocused();
        ImVec2 a = ImGui::GetItemRectMin();
        auto* d = ImGui::GetWindowDrawList();
        d->AddText(a, Faded(lit ? kYellow : kPaper), label, ImGui::FindRenderedTextEnd(label));
        if (lit)
            d->AddRectFilled({a.x, a.y + size.y}, {a.x + size.x, a.y + size.y + 2 * Scale()}, Faded(kYellow));
        ImGui::PopFont();
        return hit;
    }

    void Steps(int current)
    {
        static const char* const kSteps[] = { "01  YOUR GAME", "02  SETUP", "03  INSTALL" };
        float s = Scale();
        float gap = 10 * s, width = (ImGui::GetContentRegionAvail().x - 2 * gap) / 3;
        ImVec2 at = ImGui::GetCursorScreenPos();
        auto* d = ImGui::GetWindowDrawList();
        ImFont* font = Or(s_label);
        for (int i = 0; i < 3; i++)
        {
            ImVec2 a(at.x + i * (width + gap), at.y);
            bool done = i + 1 < current, now = i + 1 == current;
            ImU32 bar = done ? IM_COL32(150, 112, 30, 255) : now ? kYellow : kSteel;
            ImU32 color = now ? kPaper : done ? IM_COL32(184, 173, 151, 255) : IM_COL32(110, 102, 88, 255);
            d->AddText(font, 15 * s, a, Faded(color), kSteps[i]);
            Slab(d, {a.x, a.y + 22 * s}, {a.x + width, a.y + 27 * s}, Faded(bar), 4 * s);
        }
        ImGui::Dummy({ImGui::GetContentRegionAvail().x, 27 * s});
    }

    void HazardBar(float fraction, float height)
    {
        float s = Scale();
        ImVec2 a = ImGui::GetCursorScreenPos();
        ImVec2 b(a.x + ImGui::GetContentRegionAvail().x, a.y + height);
        ImGui::Dummy({b.x - a.x, height});
        auto* d = ImGui::GetWindowDrawList();
        d->AddRectFilled(a, b, Faded(IM_COL32(40, 36, 30, 255)));
        float phase = ReducedMotion() ? 0 : float(ImGui::GetTime() * 60 * s);
        float end = fraction < 0 ? b.x : a.x + (b.x - a.x) * std::clamp(fraction, 0.0f, 1.0f);
        if (end > a.x)
            Stripes(d, a, {end, b.y}, 22 * s, phase, Faded(kYellow), Faded(IM_COL32(200, 140, 20, 255)));
    }

    void BeginReveal(double pageStarted, int index)
    {
        float t = ReducedMotion() ? 1.0f
            : std::clamp(float((ImGui::GetTime() - pageStarted - index * .055) / .24), 0.0f, 1.0f);
        float ease = 1 - (1 - t) * (1 - t) * (1 - t);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * ease);
        float shift = (1 - ease) * 22 * Scale();
        ImGui::GetStateStorage()->SetFloat(ImGui::GetID("##reveal-shift"), shift);
        if (shift > .01f)
            ImGui::Indent(shift);
    }

    void EndReveal()
    {
        float shift = ImGui::GetStateStorage()->GetFloat(ImGui::GetID("##reveal-shift"));
        if (shift > .01f)
            ImGui::Unindent(shift);
        ImGui::PopStyleVar();
    }
}
