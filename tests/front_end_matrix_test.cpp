// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// The front end's matrix after the window changes shape (game/
// front_end_matrix.h): the "EVENT HANDLER" label a player saw above the
// boot PSA after pressing the green button mid-movie (2026-10-12). A model
// of the game's pointer-keyed cache in sub_82453768 and of the front-end
// hook, then the renderer's front-end recognition (the matrix the hook left
// last) and its band clip, on the label's own numbers.
//
// Build by hand: c++ -std=c++20 -O2 -I runtime tests/front_end_matrix_test.cpp
#include <game/front_end_matrix.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <initializer_list>

using game::front_end::MatrixTracker;

namespace
{
    int s_failures = 0;

#define CHECK(cond, ...)                                              \
    do                                                                \
    {                                                                 \
        if (!(cond))                                                  \
        {                                                             \
            s_failures++;                                             \
            fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); \
            fprintf(stderr, __VA_ARGS__);                             \
            fprintf(stderr, "\n");                                    \
        }                                                             \
    } while (0)

    // The front-end builder's matrix for a 16:9 frame (sub_82440D80's
    // widescreen case): x 0.92 x 0.75, y 0.92.
    constexpr float kBaseX = 0.92f * 0.75f, kBaseY = 0.92f;
    // Its three pointers: the view entry, the builder's stack copy (the same
    // address every frame), the object.
    constexpr uint32_t kView = 0x82C3F590, kStack = 0x7000FF60, kObject = 0x40012340;
    // The "EVENT HANDLER" label, 13 quads parked above the frame: clip y
    // 1.039..1.092 under the 16:9 matrix (c5 = 0.92), x 0.298..0.647
    // (BURNOUT_LOG_FRONTEND / a diagnostic log of its draw, 2026-10-12).
    constexpr float kLabelLo = 1.039f / kBaseY, kLabelHi = 1.092f / kBaseY;  // front-end units

    // sub_82453768: returns at once when called with the same view, matrix
    // and object pointers as last time (its cache at 0x82A2D190, +4, +8).
    struct GuestSetMatrix
    {
        uint32_t view = 0, matrix = 0, object = 0;
        float x = 0.0f, y = 0.0f;  // what the front end's draws get (c0, c5)
        int uploads = 0;
        void Call(uint32_t v, uint32_t m, uint32_t o, float mx, float my)
        {
            const bool same = v == view && m == matrix && o == object;
            view = v;
            matrix = m;
            object = o;
            if (same)
                return;
            x = mx;
            y = my;
            uploads++;
        }
    };

    // The game's front end for one frame: the builder, the hook (narrowing
    // by the view scales; with `tracker`, clearing the cached matrix pointer
    // when the matrix changed, as game/ultrawide.cpp does), the upload. With
    // `otherView`, a 3D view went through the cache before it (menus, races).
    struct Game
    {
        GuestSetMatrix set;
        MatrixTracker tracker;
        bool resend = true;
        float hookX = 0.0f, hookY = 0.0f;  // game::FrontEndScaleX/Y
        void Frame(float scaleX, float scaleY, bool otherView = false)
        {
            if (otherView)
                set.Call(0x82C3F710, 0x7000FE00, kObject, 1.0f, 1.0f);
            hookX = kBaseX * scaleX;
            hookY = kBaseY * scaleY;
            if (resend && tracker.Changed(hookX, hookY))
                set.matrix = 0;
            set.Call(kView, kStack, kObject, hookX, hookY);
        }
    };

    // The renderer (gpu/renderer.cpp): a front-end draw is recognised when
    // its c0 and c5 are the matrix the hook left last; then under Vert+ a
    // primitive wholly outside the band (+-scaleY) is clipped away.
    struct Outcome
    {
        bool matched;
        float labelLo, labelHi;  // the label's clip y
        bool labelShows;         // on screen: inside clip +-1 and not clipped
    };
    Outcome Render(const Game& g, float scaleY)
    {
        Outcome o{};
        o.matched = std::fabs(g.set.x - g.hookX) <= g.hookX * 1e-5f && std::fabs(g.set.y - g.hookY) <= g.hookY * 1e-5f;
        o.labelLo = kLabelLo * g.set.y;
        o.labelHi = kLabelHi * g.set.y;
        const bool outsideBand = o.labelLo > scaleY;
        const bool clipped = o.matched && scaleY < 1.0f && outsideBand;
        o.labelShows = !clipped && o.labelLo < 1.0f;
        return o;
    }

    void Tracker()
    {
        MatrixTracker t;
        CHECK(t.Changed(kBaseX, kBaseY), "the first matrix is sent");
        CHECK(!t.Changed(kBaseX, kBaseY), "the same one again is not");
        CHECK(t.Changed(kBaseX, kBaseY * 0.9f), "a new y scale is");
        CHECK(!t.Changed(kBaseX, kBaseY * 0.9f), "and only once");
        CHECK(t.Changed(kBaseX * 0.744f, kBaseY * 0.9f), "a new x scale is");
        CHECK(t.Changed(kBaseX, kBaseY), "back to 16:9 is");
    }

    // The reported case: the PSA starts in a 1512x921-point window (Vert+
    // 0.9235), the green button takes it to the 3024x1900 fullscreen Space
    // (0.8953) mid-movie; nothing else uses the cache during the movies.
    void GreenButtonMidMovie()
    {
        for (bool resend : { true, false })
        {
            Game g;
            g.resend = resend;
            for (int f = 0; f < 30; f++)
                g.Frame(1.0f, 0.9235f);
            Outcome before = Render(g, 0.9235f);
            CHECK(before.matched && !before.labelShows, "%s, before the change: matched %d, label at %.3f..%.3f shows %d",
                resend ? "fixed" : "as v0.1.3", before.matched, before.labelLo, before.labelHi, before.labelShows);
            for (int f = 0; f < 30; f++)
                g.Frame(1.0f, 0.8953f);
            Outcome after = Render(g, 0.8953f);
            if (resend)
            {
                CHECK(std::fabs(g.set.y - kBaseY * 0.8953f) < 1e-6f, "the new matrix is uploaded: c5 %.4f", g.set.y);
                CHECK(after.matched, "the front end's draws are recognised again");
                CHECK(!after.labelShows, "the label stays hidden: clip y %.3f..%.3f, clipped above the band 0.8953", after.labelLo,
                    after.labelHi);
                CHECK(g.set.uploads == 2, "one upload per matrix: %d", g.set.uploads);
            }
            else
            {
                // The model reproduces the report: the old matrix stays, the
                // draws aren't recognised, and the label lands in the screen's
                // top rows (clip 0.959..1.009, its top cut off).
                CHECK(std::fabs(g.set.y - kBaseY * 0.9235f) < 1e-6f, "as v0.1.3 the old matrix stays: c5 %.4f", g.set.y);
                CHECK(!after.matched && after.labelShows && after.labelLo > 0.95f && after.labelLo < 0.97f,
                    "as v0.1.3 the label shows at clip %.3f..%.3f", after.labelLo, after.labelHi);
            }
        }
    }

    // Menus and races: a 3D view goes through the cache every frame, so the
    // front end's call is never a repeat and a new matrix arrives either way.
    void MenusRefreshAnyway()
    {
        for (bool resend : { true, false })
        {
            Game g;
            g.resend = resend;
            for (int f = 0; f < 10; f++)
                g.Frame(1.0f, 0.9235f, true);
            for (int f = 0; f < 10; f++)
                g.Frame(1.0f, 0.8953f, true);
            CHECK(Render(g, 0.8953f).matched, "%s: menus get the new matrix", resend ? "fixed" : "as v0.1.3");
        }
    }

    // A session that never changes shape: the front end's matrix is sent
    // once, as before (the cache still skips every repeat).
    void NoChangeNoResend()
    {
        Game g;
        for (int f = 0; f < 600; f++)
            g.Frame(1.0f, 0.8953f);
        CHECK(g.set.uploads == 1, "one upload in 600 frames: %d", g.set.uploads);
        Game wide;
        for (int f = 0; f < 600; f++)
            wide.Frame(0.744f, 1.0f);
        CHECK(wide.set.uploads == 1 && Render(wide, 1.0f).matched, "Hor+ the same: %d", wide.set.uploads);
    }

    // The other way: from a tall window to a 16:9 one mid-movie (Vert+ off,
    // so nothing is clipped): the old narrowed matrix would put the label at
    // clip 0.930..0.978, on a 16:9 screen; the new one puts it past the
    // frame's top, where a 360 has it.
    void TallTo169()
    {
        for (bool resend : { true, false })
        {
            Game g;
            g.resend = resend;
            for (int f = 0; f < 30; f++)
                g.Frame(1.0f, 0.8953f);
            for (int f = 0; f < 30; f++)
                g.Frame(1.0f, 1.0f);
            Outcome o = Render(g, 1.0f);
            if (resend)
                CHECK(!o.labelShows && o.labelLo > 1.0f, "fixed: the label past the frame (clip %.3f)", o.labelLo);
            else
                CHECK(o.labelShows, "as v0.1.3 the label shows on the 16:9 screen (clip %.3f..%.3f)", o.labelLo, o.labelHi);
        }
    }

    // Ultrawide to 16:9 mid-movie: the x scale alone changes (Hor+), and the
    // hook's x-only overload (a ppc/ from before Vert+) passes y 0.
    void HorPlusChange()
    {
        Game g;
        for (int f = 0; f < 10; f++)
            g.Frame(0.744f, 1.0f);
        for (int f = 0; f < 10; f++)
            g.Frame(1.0f, 1.0f);
        CHECK(std::fabs(g.set.x - kBaseX) < 1e-6f && Render(g, 1.0f).matched, "the 16:9 matrix is uploaded: c0 %.4f", g.set.x);
        MatrixTracker xOnly;
        CHECK(xOnly.Changed(kBaseX * 0.744f, 0.0f) && !xOnly.Changed(kBaseX * 0.744f, 0.0f) && xOnly.Changed(kBaseX, 0.0f),
            "the x-only hook's changes");
    }
}

int main()
{
    Tracker();
    GreenButtonMidMovie();
    MenusRefreshAnyway();
    NoChangeNoResend();
    TallTo169();
    HorPlusChange();
    if (s_failures)
    {
        fprintf(stderr, "%d failure(s)\n", s_failures);
        return 1;
    }
    printf("front_end_matrix_test: all passed\n");
    return 0;
}
