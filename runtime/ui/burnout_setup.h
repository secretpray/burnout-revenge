// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <imgui.h>
namespace ui::burnout
{
    void LoadFonts();  // ui::Initialize, after the UI font (the default stays first)
    void LoadArt();
    void ReleaseArt(); // after renderer shutdown, before destroying ImGui
    // BR_REDUCED_MOTION=1, or macOS's Reduce motion: nothing slides or drifts.
    bool ReducedMotion();
    // `racing`: the hazard stripes drift (while the game installs).
    void Backdrop(bool racing = false);
    void Poster(ImVec2 origin, ImVec2 size);
    bool Button(const char* label, ImVec2 size, bool primary = false);
    void Heading(const char* text, float size = 48);
    void Eyebrow(const char* text);
    // A big yellow figure (the install's percentage), in the headings' face.
    void Figure(const char* text, float size = 64);
    // A small text button (the music's switch): yellow when lit, no box.
    bool Link(const char* label);
    // 01 GAME / 02 SETUP / 03 INSTALL, with `current` (1..3) lit and the
    // ones before it done.
    void Steps(int current);
    // A progress bar filled with moving hazard stripes; `fraction` < 0: busy, no amount.
    void HazardBar(float fraction, float height);
    // A page's blocks come in one after another: block `index` of a page
    // shown at `pageStarted` (ImGui::GetTime()). Pair with EndReveal.
    void BeginReveal(double pageStarted, int index);
    void EndReveal();
}
