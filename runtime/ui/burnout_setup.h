// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <imgui.h>
namespace ui::burnout
{
    void LoadArt();
    void ReleaseArt(); // after renderer shutdown, before destroying ImGui
    void Backdrop();
    void Poster(ImVec2 origin, ImVec2 size);
    bool Button(const char* label, ImVec2 size, bool primary = false);
    void Heading(const char* text, float size = 34);
    void Eyebrow(const char* text);
}
