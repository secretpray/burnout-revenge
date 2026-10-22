#include <user/localization.h>
// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The first-run screens' look, after the game's own menus: heavy condensed
// italic capitals (Barlow Condensed, OFL, standing in for the game's font,
// which isn't ours to ship), yellow and black hazard stripes, slanted slabs.
// Motion follows state (a page opening, a button lit, a copy in progress)
// and stops for Reduce motion. Navigation stays ImGui's own.
#include "burnout_setup.h"
#include "ui.h"
#include <hid/input.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
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
        // Barlow has Latin only. Text from outside (a song's tags, a folder's
        // name) can be Cyrillic, Greek or CJK: the system's Arial Narrow (of
        // the same cut) and then Arial Unicode draw what Barlow lacks. Read
        // from macOS at run time, not shipped.
        auto fallbacks = [&io](const char* narrow) {
#ifdef __APPLE__
            ImFontConfig merge;
            merge.MergeMode = true;
            for (const char* file : { narrow, "/System/Library/Fonts/Supplemental/Arial Unicode.ttf" })
                if (std::filesystem::exists(file))
                    io.Fonts->AddFontFromFileTTF(file, 0, &merge);
#else
            (void)io;
            (void)narrow;
#endif
        };
        s_display = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kDisplayFont), int(sizeof(kDisplayFont)), 0, &config);
        fallbacks("/System/Library/Fonts/Supplemental/Arial Narrow Bold Italic.ttf");
        s_label = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kLabelFont), int(sizeof(kLabelFont)), 0, &config);
        fallbacks("/System/Library/Fonts/Supplemental/Arial Narrow Bold Italic.ttf");
        s_row = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kRowFont), int(sizeof(kRowFont)), 0, &config);
        fallbacks("/System/Library/Fonts/Supplemental/Arial Narrow Bold.ttf");
    }

    ImFont* DisplayFont() { return Or(s_display); }
    ImFont* LabelFont() { return Or(s_label); }
    ImFont* RowFont() { return Or(s_row); }

    void Hints(std::initializer_list<std::pair<const char*, const char*>> items, float size)
    {
        const float s = Scale();
        ImGui::PushFont(Or(s_label), size);
        const float h = ImGui::GetFontSize() * 1.1f, gap = 6 * s, between = 18 * s;
        auto glyphs = [&](ImDrawList* draw, ImVec2 at, const char* list) {
            float x = at.x;
            std::string all(list);
            for (size_t start = 0; start <= all.size();)
            {
                size_t space = all.find(' ', start);
                std::string one = all.substr(start, space == std::string::npos ? std::string::npos : space - start);
                x += ButtonGlyph(draw, ImVec2(x, at.y), h, one.c_str()) + 3 * s;
                if (space == std::string::npos)
                    break;
                start = space + 1;
            }
            return x - at.x - 3 * s;
        };
        float width = 0;
        for (auto [label, button] : items)
            width += ImGui::CalcTextSize(locale::Text(label)).x + gap + glyphs(nullptr, ImVec2(), button) + between;
        width -= between;
        ImVec2 at = ImGui::GetCursorScreenPos();
        const float frame = ImGui::GetFrameHeight();
        at.x = std::max(at.x, ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - width);
        at.y += (frame - h) * 0.5f;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        for (auto [label, button] : items)
        {
            label = locale::Text(label);
            const ImVec2 text = ImGui::CalcTextSize(label);
            draw->AddText(ImVec2(at.x, at.y + (h - text.y) * 0.5f), Faded(kPaper), label);
            at.x += text.x + gap;
            at.x += glyphs(draw, at, button) + between;
        }
        ImGui::Dummy(ImVec2(0, frame));
        ImGui::PopFont();
    }

    void PushDialogStyle()
    {
        const float s = Scale();
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.035f, 0.032f, 0.028f, 0.97f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.032f, 0.028f, 0.97f));  // a plain window's (What's new)
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.6f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(34 * s, 26 * s));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * s, 6 * s));
    }

    void PopDialogStyle()
    {
        ImGui::PopStyleVar(6);
        ImGui::PopStyleColor(3);
    }

    void EndDialog(bool popup)
    {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::PopFont();
        if (popup)
            ImGui::EndPopup();
        else
            ImGui::End();
    }

    void DialogFrame()
    {
        // The game's frame: white, a little inside the panel's edge, with
        // notches along its top and bottom (SAVE/LOAD's player file box).
        const float s = Scale();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 a = ImGui::GetWindowPos(), b(a.x + ImGui::GetWindowWidth(), a.y + ImGui::GetWindowHeight());
        a.x += 6 * s; a.y += 6 * s; b.x -= 6 * s; b.y -= 6 * s;
        const ImU32 white = Faded(IM_COL32(236, 232, 222, 255));
        draw->AddRect(a, b, white, 0, 0, 3 * s);
        for (float x = a.x + 70 * s; x < b.x - 30 * s; x += 120 * s)
        {
            draw->AddLine({x, a.y}, {x, a.y + 9 * s}, white, 3 * s);
            draw->AddLine({x, b.y}, {x, b.y - 9 * s}, white, 3 * s);
        }
        ImGui::PushFont(Or(s_label), 24);
        // Any plain button inside (the update notes' links and the like):
        // the game's dark band, yellow when lit, square.
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.21f, 0.16f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.84f, 0.62f, 0.13f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.96f, 0.71f, 0.15f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    }

    void DialogText(const char* text, float width)
    {
        text = locale::Text(text);
        // Centred lines, wrapped at `width`, white with the game's shadow.
        ImFont* font = ImGui::GetFont();
        const float size = ImGui::GetFontSize(), s = Scale();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const char* end = text + std::strlen(text);
        for (const char* line = text; line < end;)
        {
            const char* wrap = font->CalcWordWrapPosition(size, line, end, width);
            if (wrap == line)
                wrap = line + 1;
            const char* stop = wrap;
            while (stop > line && (stop[-1] == ' ' || stop[-1] == '\n'))
                stop--;
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float w = font->CalcTextSizeA(size, FLT_MAX, 0, line, stop).x;
            const ImVec2 pos(at.x + (width - w) * 0.5f, at.y);
            draw->AddText(font, size, {pos.x + 2 * s, pos.y + 2 * s}, Faded(IM_COL32(0, 0, 0, 200)), line, stop);
            draw->AddText(font, size, pos, Faded(kPaper), line, stop);
            ImGui::Dummy(ImVec2(width, size * 1.15f));
            line = wrap;
            while (line < end && (*line == ' ' || *line == '\n'))
                line++;
        }
    }

    bool DialogOption(const char* label, float width)
    {
        const char* identity = label;
        label = locale::Text(label);
        // A choice: the focused (or hovered) one as the game's selection,
        // large and white on a yellow band; the others orange on dark bands.
        const float s = Scale();
        const float height = 44 * s;
        ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
        const bool hit = ImGui::InvisibleButton(identity, {width, height}, ImGuiButtonFlags_EnableNav);
        ImGui::PopStyleColor();
        const bool lit = ImGui::IsItemFocused() || ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const float fade = std::min(70 * s, width * 0.25f);
        const ImU32 band = lit ? IM_COL32(214, 158, 32, 235) : IM_COL32(60, 54, 40, 200);
        const ImU32 clear = band & ~IM_COL32_A_MASK;
        draw->AddRectFilledMultiColor(a, {a.x + fade, b.y}, Faded(clear), Faded(band), Faded(band), Faded(clear));
        draw->AddRectFilled({a.x + fade, a.y}, {b.x - fade, b.y}, Faded(band));
        draw->AddRectFilledMultiColor({b.x - fade, a.y}, b, Faded(band), Faded(clear), Faded(clear), Faded(band));
        ImFont* font = Or(s_label);
        float size = lit ? 34 * s : 27 * s;
        const char* end = ImGui::FindRenderedTextEnd(label);
        ImVec2 text = font->CalcTextSizeA(size, FLT_MAX, 0, label, end);
        if (text.x > width - 32 * s)
        {
            size = std::max(18 * s, size * (width - 32 * s) / text.x);
            text = font->CalcTextSizeA(size, FLT_MAX, 0, label, end);
        }
        const ImVec2 pos(a.x + (width - text.x) * 0.5f, a.y + (height - text.y) * 0.5f);
        draw->AddText(font, size, {pos.x + 2 * s, pos.y + 2 * s}, Faded(IM_COL32(0, 0, 0, 190)), label, end);
        draw->AddText(font, size, pos, Faded(lit ? kPaper : IM_COL32(242, 154, 34, 255)), label, end);
        return hit;
    }

    void NoticeBand(ImDrawList* draw, ImVec2 a, ImVec2 b)
    {
        const float s = Scale(), fade = std::min(90 * s, (b.x - a.x) * 0.2f);
        const ImU32 band = IM_COL32(150, 112, 34, 225), clear = band & ~IM_COL32_A_MASK;
        draw->AddRectFilledMultiColor(a, {a.x + fade, b.y}, clear, band, band, clear);
        draw->AddRectFilled({a.x + fade, a.y}, {b.x - fade, b.y}, band);
        draw->AddRectFilledMultiColor({b.x - fade, a.y}, b, band, clear, clear, band);
    }

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
        // The buttons the game's prompts show too (hid::PromptGlyphs: the
        // Button Prompts setting, Auto the device used last): Xbox 360's
        // lettered discs, PlayStation's shapes (L1/R1 for the shoulders), or
        // the keyboard's keys as caps.
        const float s = Scale();
        const hid::Glyphs set = hid::PromptGlyphs();
        std::string label = button;
        if (set == hid::Glyphs::PlayStation)
            label = label == "LB" ? "L1" : label == "RB" ? "R1" : label;
        else if (set == hid::Glyphs::Keyboard)
            label = label == "A" ? "ENTER" : label == "B" ? "ESC" : label == "X" ? "DEL" : label == "Y" ? "BKSP"
                : label == "LB" ? "Q" : label == "RB" ? "E" : label == "D-PAD" ? "ARROWS" : label;
        const bool face = set != hid::Glyphs::Keyboard && label.size() == 1 && std::strchr("ABXY", label[0]);
        ImFont* font = Or(s_label);
        const float size = height * .72f;
        const ImVec2 text = font->CalcTextSizeA(size, FLT_MAX, 0, label.c_str());
        const float w = face ? height : std::max(height, text.x + height * .7f);
        if (!draw)
            return w;  // measured only
        const ImVec2 b(at.x + w, at.y + height);
        if (face && set == hid::Glyphs::PlayStation)
        {
            // A dark disc with the shape in its colour.
            const ImVec2 c(at.x + height * .5f, at.y + height * .5f);
            const float r = height * .5f, k = height * .22f, line = std::max(1.5f, 2.2f * s);
            draw->AddCircleFilled(c, r, Faded(IM_COL32(30, 30, 34, 255)));
            draw->AddCircle(c, r, Faded(IM_COL32(255, 255, 255, 70)), 0, 1.2f * s);
            switch (label[0])
            {
            case 'A':  // cross
            {
                const ImU32 blue = Faded(IM_COL32(124, 178, 232, 255));
                draw->AddLine({c.x - k, c.y - k}, {c.x + k, c.y + k}, blue, line);
                draw->AddLine({c.x - k, c.y + k}, {c.x + k, c.y - k}, blue, line);
                break;
            }
            case 'B':  // circle
                draw->AddCircle(c, k * 1.05f, Faded(IM_COL32(232, 84, 92, 255)), 0, line);
                break;
            case 'X':  // square
                draw->AddRect({c.x - k * .9f, c.y - k * .9f}, {c.x + k * .9f, c.y + k * .9f}, Faded(IM_COL32(222, 140, 200, 255)), 0, 0, line);
                break;
            default:  // 'Y': triangle
                draw->AddTriangle({c.x, c.y - k * 1.05f}, {c.x + k * 1.05f, c.y + k * .8f}, {c.x - k * 1.05f, c.y + k * .8f},
                    Faded(IM_COL32(64, 200, 170, 255)), line);
                break;
            }
            return w;
        }
        // Xbox 360's colours for the face buttons; a dark pill or key cap otherwise.
        ImU32 fill = set == hid::Glyphs::Keyboard ? IM_COL32(52, 50, 46, 255) : IM_COL32(70, 66, 60, 255);
        if (face && label == "A") fill = IM_COL32(96, 168, 36, 255);
        else if (face && label == "B") fill = IM_COL32(200, 40, 32, 255);
        else if (face && label == "X") fill = IM_COL32(32, 104, 196, 255);
        else if (face && label == "Y") fill = IM_COL32(232, 180, 24, 255);
        const float round = set == hid::Glyphs::Keyboard ? height * .2f : height * .5f;
        draw->AddRectFilled(at, b, Faded(fill), round);
        draw->AddRect(at, b, Faded(IM_COL32(255, 255, 255, set == hid::Glyphs::Keyboard ? 110 : 70)), round, 0, 1.2f * s);
        draw->AddText(font, size, {at.x + (w - text.x) * .5f, at.y + (height - text.y) * .5f}, Faded(kPaper), label.c_str());
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
        d->AddText(Or(s_display), 24 * s, {p.x + 20 * s, bottom + 7 * s}, kYellow, locale::Text("TRAFFIC. TAKEDOWNS. REVENGE."));
        d->AddText(Or(s_label), 14 * s, {p.x + 20 * s, bottom + 36 * s}, IM_COL32(184, 173, 151, 255), locale::Text("NATIVE ON APPLE SILICON"));
    }

    bool Button(const char* label, ImVec2 size, bool primary)
    {
        const char* identity = label;
        label = locale::Text(label);
        float s = Scale();
        ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
        bool hit = ImGui::InvisibleButton(identity, size, ImGuiButtonFlags_EnableNav);
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
        if (text.x > size.x - 64 * s)
        {
            textSize = std::max(17 * s, textSize * (size.x - 64 * s) / text.x);
            text = font->CalcTextSizeA(textSize, FLT_MAX, 0, label, end);
        }
        ImU32 color = disabled ? IM_COL32(135, 125, 107, 255) : primary ? kInk : kPaper;
        d->AddText(font, textSize, {a.x + 28 * s + ease * 8 * s, a.y + (size.y - text.y) * .5f}, Faded(color), label, end);
        // A broad hit area and normal ImGui navigation remain underneath
        // the clipped silhouette. No custom keyboard or controller scheme.
        return hit;
    }

    void Heading(const char* text, float size)
    {
        text = locale::Text(text);
        ImGui::PushFont(Or(s_display), size);
        ImGui::TextWrapped("%s", text);
        ImGui::PopFont();
    }

    void Eyebrow(const char* text)
    {
        text = locale::Text(text);
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
        label = locale::Text(label);
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
            d->AddText(font, 15 * s, a, Faded(color), locale::Text(kSteps[i]));
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
