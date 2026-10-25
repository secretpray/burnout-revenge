// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Optional retail Crash bar reconstruction. Geometry and UVs were
// checked against PS2/Xbox; pixels come from the player's installed 360 files.
#if defined(BR_GAME_BURNOUT)
#include "crash_launch_hud.h"
#include "crash_launch_art.h"
#include "burnout_setup.h"
#include <game/crash_launch_status.h>
#include <hid/input.h>
#include <user/settings.h>
#include <user/localization.h>
#include <SDL3/SDL_keyboard.h>
#include <imgui_impl_vulkan.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>

namespace ui
{
    namespace
    {
        VkPipeline additive{};
        void Additive(const ImDrawList*, const ImDrawCmd*)
        {
            auto* state = static_cast<ImGui_ImplVulkan_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
            if (state && additive) vkCmdBindPipeline(state->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, additive);
        }
        constexpr float sweep = 1.483529806f, radiusPerWidth = 9.488803958f;
        constexpr float start = std::numbers::pi_v<float> - sweep * .5f;
        struct Arc
        {
            ImDrawList* draw;
            ImVec2 center;
            float radius, alpha;
            ImVec2 Point(float level, float radial) const
            {
                const float angle = start + level * sweep;
                return {center.x + std::cos(angle) * radius * radial, center.y + std::sin(angle) * radius * radial};
            }
            void Strip(crash_art::Sprite sprite, float low, float high, ImVec2 uv0, ImVec2 uv1,
                float inner0, float outer0, float inner1, float outer1, ImU32 color, unsigned segments,
                float innerSkew = 0, float outerSkew = 0) const
            {
                color = (color & 0x00FFFFFFu) | (unsigned(float(color >> 24) * alpha) << 24);
                auto uv = [&](float u, float v) { return ImVec2(sprite.low.x + (sprite.high.x - sprite.low.x) * u,
                    sprite.low.y + (sprite.high.y - sprite.low.y) * v); };
                for (unsigned i = 0; i < segments; ++i)
                {
                    const float a = float(i) / segments, b = float(i + 1) / segments;
                    const float la = std::lerp(low, high, a), lb = std::lerp(low, high, b);
                    const float ua = std::lerp(uv0.x, uv1.x, a), ub = std::lerp(uv0.x, uv1.x, b);
                    draw->AddImageQuad(sprite.texture,
                        Point(la + innerSkew, std::lerp(inner0, inner1, a)), Point(lb + innerSkew, std::lerp(inner0, inner1, b)),
                        Point(lb + outerSkew, std::lerp(outer0, outer1, b)), Point(la + outerSkew, std::lerp(outer0, outer1, a)),
                        uv(ua, uv0.y), uv(ub, uv0.y), uv(ub, uv1.y), uv(ua, uv1.y), color);
                }
            }
            void Ring(crash_art::Sprite sprite, float low, float high, ImVec2 uv0, ImVec2 uv1,
                float inner, float outer, ImU32 color, unsigned segments) const
            { Strip(sprite, low, high, uv0, uv1, inner, outer, inner, outer, color, segments); }
        };
        std::string Frame(const char* prefix, float time, unsigned count)
        {
            char name[40];
            std::snprintf(name, sizeof(name), "%s%02u", prefix, unsigned(std::max(0.f, time) * 30) % count + 1);
            return name;
        }
    }
    void ReleaseCrashLaunchRenderer(VkDevice device, bool releaseArt)
    {
        if (additive) vkDestroyPipeline(device, additive, nullptr);
        additive = {};
        if (releaseArt) crash_art::Release();
    }
    void DrawCrashLaunch(const game::crash_launch::Status& status)
    {
        if (!status.visible || !crash_art::Load()) return;
        if (!additive) additive = ImGui_ImplVulkan_CreateAdditivePipeline();
        const auto& m = status.model;
        const auto* viewport = ImGui::GetMainViewport();
        const auto size = viewport->WorkSize;
        const float scale = std::min(size.y / 448.f, size.x / (448.f * 16.f / 9.f));
        const float left = viewport->WorkPos.x + (size.x - 448.f * 16.f / 9.f * scale) * .5f;
        const float top = viewport->WorkPos.y + (size.y - 448.f * scale) * .5f;
        // Retail width=24, height=337.846, position=(75,0), centred vertically.
        const float radius = 24 * radiusPerWidth;
        auto* draw = ImGui::GetForegroundDrawList();
        const Arc arc{draw, {left + (75 + radius) * scale, top + 224 * scale}, radius * scale, status.alpha};
        const auto bar = crash_art::Get("CrashBar");
        constexpr ImU32 white = IM_COL32(255, 255, 255, 255);
        constexpr float inner = 1 - .5f / radiusPerWidth, outer = 1 + .5f / radiusPerWidth;
        arc.Ring(bar, 0, 1, {1.f / 256, 1.f / 32}, {184.f / 256, 27.f / 32}, inner, outer, white, 12);
        for (const auto range : {std::pair{m.parameters.limits[1], m.parameters.limits[2]},
            std::pair{m.parameters.limits[4], m.parameters.limits[5]}})
            arc.Ring(bar, range.first, range.second, {191.f / 256, 1}, {220.f / 256, 0},
                1 - .7f / radiusPerWidth, 1 + .53f / radiusPerWidth, white, 4);

        if (!status.waiting && additive)
        {
            draw->AddCallback(Additive, nullptr);
            // Retail core: quarter-arc pieces, .15-arc advance, alternating V.
            float clock = status.time;
            bool flip = false;
            for (float low = -.01f; low < m.level; low += .15f, clock *= .92f, flip = !flip)
            {
                const float high = std::min(low + .25f, m.level);
                arc.Ring(crash_art::Get(Frame("BoostFireCore", clock, 30)), low, high,
                    {0, flip ? 0.f : 1.f}, {(high - low) * 4, flip ? 1.f : 0.f},
                    inner, outer, IM_COL32(255, 255, 0, 255), 2);
            }
            clock = status.time;
            const float taper = std::pow(std::max(0.f, m.level), .1f);
            for (float low = -.02f; low < m.level + .02f; low += .275f, clock *= .92f)
            {
                const float high = std::min(low + .35f, m.level + .02f);
                const auto fire = crash_art::Get(Frame("BoostFireEdge", clock, 41));
                auto r = [](float level, float a, float b) { return 1 - std::lerp(a, b,
                    std::clamp((level + .02f) / 1.04f, 0.f, 1.f)) / radiusPerWidth; };
                const float end = (high + .02f) / 1.04f;
                const auto far = [&](float level) { return std::lerp(r(level, .3f, 0), r(level, 2.5f, .6f), taper); };
                // Each edge strip has a separate angular skew at its far
                // edge. Retail stretches the full U range even when clipped.
                arc.Strip(fire, low, high, {0, 0}, {1, 1}, far(low), r(low, .3f, 0),
                    far(high), r(high, .3f, 0), IM_COL32(255, 255, 0, 255), 2, std::lerp(.075f, -.05f, end));
                arc.Strip(fire, low, high, {0, 1}, {1, 0}, r(low, -.4f, .2f), r(low, -.7f, -3),
                    r(high, -.4f, .2f), r(high, -.7f, -3), IM_COL32(255, 255, 0, 255), 2, 0, .15f * end);
            }
            // The blue layer uses the native rising/falling height formulas.
            const float blue = m.phase != game::crash_launch::Phase::Ascending
                ? std::max(0.f, 3 * m.level - 2) : std::min(1.f, 3 * m.level);
            if (blue > 1.f / 65536)
                arc.Ring(crash_art::Get("BoostEarnFlame"), -.02f, -.02f + .42f * blue,
                    {0, 1}, {1, 0}, 1 - .75f / radiusPerWidth, 1 + .75f / radiusPerWidth, white, 5);
            draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        }
        // Sprite at the right edge of CrashBar: moving flame front and saved markers.
        if (!status.waiting)
            arc.Ring(bar, m.level - .01914f, m.level + .01914f, {245.5f / 256, 1}, {1, 0},
                1 - .9f / radiusPerWidth, outer, white, 1);
        if (m.upperStopped)
            arc.Ring(bar, m.upper - .03828f, m.upper + .03828f, {245.5f / 256, 1}, {1, 0},
                1 + .7f / radiusPerWidth, 1 - .7f / radiusPerWidth, white, 1);
        const bool done = m.phase == game::crash_launch::Phase::Done;
        if (done && m.result != game::crash_launch::Result::Blown && m.result != game::crash_launch::Result::Missed)
            arc.Ring(bar, m.level - .03828f, m.level + .03828f, {245.5f / 256, 1}, {1, 0},
                1 + .7f / radiusPerWidth, 1 - .7f / radiusPerWidth, white, 1);
        // Solo supplies its native prompt; network Crash starts a countdown.
        if (status.waiting && !status.showStartPrompt) return;
        if (done && status.nativeMessage) return;
        auto* font = burnout::RowFont();
        const bool originalFont = locale::Current() == locale::Language::English && crash_art::LoadFont();
        auto width = [&](const char* text, float height) {
            return originalFont ? crash_art::TextWidth(text, height) : font->CalcTextSizeA(height, 10000, 0, text).x;
        };
        auto outlined = [&](const char* text, ImVec2 p, float size, ImU32 color) {
            const unsigned alpha = unsigned(status.alpha * 255);
            auto textAt = [&](ImVec2 at, ImU32 tint) {
                if (originalFont) crash_art::Text(draw, text, at, size, tint);
                else draw->AddText(font, size, at, tint, text);
            };
            for (int y = -1; y <= 1; ++y)
                for (int x = -1; x <= 1; ++x)
                    if (x || y) textAt({p.x + x * scale, p.y + y * scale}, IM_COL32(9, 8, 7, alpha));
            textAt(p, (color & 0xFFFFFFu) | (alpha << 24));
        };
        if (!done)
        {
            const float fontSize = (originalFont ? 31 : 23) * scale;
            const std::string prompt = locale::Text(status.waiting ? "PRESS {} TO START" :
                m.upperStopped ? "PRESS {} TO SET CONTROL" : "PRESS {} TO SET SPEED");
            const size_t button = prompt.find("{}");
            if (button == std::string::npos) return;
            const std::string prefix = prompt.substr(0, button), suffix = prompt.substr(button + 2);
            const float a = width(prefix.c_str(), fontSize);
            const bool keyboard = hid::PromptGlyphs() == hid::Glyphs::Keyboard;
            auto code = settings::GetInt(settings::Id::KeyA);
            if (!code) code = settings::GetInt(settings::Id::KeyA2);
            const char* key = code ? SDL_GetScancodeName(SDL_Scancode(code)) : "UNBOUND";
            const float glyph = keyboard ? width(key, fontSize) + 10 * scale : 25 * scale;
            const float total = a + glyph + width(suffix.c_str(), fontSize);
            const ImVec2 at{viewport->WorkPos.x + (size.x - total) * .5f, top + (status.waiting || m.upperStopped ? 321 : 76) * scale};
            outlined(prefix.c_str(), at, fontSize, white);
            if (keyboard) outlined(key, {at.x + a + 5 * scale, at.y}, fontSize, white);
            else burnout::ButtonGlyph(draw, {at.x + a, at.y}, 25 * scale, "A");
            outlined(suffix.c_str(), {at.x + a + glyph, at.y}, fontSize, white);
        }
        else
        {
            using game::crash_launch::Result;
            const char* title = m.result == Result::Power ? "POWER BOOST!" : m.result == Result::Fast ? "FAST START!" :
                m.result == Result::Slow ? "SLOW START!" : m.result == Result::Stall ? "STALLED!" :
                m.result == Result::Blown ? "ENGINE BLOWN!" : "SLOW START!";
            font = burnout::DisplayFont();
            const float fontSize = (originalFont ? 48 : 38) * scale;
            const float textWidth = width(title, fontSize);
            const ImVec2 at{viewport->WorkPos.x + (size.x - textWidth) * .5f + 22 * scale, top + 72 * scale};
            outlined(title, at, fontSize, IM_COL32(255, 214, 90, 255));
            // Retail launch messages select AgressivePos (texture index 1),
            // rotated clockwise by 45 degrees to form the collision diamond.
            const auto icon = crash_art::Get("AgressivePos");
            const ImVec2 center{at.x - 34 * scale, at.y + 22 * scale};
            const float diagonal = 30 * std::numbers::sqrt2_v<float> * scale;
            draw->AddImageQuad(icon.texture, {center.x, center.y - diagonal},
                {center.x + diagonal, center.y}, {center.x, center.y + diagonal},
                {center.x - diagonal, center.y}, icon.low, {icon.high.x, icon.low.y},
                icon.high, {icon.low.x, icon.high.y}, IM_COL32(255, 255, 255, unsigned(status.alpha * 255)));
        }
    }
}
#endif
