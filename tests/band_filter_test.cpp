// Tests for the post-processing's band split: video::BandOf
// (runtime/video/picture_fit.cpp) and video::band (runtime/video/
// band_filter.h, which video/presenter.cpp's GLSL follows line for line).
// From the repo root:
//   clang++ -std=c++20 -O2 -UNDEBUG -Iruntime tests/band_filter_test.cpp runtime/video/picture_fit.cpp -o build/band_filter_test
//   build/band_filter_test
// What it checks:
//   - BandOf's rows and columns are those whose centres lie inside the band
//     (a pixel-centre oracle), and with the edge-bar fills' rows (gpu::
//     bars::BlackoutRects of a frame as big as the band) they split the
//     frame with no gap and no overlap, at every internal resolution (a fuzz
//     over screen shapes too);
//   - the scaling pass, modelled along the axis that is split (bilinear,
//     Catmull-Rom with its 2x2 clamp and sharpening, and the 7-fetch
//     variant), on a frame whose strips are black and whose band is white:
//     every screen row whose centre maps into a strip is exactly black and
//     every one in the band exactly white (the career intro's movie
//     letterbox bled ~5% of the band into the strip's last row before);
//   - every fetch the anti-aliasing pass makes for a pixel reads only its
//     own span's texels;
//   - FSR 1, where the picture is enlarged along the split axis: each of
//     EASU's taps for a screen pixel (texels floor(t - 0.5) - 1 to + 2 round
//     its centre t) reads only that pixel's span, and each neighbour RCAS
//     reads (the pixel's own picture, one pixel either way) lies in the same
//     span, so the black strips stay black and the band's edge rows equal its
//     middle ones.
// The same checks run with planted faults (no split, a band edge one texel
// off either way, clamping to the span's edges instead of its texel
// centres, an unclamped gather or EASU tap, RCAS reading across the edge):
// each must fail.
#include <gpu/edge_bars.h>
#include <video/band_filter.h>
#include <video/picture_fit.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iterator>
#include <random>
#include <vector>

static int g_failures = 0;
static bool g_print = true;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; if (g_print) { printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } } while (0)

using video::band::Span;

// How a pixel's span is found and a tap kept in it: the code under test, or
// a planted fault.
struct Model
{
    const char* name;
    // The band's [lo, hi) as the presenter passes it, from BandOf's.
    std::function<void(int32_t& lo, int32_t& hi, int32_t size)> band = [](int32_t&, int32_t&, int32_t) {};
    std::function<Span(float t, int32_t lo, int32_t hi, int32_t size)> spanOf = video::band::SpanOf;
    std::function<float(float t, Span s)> clamp = video::band::Clamp;
    std::function<int32_t(int32_t i, Span s)> clampTexel = video::band::ClampTexel;
    std::function<int32_t(int32_t i, int32_t j, int32_t lo, int32_t hi, int32_t outSize, int32_t srcSize)> rcasNeighbour =
        video::band::RcasNeighbour;
};

// One axis of the frame image: texel values, the sampler's clamp to edge.
struct Line
{
    std::vector<float> v;
    float Texel(int32_t i) const
    {
        return v[size_t(std::clamp(i, 0, int32_t(v.size()) - 1))];
    }
    // A bilinear fetch at coordinate t (texel units).
    float At(float t) const
    {
        const float x = t - 0.5f, f = x - std::floor(x);
        const int32_t i = int32_t(std::floor(x));
        return Texel(i) * (1.0f - f) + Texel(i + 1) * f;
    }
};

// The scaling pass at frame coordinate pos (a screen pixel's centre), one
// axis. kind 0 bilinear, 1 Catmull-Rom (kScaleFs), 2 the 7-fetch variant
// (kScaleFastFs; along one axis its gather is the 2 texels round pos).
static float Scale(const Line& line, float pos, int kind, float sharpen, const Model& m, int32_t lo, int32_t hi)
{
    const int32_t size = int32_t(line.v.size());
    const Span s = m.spanOf(pos, lo, hi, size);
    auto at = [&](float t) { return line.At(m.clamp(t, s)); };
    if (kind == 0)
        return at(pos);
    const float c = std::floor(pos - 0.5f) + 0.5f, f = pos - c;
    const float w0 = f * (-0.5f + f * (1.0f - 0.5f * f)), w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    const float w2 = f * (0.5f + f * (2.0f - 1.5f * f)), w3 = f * f * (-0.5f + 0.5f * f), w12 = w1 + w2;
    if (kind == 1)
    {
        const float cr = at(c - 1.0f) * w0 + at(c + w2 / w12) * w12 + at(c + 2.0f) * w3;
        const float a = at(c), b = at(c + 1.0f);
        return std::clamp(cr + sharpen * (cr - at(pos)), std::min(a, b), std::max(a, b));
    }
    const int32_t first = int32_t(c - 0.5f);
    const float a = line.Texel(m.clampTexel(first, s)), d = line.Texel(m.clampTexel(first + 1, s));
    const float middle = a + (d - a) * (w2 / w12), bilinear = a + (d - a) * f;
    const float cr = (at(c - 1.0f) * w0 + middle * w12 + at(c + 2.0f) * w3) / (w0 + w12 + w3);
    return std::clamp(cr + sharpen * (cr - bilinear), std::min(a, d), std::max(a, d));
}

struct Shape
{
    uint32_t w, h;
};

// Screens: Vert+ (16:10, 3:2, 4:3, iPad, 5:4 letterboxed to 4:3), Hor+
// (ultrawides, a phone, 32:9).
constexpr Shape kScreens[] = { { 1280, 800 }, { 1920, 1200 }, { 2560, 1600 }, { 1200, 800 }, { 2160, 1440 }, { 1024, 768 },
    { 2732, 2048 }, { 1280, 1024 }, { 2560, 1080 }, { 3440, 1440 }, { 2796, 1290 }, { 3840, 1080 } };
// Internal resolutions: 1x, 2x2, 3x2, 1x2, 3x3.
constexpr Shape kImages[] = { { 1280, 720 }, { 2560, 1440 }, { 3840, 1440 }, { 1280, 1440 }, { 3840, 2160 } };

// One screen, one image size: the scaling pass along the split axis on a
// black-strips, white-band frame. Returns the failures it added.
static int ScaleCase(const Model& m, const Shape& screen, const Shape& image, int kind)
{
    const int before = g_failures;
    const video::PictureFit fit = video::FitPicture(screen.w, screen.h, 1280.0f, 720.0f, true, true);
    const video::FrameBand band = video::BandOf(image.w, image.h, fit.scaleX, fit.scaleY);
    const bool tall = fit.scaleY < 1.0f;
    const int32_t size = int32_t(tall ? image.h : image.w);
    int32_t lo = tall ? band.y0 : band.x0, hi = tall ? band.y1 : band.x1;
    const int32_t trueLo = lo, trueHi = hi;
    m.band(lo, hi, size);
    Line line;
    line.v.resize(size_t(size));
    for (int32_t i = 0; i < size; i++)
        line.v[size_t(i)] = i >= trueLo && i < trueHi ? 1.0f : 0.0f;
    // The picture's pixels along that axis (the presenter's viewport).
    const int32_t pixels = tall ? fit.height : fit.width;
    int strip = 0, inBand = 0;
    for (int32_t r = 0; r < pixels; r++)
    {
        const float pos = (float(r) + 0.5f) / float(pixels) * float(size);
        const float v = Scale(line, pos, kind, 0.3f, m, lo, hi);
        const bool want = pos >= float(trueLo) && pos < float(trueHi);
        if (want)
        {
            inBand++;
            CHECK(std::fabs(v - 1.0f) <= 1e-5f, "%ux%u, image %ux%u, filter %d: band pixel %d (frame %.3f) reads %.5f, want 1", screen.w, screen.h,
                image.w, image.h, kind, r, pos, v);
        }
        else
        {
            strip++;
            CHECK(std::fabs(v) <= 1e-5f, "%ux%u, image %ux%u, filter %d: strip pixel %d (frame %.3f) reads %.5f, want 0 (bleed)", screen.w,
                screen.h, image.w, image.h, kind, r, pos, v);
        }
    }
    CHECK(strip > 0 && inBand > 0, "%ux%u: %d strip and %d band pixels", screen.w, screen.h, strip, inBand);
    return g_failures - before;
}

// Every fetch the anti-aliasing pass can make for pixel i (its neighbours,
// its edge walk up to 24 texels away, half-texel steps, the final sample's
// offset) reads only texels of i's own span. Returns the failures it added.
static int AaCase(const Model& m, int32_t size, int32_t lo0, int32_t hi0)
{
    const int before = g_failures;
    int32_t lo = lo0, hi = hi0;
    m.band(lo, hi, size);
    for (int32_t i = 0; i < size; i++)
    {
        const Span own = video::band::SpanOf(float(i) + 0.5f, lo0, hi0, size);  // where the pixel truly is
        const Span s = m.spanOf(float(i) + 0.5f, lo, hi, size);
        for (float d = -24.0f; d <= 24.0f; d += 0.25f)
        {
            const float t = m.clamp(float(i) + 0.5f + d, s) - 0.5f;
            const int32_t a = int32_t(std::floor(t));
            const float f = t - float(a);
            const bool okA = std::clamp(a, 0, size - 1) >= own.a && std::clamp(a, 0, size - 1) < own.b;
            const bool okB = f == 0.0f || (std::clamp(a + 1, 0, size - 1) >= own.a && std::clamp(a + 1, 0, size - 1) < own.b);
            CHECK(okA && okB, "size %d band [%d,%d): pixel %d's fetch at %+.2f reads texel %d%s outside its span [%d,%d)", size, lo0, hi0, i, d,
                okA ? a + 1 : a, okA ? "" : " (or its neighbour)", own.a, own.b);
            if (g_failures - before > 8)
                return g_failures - before;
        }
    }
    return g_failures - before;
}

// FSR 1 along the split axis of one screen and image size, where the
// picture is enlarged that way (else bicubic draws it: skipped). EASU's taps
// for each screen pixel, and the screen pixels RCAS reads for it, against
// where that pixel truly is. Returns the failures it added.
static int FsrCase(const Model& m, const Shape& screen, const Shape& image)
{
    const int before = g_failures;
    const video::PictureFit fit = video::FitPicture(screen.w, screen.h, 1280.0f, 720.0f, true, true);
    const video::FrameBand band = video::BandOf(image.w, image.h, fit.scaleX, fit.scaleY);
    const bool tall = fit.scaleY < 1.0f;
    const int32_t size = int32_t(tall ? image.h : image.w), pixels = tall ? fit.height : fit.width;
    if (pixels <= size)
        return 0;
    const int32_t trueLo = tall ? band.y0 : band.x0, trueHi = tall ? band.y1 : band.x1;
    int32_t lo = trueLo, hi = trueHi;
    m.band(lo, hi, size);
    auto own = [&](int32_t r) { return video::band::SpanOf(video::band::SourceOf(r, pixels, size), trueLo, trueHi, size); };
    for (int32_t r = 0; r < pixels && g_failures - before <= 8; r++)
    {
        const float t = video::band::SourceOf(r, pixels, size);
        const Span s = m.spanOf(t, lo, hi, size), truth = own(r);
        const int32_t fp = int32_t(std::floor(t - 0.5f));
        for (int32_t k = -1; k <= 2; k++)
        {
            const int32_t texel = std::clamp(m.clampTexel(fp + k, s), 0, size - 1);  // and the sampler's clamp to edge
            CHECK(texel >= truth.a && texel < truth.b, "%ux%u, image %ux%u: EASU tap %+d of screen pixel %d (frame %.3f) reads texel %d outside its span [%d,%d)",
                screen.w, screen.h, image.w, image.h, k, r, t, texel, truth.a, truth.b);
        }
        for (int32_t d : { -1, 1 })
        {
            const int32_t read = m.rcasNeighbour(r, r + d, lo, hi, pixels, size);
            const Span rs = own(read);
            CHECK(rs.a == truth.a && rs.b == truth.b, "%ux%u, image %ux%u: RCAS's neighbour %+d of screen pixel %d reads pixel %d, in [%d,%d) not [%d,%d)",
                screen.w, screen.h, image.w, image.h, d, r, read, rs.a, rs.b, truth.a, truth.b);
        }
    }
    return g_failures - before;
}

static int AllCases(const Model& m)
{
    int failed = 0;
    for (const Shape& s : kScreens)
        for (const Shape& im : kImages)
        {
            for (int kind = 0; kind < 3; kind++)
                failed += ScaleCase(m, s, im, kind) ? 1 : 0;
            failed += FsrCase(m, s, im) ? 1 : 0;
        }
    // The anti-aliasing at the frame's resolution: 16:10, 4:3, 3:2 rows and
    // ultrawide columns at 1x and 2x.
    for (const Shape& s : { Shape{ 1280, 800 }, Shape{ 1024, 768 }, Shape{ 1200, 800 }, Shape{ 2560, 1080 } })
        for (const Shape& im : { kImages[0], kImages[1] })
        {
            const video::PictureFit fit = video::FitPicture(s.w, s.h, 1280.0f, 720.0f, true, true);
            const video::FrameBand b = video::BandOf(im.w, im.h, fit.scaleX, fit.scaleY);
            failed += fit.scaleY < 1.0f ? (AaCase(m, int32_t(im.h), b.y0, b.y1) ? 1 : 0) : (AaCase(m, int32_t(im.w), b.x0, b.x1) ? 1 : 0);
        }
    return failed;
}

// BandOf against the pixel-centre oracle, and against the edge-bar fills:
// with the fill of a black frame as big as the band (gpu::bars::
// BlackoutRects, through a full-frame front-end viewport: NDC y = -clip y)
// every row is in exactly one of the band and the fill. Columns likewise
// under Hor+.
static void BandRowsCase(uint32_t w, uint32_t h, float sx, float sy)
{
    const video::FrameBand b = video::BandOf(w, h, sx, sy);
    for (int axis = 0; axis < 2; axis++)
    {
        const float scale = axis ? sy : sx;
        const int32_t size = int32_t(axis ? h : w), lo = axis ? b.y0 : b.x0, hi = axis ? b.y1 : b.x1;
        if (scale >= 1.0f)
        {
            CHECK(lo == 0 && hi == size, "%ux%u scale %.4f: axis %d band [%d,%d), want the whole %d", w, h, scale, axis, lo, hi, size);
            continue;
        }
        int wrong = 0;
        for (int32_t i = 0; i < size; i++)
        {
            const float ndc = (float(i) + 0.5f) / float(size) * 2.0f - 1.0f;
            if (std::fabs(std::fabs(ndc) - scale) * float(size) * 0.5f < 1.0f / 256.0f)
                continue;  // a tie, within the rasteriser's snap
            wrong += (std::fabs(ndc) < scale) != (i >= lo && i < hi);
        }
        CHECK(wrong == 0, "%ux%u scale %.4f: %d %s differ from the pixel-centre oracle", w, h, scale, wrong, axis ? "rows" : "columns");
    }
    const float ps[4] = { 1.0f, -1.0f, 0.0f, 0.0f };
    // The fill of a black frame as big as the band itself: everything past
    // the band (the rows above and below it, or the columns beside it).
    const gpu::bars::Box band{ -sx, sx, -sy, sy };
    gpu::bars::Rect r[4]{};
    const uint32_t n = gpu::bars::BlackoutRects(band, ps, float(w), float(h), { 0, 0, int32_t(w), int32_t(h) }, r);
    int wrongPx = 0;
    for (int32_t y = 0; y < int32_t(h); y += (y % 97 == 0 ? 1 : 7))
        for (int32_t x = 0; x < int32_t(w); x += 13)
        {
            int in = (x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1) ? 1 : 0;
            for (uint32_t i = 0; i < n; i++)
                in += (x >= r[i].x0 && x < r[i].x1 && y >= r[i].y0 && y < r[i].y1) ? 1 : 0;
            wrongPx += in != 1;
        }
    // Every row and column at the edges themselves.
    for (int32_t y : { b.y0 - 1, b.y0, b.y1 - 1, b.y1 })
        for (int32_t x : { b.x0 - 1, b.x0, b.x1 - 1, b.x1, int32_t(w) / 2 })
        {
            if (x < 0 || y < 0 || x >= int32_t(w) || y >= int32_t(h))
                continue;
            int in = (x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1) ? 1 : 0;
            for (uint32_t i = 0; i < n; i++)
                in += (x >= r[i].x0 && x < r[i].x1 && y >= r[i].y0 && y < r[i].y1) ? 1 : 0;
            wrongPx += in != 1;
        }
    CHECK(wrongPx == 0, "%ux%u scale x %.4f y %.4f: %d pixel(s) in both or neither of the band and the black frame's fill", w, h, sx, sy, wrongPx);
}

int main()
{
    printf("BandOf: the band's rows and columns, and with the edge-bar fills a partition of the frame:\n");
    for (const Shape& s : kScreens)
    {
        const video::PictureFit fit = video::FitPicture(s.w, s.h, 1280.0f, 720.0f, true, true);
        for (const Shape& im : kImages)
            BandRowsCase(im.w, im.h, fit.scaleX, fit.scaleY);
        const video::FrameBand b = video::BandOf(1280, 720, fit.scaleX, fit.scaleY);
        printf("  %ux%u: view scale x %.4f y %.4f, 1x frame band x [%d,%d) y [%d,%d)\n", s.w, s.h, fit.scaleX, fit.scaleY, b.x0, b.x1, b.y0, b.y1);
    }
    {
        // 16:9 and the letterboxed shapes: the whole frame, nothing split.
        for (const Shape& s : { Shape{ 1280, 720 }, Shape{ 1920, 1080 }, Shape{ 1366, 768 }, Shape{ 1360, 768 } })
        {
            const video::PictureFit fit = video::FitPicture(s.w, s.h, 1280.0f, 720.0f, true, true);
            const video::FrameBand b = video::BandOf(1280, 720, fit.scaleX, fit.scaleY);
            CHECK(b.x0 == 0 && b.x1 == 1280 && b.y0 == 0 && b.y1 == 720, "%ux%u: band x [%d,%d) y [%d,%d), want the whole frame", s.w, s.h, b.x0,
                b.x1, b.y0, b.y1);
        }
        // A fuzz: random view scales (one below 1) and image sizes.
        std::mt19937 rng(12345);
        std::uniform_real_distribution<float> scale(0.5f, 0.999f);
        std::uniform_int_distribution<int> size(200, 4000), coin(0, 1);
        for (int i = 0; i < 2000; i++)
        {
            const float sc = scale(rng);
            const bool tall = coin(rng);
            BandRowsCase(uint32_t(size(rng)), uint32_t(size(rng)), tall ? 1.0f : sc, tall ? sc : 1.0f);
        }
        printf("  fuzz: 2000 random scales and image sizes\n");
    }
    const int bandFailures = g_failures;

    const Model fixed{ "band_filter.h" };
    const int failed = AllCases(fixed);
    int fsrCases = 0;
    for (const Shape& s : kScreens)
        for (const Shape& im : kImages)
        {
            const video::PictureFit fit = video::FitPicture(s.w, s.h, 1280.0f, 720.0f, true, true);
            fsrCases += (fit.scaleY < 1.0f ? fit.height > int32_t(im.h) : fit.width > int32_t(im.w)) ? 1 : 0;
        }
    CHECK(fsrCases >= 8, "only %d FSR 1 cases enlarge along the split axis", fsrCases);
    printf("scaling (3 filters + FSR 1 where enlarged, %d cases, x %zu screens x %zu images) and anti-aliasing fetches: %d case(s) failed\n",
        fsrCases, std::size(kScreens), std::size(kImages), failed);

    printf("planted faults (each must fail):\n");
    const Model planted[] = {
        { "no split (v0.1.1)", [](int32_t& lo, int32_t& hi, int32_t size) { lo = 0, hi = size; } },
        { "band one texel early", [](int32_t& lo, int32_t& hi, int32_t) { lo -= 1, hi -= 1; } },
        { "band one texel late", [](int32_t& lo, int32_t& hi, int32_t) { lo += 1, hi += 1; } },
        { "band one texel wider", [](int32_t& lo, int32_t& hi, int32_t) { lo -= 1, hi += 1; } },
        { "clamp to the span's edges", [](int32_t&, int32_t&, int32_t) {}, video::band::SpanOf,
            [](float t, Span s) { return std::clamp(t, float(s.a), float(s.b)); } },
        { "no clamp of the gather", [](int32_t&, int32_t&, int32_t) {}, video::band::SpanOf, video::band::Clamp,
            [](int32_t i, Span) { return i; } },
        { "RCAS reads across the edge", [](int32_t&, int32_t&, int32_t) {}, video::band::SpanOf, video::band::Clamp, video::band::ClampTexel,
            [](int32_t, int32_t j, int32_t, int32_t, int32_t outSize, int32_t) { return std::clamp(j, 0, outSize - 1); } },
    };
    for (const Model& m : planted)
    {
        const int saved = g_failures;
        g_print = false;
        const int n = AllCases(m);
        g_print = true;
        g_failures = saved;
        printf("  %-28s %d case(s) failed\n", m.name, n);
        CHECK(n > 0, "planted \"%s\" passed every case", m.name);
    }
    if (g_failures)
    {
        printf("%d check(s) FAILED (%d in the band rows)\n", g_failures, bandFailures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
