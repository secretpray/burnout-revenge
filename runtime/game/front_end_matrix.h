// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The front end's matrix and the game's cache of it, after the window
// changes shape (game/ultrawide.cpp's UltrawideFrontEnd).
//
// The front-end builder (sub_82440D80) makes the HUD and menu matrix on its
// stack (x 0.92 x 0.75, y 0.92 for the 16:9 frame; the hook narrows them by
// the view scales) and hands it to sub_82453768 with the view entry
// (0x82C3F590) and an object. That function keeps the three pointers of its
// last call at 0x82A2D190 (view), +4 (matrix) and +8 (object) and returns
// at once when a call brings the same three: on the 360 the front end's
// matrix never changes, so the same address means the same matrix. In menus
// and races the game's other views go through it every frame, so the front
// end's call never repeats the last one; through the boot movies (the EA
// logos, the PSA, the attract movie) nothing else does. So when the window
// changed shape during them (the green button mid-movie), the front end kept
// the matrix narrowed for the old shape: the renderer, which recognises
// front-end draws by the matrix the hook made last, left them alone, and what
// the game parks just above the 16:9 frame (an "EVENT HANDLER" label at
// 1.04..1.09 of the frame's height, right of centre) showed in the rows
// Vert+ adds above it.
//
// The fix: when the hook leaves a matrix other than the one it left last, it
// clears the cache's matrix pointer (a real pointer is never 0), so the call
// right after it uploads the new matrix. Once per change; a session that
// never changes shape uploads exactly as before, past the first frames.
//
// Header only (tests/front_end_matrix_test.cpp).
#pragma once
#include <cstdint>

namespace game::front_end
{
    // sub_82453768's cache of its last call: view, matrix, object pointers.
    constexpr uint32_t kMatrixCache = 0x82A2D190;
    constexpr uint32_t kMatrixCachePointer = kMatrixCache + 4;

    // The matrix scales the front-end hook left last (its thread only).
    class MatrixTracker
    {
    public:
        // Whether the matrix (x, y scales) differs from the one left last,
        // and the first time: the game must upload it again.
        bool Changed(float x, float y)
        {
            const bool changed = !m_seen || x != m_x || y != m_y;
            m_seen = true;
            m_x = x;
            m_y = y;
            return changed;
        }

    private:
        bool m_seen = false;
        float m_x = 0.0f, m_y = 0.0f;
    };
}
