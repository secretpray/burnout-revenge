// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The sun's glare on a screen of another shape than 16:9 (game/glare.cpp
// hooks it; tests/glare_fit_test.cpp checks this header alone).
//
// The glare is a screen-space sprite, not a view's geometry: the sun code
// (sub_82225730) projects the sun through the view, turns the result into
// pixels of the 1280x720 frame (view 0's target), and its quad builder
// (sub_822251C8) makes a rotated square of a fixed size in those pixels
// around that point: v0 = (cx - a, cy - b), v1 = (cx + b, cy - a),
// v2 = (cx + a, cy + b), v3 = (cx - b, cy + a), a = R (cos + sin),
// b = R (cos - sin). A pixel-to-clip matrix of its own (not the front
// end's) draws it. Under Hor+ and Vert+ the presenter stretches the frame by
// 1/scaleX in x or 1/scaleY in y (video::PlacePicture), so a square of frame
// pixels came out 1/scaleX wide or 1/scaleY tall: 1.33x taller at 4:3,
// 1.11x on a 16:10 Steam Deck, 1.34x wider at 3440x1440. The view's own
// geometry doesn't, because its projection was narrowed by the same scale
// (game/ultrawide.cpp).
//
// The fix scales the glow's corners about their centre by the view scales,
// x by scaleX and y by scaleY (one of them is 1), after the game built them:
// the stretch then gives back the square the game meant, with its angle, at
// the size it has against the 16:9 band (the world's, the HUD's). The centre,
// the sun's projected position, is already right. Only the glow is changed,
// and only in the views the projection hook widens: the sun's occlusion
// quad (the same builder, called from sub_82225438) keeps its 32x32 pixels,
// so the share of it that is visible is still counted against the game's
// expected 2048 samples; and in the environment cube's faces (views 18-23,
// square targets sampled by direction, never stretched) the glow stays as
// the game draws it.
#pragma once
#include <cstdint>

namespace game::glare
{
    // Return addresses of sub_822251C8's two calls (ctx.lr at entry): the
    // glow, from the sun code sub_82225730, and the occlusion quad, from
    // sub_82225438.
    constexpr uint32_t kGlowCall = 0x82225A40;
    constexpr uint32_t kOcclusionQuadCall = 0x822254B0;

    // The views game/ultrawide.cpp widens (the projection hook's set): the
    // player's camera (1), 2, the road reflection (4, 5) and 17. The rest
    // (the shadow maps 13-16, the environment cube's faces 18-23) keep the
    // 16:9 projection.
    inline bool WidenedViewId(uint32_t id)
    {
        return id == 1 || id == 2 || id == 4 || id == 5 || id == 17;
    }

    // The sprite's four corners (frame pixels), scaled about their centre:
    // x by scaleX, y by scaleY. The centre is the mean of the corners (the
    // builder's quad is symmetric about it).
    inline void FitSprite(float x[4], float y[4], float scaleX, float scaleY)
    {
        const float cx = (x[0] + x[1] + x[2] + x[3]) * 0.25f, cy = (y[0] + y[1] + y[2] + y[3]) * 0.25f;
        for (int i = 0; i < 4; i++)
        {
            x[i] = cx + (x[i] - cx) * scaleX;
            y[i] = cy + (y[i] - cy) * scaleY;
        }
    }
}
