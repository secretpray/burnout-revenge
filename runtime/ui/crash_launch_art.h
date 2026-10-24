// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <imgui.h>
#include <string_view>
namespace ui::crash_art
{
    struct Sprite { ImTextureRef texture; ImVec2 low{}, high{}; };
    bool Load();
    Sprite Get(std::string_view name);
    bool LoadFont();
    float TextWidth(std::string_view text, float height);
    void Text(ImDrawList* draw, std::string_view text, ImVec2 at, float height, ImU32 color);
    void Release();
}
