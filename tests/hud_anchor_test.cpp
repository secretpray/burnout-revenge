// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// The race HUD at an ultrawide's edges (game/hud_anchor.h): pieces drawn in a
// race (Road Rage, 18.10, logged by their batch flushes) and where they go.
#include <game/hud_anchor.h>

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace game::hud;

int main()
{
    // The shift keeps the margin: a piece at x in the frame lands at
    // 640 + (x + shift - 640) * s after the narrowing, the screen showing
    // frame x 640 +- 640 at 640 +- 640 * s... i.e. x * s from its left edge.
    {
        const float s = 0.744f, shift = EdgeShift(s);
        const float x = 60.0f, landed = 640.0f + (x - shift - 640.0f) * s;
        assert(std::fabs(landed - x * s) < 1e-3f);
        assert(EdgeShift(1.0f) == 0.0f);
    }
    // One frame of the Road Rage HUD, twice (the second frame sees the first).
    const Box left[] = { { -1, 61, 128, 127 }, { 127, 63, 256, 129 }, { 60, 71, 179, 91 }, { 188, 76, 230, 117 } };
    const Box middle[] = { { 448, 26, 576, 86 }, { 704, 26, 832, 86 }, { 576, 26, 704, 86 }, { 555, 44, 726, 66 }, { 546, 85, 746, 134 },
        { 374, 271, 906, 297 } };
    const Box right[] = { { 1152, 61, 1281, 127 }, { 1024, 63, 1153, 129 }, { 1041, 71, 1145, 91 }, { 1147, 73, 1220, 114 },
        { 1072, 547, 1229, 559 }, { 1062, 607, 1168, 649 } };
    // The crash camera's tyre-track strip: five tiles of 256 across the frame.
    const Box strip[] = { { 0, 609, 256, 641 }, { 256, 609, 512, 641 }, { 512, 609, 768, 641 }, { 768, 609, 1024, 641 }, { 1024, 609, 1280, 641 } };
    // CRASHBREAKER: its icon (769-882) left of the middle line's right, the
    // text past it: one element, on the right.
    const Box crashbreaker[] = { { 769, 615, 882, 699 }, { 813, 647, 1148, 678 } };

    Anchor anchor;
    double t = 0.0;
    for (int frame = 0; frame < 3; frame++, t += 1.0 / 60.0)
    {
        // The first frame has no earlier pieces: a plate of the title drawn
        // before the middle one is on its own (one frame as the HUD appears).
        for (const Box& b : left)
            assert(anchor.Decide(b, t) == Place::Left);
        for (const Box& b : middle)
            assert(anchor.Decide(b, t) == Place::Middle || frame == 0);
        for (const Box& b : right)
            assert(anchor.Decide(b, t) == Place::Right);
        for (const Box& b : crashbreaker)
            assert(frame == 0 || anchor.Decide(b, t) == Place::Right);
        for (int i = 0; i < 5; i++)
        {
            const Place p = anchor.Decide(strip[i], t);
            // From the second frame the row is known: every tile is widened.
            assert(frame == 0 ? (i < 4 ? p != Place::Row : p == Place::Row) : p == Place::Row);
        }
    }
    // A piece seen long ago no longer joins an element.
    Anchor later;
    assert(later.Decide({ 500, 300, 700, 320 }, 0.0) == Place::Middle);
    assert(later.Decide({ 690, 300, 760, 320 }, 5.0) == Place::Right);
    // The menus' frame: its twelve tiles, and nothing else of the menus.
    {
        const Box tiles[] = { { -1, -1, 127, 127 }, { -1, 127, 127, 384 }, { 127, -1, 639, 127 }, { 1152, -1, 1280, 127 },
            { 1152, 127, 1280, 384 }, { 639, -1, 1152, 127 }, { -1, 593, 127, 722 }, { -1, 335, 127, 593 }, { 127, 593, 639, 722 },
            { 1152, 593, 1280, 722 }, { 1152, 335, 1280, 593 }, { 639, 593, 1152, 722 } };
        for (const Box& b : tiles)
            assert(IsMenuFrameTile(b));
        const Box others[] = { { 639, 561, 1151, 624 }, { -128, -104, 128, 150 }, { 0, -53, 381, 21 }, { 447, 477, 823, 524 }, { 127, 127, 639, 384 } };
        for (const Box& b : others)
            assert(!IsMenuFrameTile(b));
        // The rows' ends, widened about the middle, meet the moved columns.
        const float s = 0.744f, shift = EdgeShift(s), k = MenuFrameStretch(s);
        assert(std::fabs(639.5f + (127.0f - 639.5f) * k - (127.0f - shift)) < 1e-2f);
        assert(std::fabs(639.5f + (1152.0f - 639.5f) * k - (1152.0f + shift)) < 1e-2f);
    }
    puts("hud_anchor_test: the race HUD's elements, middle and rows, at an ultrawide's edges");
}
