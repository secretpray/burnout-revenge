// Tests for game::glare (runtime/game/glare.h): the sun's glow, a square of
// frame pixels the game builds around the sun, fitted to the view scales so
// that on a screen of any shape (video::FitPicture's picture) it is still a
// square, at the game's angle, in the same place, and as large against the
// 16:9 band as at 16:9. From the repo root:
//   clang++ -std=c++20 -O2 -UNDEBUG -Iruntime tests/glare_fit_test.cpp runtime/video/picture_fit.cpp -o build/glare_fit_test
//   build/glare_fit_test
// The shape checks run again with planted wrong fits (none, the inverse
// scale, the axes swapped, about the frame's corner instead of the centre):
// each must fail at least one case.
#include <game/glare.h>
#include <video/picture_fit.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iterator>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; if (g_print) { printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } } while (0)
static bool g_print = true;

using Fit = std::function<void(float x[4], float y[4], float scaleX, float scaleY)>;

// The glow's quad as sub_822251C8 builds it: centre (cx, cy), "radius" r
// (its size times the table's factor), angle t: a = r (cos + sin),
// b = r (cos - sin); corners (cx - a, cy - b), (cx + b, cy - a),
// (cx + a, cy + b), (cx - b, cy + a). A square, its first
// side at angle t, 2r long.
static void Build(float cx, float cy, float r, float t, float x[4], float y[4])
{
    const float s = std::sin(t), c = std::cos(t), a = r * (c + s), b = r * (c - s);
    const float px[4] = { cx - a, cx + b, cx + a, cx - b }, py[4] = { cy - b, cy - a, cy + b, cy + a };
    for (int i = 0; i < 4; i++)
        x[i] = px[i], y[i] = py[i];
}

struct Screen
{
    uint32_t w, h;
    const char* name;
};

constexpr Screen kScreens[] = {
    { 1280, 720, "16:9 1280x720" },
    { 1920, 1080, "16:9 1920x1080" },
    { 1366, 768, "1366x768 (within 1% of 16:9: letterboxed)" },
    { 1280, 800, "Steam Deck 16:10" },
    { 1200, 800, "3:2" },
    { 1024, 768, "4:3" },
    { 2732, 2048, "iPad Pro" },
    { 1280, 1024, "5:4 (a 4:3 picture, letterboxed)" },
    { 2560, 1080, "ultrawide 2560x1080" },
    { 3440, 1440, "ultrawide 3440x1440" },
    { 2796, 1290, "iPhone 15 Pro Max" },
    { 3840, 1080, "32:9" },
};

// One glow on one screen: fitted by `fit`, then stretched as the presenter
// stretches the frame (x * picture width / 1280, y * picture height / 720).
// Returns the failures it added.
static int Case(const Fit& fit, const Screen& s, float cx, float cy, float r, float t)
{
    const int before = g_failures;
    const video::PictureFit p = video::FitPicture(s.w, s.h, 1280.0f, 720.0f, true, true);
    float x[4], y[4];
    Build(cx, cy, r, t, x, y);
    fit(x, y, p.scaleX, p.scaleY);
    const float kx = float(p.width) / 1280.0f, ky = float(p.height) / 720.0f;
    float X[4], Y[4];
    for (int i = 0; i < 4; i++)
        X[i] = x[i] * kx, Y[i] = y[i] * ky;
    // A square on the screen: four equal sides, equal diagonals.
    float side[4];
    for (int i = 0; i < 4; i++)
        side[i] = std::hypot(X[(i + 1) & 3] - X[i], Y[(i + 1) & 3] - Y[i]);
    const float d0 = std::hypot(X[2] - X[0], Y[2] - Y[0]), d1 = std::hypot(X[3] - X[1], Y[3] - Y[1]);
    const float tol = 1e-3f * side[0];
    for (int i = 1; i < 4; i++)
        CHECK(std::fabs(side[i] - side[0]) <= tol, "%s, angle %.2f: side %d is %.3f px, side 0 %.3f px (not a square)", s.name, t, i, side[i], side[0]);
    CHECK(std::fabs(d0 - d1) <= tol, "%s, angle %.2f: diagonals %.3f and %.3f px (not a square)", s.name, t, d0, d1);
    // Its height over its width (the v0.1.1 report's measure): 1.
    const float w = std::fmax(std::fmax(X[0], X[1]), std::fmax(X[2], X[3])) - std::fmin(std::fmin(X[0], X[1]), std::fmin(X[2], X[3]));
    const float h = std::fmax(std::fmax(Y[0], Y[1]), std::fmax(Y[2], Y[3])) - std::fmin(std::fmin(Y[0], Y[1]), std::fmin(Y[2], Y[3]));
    CHECK(std::fabs(h / w - 1.0f) <= 1e-3f, "%s, angle %.2f: h/w %.4f", s.name, t, h / w);
    // At the game's angle.
    const float angle = std::atan2(Y[1] - Y[0], X[1] - X[0]), want = std::atan2(-(r * (std::cos(t) + std::sin(t))) + r * (std::cos(t) - std::sin(t)),
        r * (std::cos(t) - std::sin(t)) + r * (std::cos(t) + std::sin(t)));
    CHECK(std::fabs(std::remainder(angle - want, 6.28318531f)) <= 1e-3f, "%s, angle %.2f: first side at %.4f rad, want %.4f", s.name, t, angle, want);
    // Where the game put it: the sun's projected point.
    const float mx = (X[0] + X[1] + X[2] + X[3]) * 0.25f, my = (Y[0] + Y[1] + Y[2] + Y[3]) * 0.25f;
    CHECK(std::fabs(mx - cx * kx) <= 1e-3f && std::fabs(my - cy * ky) <= 1e-3f, "%s: centre (%.3f, %.3f), want (%.3f, %.3f)", s.name, mx, my, cx * kx, cy * ky);
    // As large against the 16:9 band as on a 16:9 screen: the band is the
    // picture's width under Vert+ (or letterboxed), its height x 16/9 under
    // Hor+; a side of 2r frame pixels (|v1 - v0| = sqrt(2 a^2 + 2 b^2)) of
    // 1280.
    const float bandW = std::fmin(float(p.width), float(p.height) * 16.0f / 9.0f);
    const float wantSide = 2.0f * r * bandW / 1280.0f;
    CHECK(std::fabs(side[0] - wantSide) <= 1e-3f * wantSide, "%s: side %.3f px, want %.3f (the 16:9 band's share)", s.name, side[0], wantSide);
    return g_failures - before;
}

static int AllCases(const Fit& fit)
{
    int failed = 0;
    for (const Screen& s : kScreens)
        for (float t : { 0.0f, 0.3f, 0.785398f, 1.2f, 2.5f })
            failed += Case(fit, s, 431.0f, 118.0f, 37.5f, t) ? 1 : 0;
    return failed;
}

int main()
{
    printf("The glow, fitted (game::glare::FitSprite), on %zu screens x 5 angles:\n", std::size(kScreens));
    for (const Screen& s : kScreens)
    {
        const video::PictureFit p = video::FitPicture(s.w, s.h, 1280.0f, 720.0f, true, true);
        printf("  %-44s picture %dx%d, view scale x %.4f y %.4f\n", s.name, p.width, p.height, p.scaleX, p.scaleY);
    }
    const int fitted = AllCases(game::glare::FitSprite);
    printf("fitted: %d case(s) failed\n", fitted);

    // On 16:9 (and on a screen the presenter letterboxes) the fit is the
    // identity: the corners come back bit for bit.
    {
        float x[4], y[4], x0[4], y0[4];
        Build(640.25f, 77.5f, 41.0f, 0.7f, x, y);
        for (int i = 0; i < 4; i++)
            x0[i] = x[i], y0[i] = y[i];
        game::glare::FitSprite(x, y, 1.0f, 1.0f);
        bool same = true;
        for (int i = 0; i < 4; i++)
            same = same && x[i] == x0[i] && y[i] == y0[i];
        CHECK(same, "the fit at scale 1, 1 moved a corner");
    }

    // The view set the projection hook widens, and the ones it doesn't.
    for (uint32_t id : { 1u, 2u, 4u, 5u, 17u })
        CHECK(game::glare::WidenedViewId(id), "view %u must be widened", id);
    for (uint32_t id : { 0u, 3u, 6u, 13u, 14u, 15u, 16u, 18u, 19u, 20u, 21u, 22u, 23u })
        CHECK(!game::glare::WidenedViewId(id), "view %u must not be widened", id);
    CHECK(game::glare::kGlowCall != game::glare::kOcclusionQuadCall, "the two call sites");

    // Planted: wrong fits must each fail.
    printf("planted wrong fits (each must fail):\n");
    struct Planted
    {
        const char* name;
        Fit fit;
    };
    const Planted planted[] = {
        { "no fit (v0.1.1)", [](float*, float*, float, float) {} },
        { "1/scale (stretches it twice)", [](float* x, float* y, float sx, float sy) { game::glare::FitSprite(x, y, 1.0f / sx, 1.0f / sy); } },
        { "axes swapped", [](float* x, float* y, float sx, float sy) { game::glare::FitSprite(x, y, sy, sx); } },
        { "y only (no Hor+)", [](float* x, float* y, float, float sy) { game::glare::FitSprite(x, y, 1.0f, sy); } },
        { "x only (no Vert+)", [](float* x, float* y, float sx, float) { game::glare::FitSprite(x, y, sx, 1.0f); } },
        { "about the frame's corner", [](float* x, float* y, float sx, float sy) {
              for (int i = 0; i < 4; i++)
                  x[i] *= sx, y[i] *= sy;
          } },
        { "about the first corner", [](float* x, float* y, float sx, float sy) {
              const float ox = x[0], oy = y[0];
              for (int i = 0; i < 4; i++)
                  x[i] = ox + (x[i] - ox) * sx, y[i] = oy + (y[i] - oy) * sy;
          } },
    };
    for (const Planted& p : planted)
    {
        const int saved = g_failures;
        g_print = false;
        const int failed = AllCases(p.fit);
        g_print = true;
        g_failures = saved;
        printf("  %-32s %d case(s) failed\n", p.name, failed);
        CHECK(failed > 0, "planted \"%s\" passed every case", p.name);
    }

    if (g_failures)
    {
        printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
