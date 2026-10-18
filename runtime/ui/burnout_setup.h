// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <imgui.h>
namespace ui::burnout
{
    void LoadFonts();  // ui::Initialize, after the UI font (the default stays first)
    // The headings' and the labels' faces (the default font until LoadFonts).
    ImFont* DisplayFont();
    ImFont* LabelFont();
    ImFont* RowFont();  // upright, as the game's option rows

    // A controller button as the game draws it in its hints: a coloured disc
    // with its letter ("A", "B", "X", "Y"), or a dark pill for "LB", "RB",
    // "D-PAD". Drawn at `at` (top left), `height` tall; returns its width
    // (only that with no `draw`).
    float ButtonGlyph(ImDrawList* draw, ImVec2 at, float height, const char* button);
    // Two rows of yellow dashes on dark, from a.x to b.x at heights a.y and b.y.
    void HazardDashes(ImDrawList* draw, ImVec2 a, ImVec2 b);
    // The game's chosen row: a yellow line above and below (a.y, b.y),
    // fading out at both ends.
    void SelectorLines(ImDrawList* draw, ImVec2 a, ImVec2 b);
    // The thin dark rule under one of the game's other rows, at y.
    void RowRule(ImDrawList* draw, float x0, float x1, float y);
    // A screen's heading band: dark, slanted, with a strip of yellow
    // chevrons running into the heading as on the game's screens (still for
    // Reduce motion), from `a` to `b`. Returns where the heading's text starts.
    float TitleBand(ImDrawList* draw, ImVec2 a, ImVec2 b);
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
