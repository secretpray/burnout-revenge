// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// The game's button prompts for the device in use; see pad_glyphs.h.
#include <stdafx.h>
#include "pad_glyphs.h"
#include <hid/input.h>
#include <kernel/write_watch.h>
#include <user/key_names.h>
#include <user/settings.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

namespace gpu::padglyphs
{
    namespace
    {
        const unsigned char kLabelFont[] = {
#include "barlow_Bold.inc"
        };

        // Each prompt's texels in the sheet (inclusive), as the game drew them.
        struct Cell { int x0, y0, x1, y1; };
        // kStick: a thumbstick, the game printing LS or RS over it.
        enum Slot { kA, kB, kX, kY, kLB, kRB, kLT, kRT, kStart, kStick, kDpad, kSlots };
        constexpr Cell kCells[kSlots] = {
            { 2, 3, 31, 32 }, { 48, 2, 77, 31 }, { 94, 2, 123, 31 }, { 2, 36, 31, 65 },
            { 6, 68, 56, 106 }, { 68, 68, 118, 106 }, { 6, 109, 57, 148 }, { 68, 109, 118, 148 },
            { 8, 152, 58, 190 }, { 73, 151, 111, 189 }, { 12, 196, 49, 233 },
        };

        enum class Family { Xbox, PlayStation, Keyboard };

        // Settings > Input > Button Prompts (lock-free; BURNOUT_PAD_GLYPHS
        // overrides it as the settings' environment variables do).
        Family Wanted()
        {
            switch (settings::ButtonPrompts(settings::GetInt(settings::Id::ButtonPrompts)))
            {
            case settings::ButtonPrompts::Xbox: return Family::Xbox;
            case settings::ButtonPrompts::PlayStation: return Family::PlayStation;
            case settings::ButtonPrompts::Keyboard: return Family::Keyboard;
            case settings::ButtonPrompts::Auto: break;
            }
            switch (hid::PublishedGlyphs())
            {
            case hid::Glyphs::PlayStation: return Family::PlayStation;
            case hid::Glyphs::Keyboard: return Family::Keyboard;
            default: return Family::Xbox;
            }
        }

        uint64_t Hash(const uint8_t* p, size_t n)
        {
            uint64_t h = 0xCBF29CE484222325ull;
            for (size_t i = 0; i < n; i++)
                h = (h ^ p[i]) * 0x100000001B3ull;
            return h;
        }

        // Xenia's 2D tiling (as the untile shader's tiled2D): the offset of
        // block (x, y) of 1 << log2 bytes, `pitch` blocks a row (32-aligned).
        uint32_t Tiled(uint32_t x, uint32_t y, uint32_t pitch, uint32_t log2)
        {
            uint32_t outer = (((y >> 5) * (pitch >> 5)) + (x >> 5)) << 6;
            uint32_t inner = (((y >> 1) & 7) << 3) | (x & 7);
            uint32_t oi = (outer | inner) << log2;
            uint32_t bank = (y >> 4) & 1, pipe = ((x >> 3) & 3) ^ (((y >> 3) & 1) << 1);
            return ((y & 1) << 4) | (pipe << 6) | (bank << 11) | (oi & 0xF) | (((oi >> 4) & 1) << 5) |
                (((oi >> 5) & 7) << 8) | ((oi >> 8) << 12);
        }

        struct Rgba { float r, g, b, a; };

        // A texture as straight-alpha texels, 0..1.
        struct Image
        {
            int width, height;
            std::vector<Rgba> px;
            Image(int w, int h) : width(w), height(h), px(size_t(w) * h) {}
            Rgba& at(int x, int y) { return px[size_t(y) * width + x]; }

            void Clear(const Cell& c)
            {
                for (int y = c.y0; y <= c.y1; y++)
                    for (int x = c.x0; x <= c.x1; x++)
                        at(x, y) = {};
            }

            // `color` over the texels of `c`, by coverage(x, y) of each texel centre.
            void Paint(const Cell& c, Rgba color, const std::function<float(float, float)>& coverage)
            {
                for (int y = c.y0; y <= c.y1; y++)
                    for (int x = c.x0; x <= c.x1; x++)
                        Over(x, y, color, coverage(x + .5f, y + .5f));
            }

            void Over(int x, int y, Rgba color, float coverage)
            {
                float a = std::clamp(coverage, 0.0f, 1.0f) * color.a;
                if (a <= 0 || x < 0 || y < 0 || x >= width || y >= height)
                    return;
                Rgba& d = at(x, y);
                float out = a + d.a * (1 - a);
                auto mix = [&](float s, float t) { return (s * a + t * d.a * (1 - a)) / out; };
                d = { mix(color.r, d.r), mix(color.g, d.g), mix(color.b, d.b), out };
            }
        };

        Rgba Col(int r, int g, int b, int a = 255) { return { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f }; }
        float Edge(float sd) { return std::clamp(.5f - sd, 0.0f, 1.0f); }  // coverage of a texel at signed distance sd

        float SdCircle(float x, float y, float cx, float cy, float r) { return std::hypot(x - cx, y - cy) - r; }
        float SdSegment(float x, float y, float ax, float ay, float bx, float by)
        {
            float px = x - ax, py = y - ay, dx = bx - ax, dy = by - ay;
            float t = std::clamp((px * dx + py * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
            return std::hypot(px - dx * t, py - dy * t);
        }
        float SdRoundBox(float x, float y, float cx, float cy, float hw, float hh, float r)
        {
            float qx = std::abs(x - cx) - hw + r, qy = std::abs(y - cy) - hh + r;
            return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
        }
        float SdTriangle(float x, float y, const float (&p)[6])
        {
            // Inside: all edges on one side; distance: the nearest edge.
            float d = 1e9f;
            bool inside = true;
            for (int i = 0; i < 3; i++)
            {
                float ax = p[i * 2], ay = p[i * 2 + 1], bx = p[(i * 2 + 2) % 6], by = p[(i * 2 + 3) % 6];
                d = std::min(d, SdSegment(x, y, ax, ay, bx, by));
                inside = inside && (bx - ax) * (y - ay) - (by - ay) * (x - ax) >= 0;
            }
            return inside ? -d : d;
        }

        // Text in Barlow Condensed Bold, its caps `capHeight` texels tall
        // (narrowed to fit `maxWidth`), centred on (cx, cy).
        void Text(Image& image, const char* text, float cx, float cy, float capHeight, float maxWidth, Rgba color)
        {
            static stbtt_fontinfo font;
            static bool ready = stbtt_InitFont(&font, kLabelFont, stbtt_GetFontOffsetForIndex(kLabelFont, 0)) != 0;
            if (!ready)
                return;
            int x0, y0, x1, y1;
            stbtt_GetCodepointBox(&font, 'H', &x0, &y0, &x1, &y1);
            float scale = capHeight / float(y1 - y0);
            auto width = [&](float s) {
                float w = 0;
                for (const char* c = text; *c; c++)
                {
                    int advance, lsb;
                    stbtt_GetCodepointHMetrics(&font, *c, &advance, &lsb);
                    w += advance * s + (c[1] ? stbtt_GetCodepointKernAdvance(&font, c[0], c[1]) * s : 0);
                }
                return w;
            };
            if (float w = width(scale); w > maxWidth)
                scale *= maxWidth / w;
            float x = cx - width(scale) * .5f, baseline = cy + (y1 - y0) * scale * .5f;
            for (const char* c = text; *c; c++)
            {
                float fx = x - std::floor(x), fy = baseline - std::floor(baseline);
                int bx0, by0, bx1, by1;
                stbtt_GetCodepointBitmapBoxSubpixel(&font, *c, scale, scale, fx, fy, &bx0, &by0, &bx1, &by1);
                int w = bx1 - bx0, h = by1 - by0;
                if (w > 0 && h > 0)
                {
                    std::vector<unsigned char> bits(size_t(w) * h);
                    stbtt_MakeCodepointBitmapSubpixel(&font, bits.data(), w, h, w, scale, scale, fx, fy, *c);
                    for (int j = 0; j < h; j++)
                        for (int i = 0; i < w; i++)
                            image.Over(int(std::floor(x)) + bx0 + i, int(std::floor(baseline)) + by0 + j, color, bits[size_t(j) * w + i] / 255.0f);
                }
                int advance, lsb;
                stbtt_GetCodepointHMetrics(&font, *c, &advance, &lsb);
                x += advance * scale + (c[1] ? stbtt_GetCodepointKernAdvance(&font, c[0], c[1]) * scale : 0);
            }
        }

        const Rgba kOutline = Col(0, 0, 0, 240);

        // A disc for a face button: the game's black rim, a dark face.
        // (`s`: the scale of the rims, 1 in the sheet's 30-texel cells.)
        void Disc(Image& image, const Cell& c, float s = 1)
        {
            float cx = (c.x0 + c.x1 + 1) * .5f, cy = (c.y0 + c.y1 + 1) * .5f, r = (c.x1 - c.x0 + 1) * .5f;
            image.Paint(c, kOutline, [&](float x, float y) { return Edge(SdCircle(x, y, cx, cy, r - .3f)); });
            image.Paint(c, Col(34, 34, 38), [&](float x, float y) { return Edge(SdCircle(x, y, cx, cy, r - 2.0f * s)); });
            image.Paint(c, Col(255, 255, 255, 60), [&](float x, float y) { return Edge(std::abs(SdCircle(x, y, cx, cy, r - 2.6f * s)) - .5f * s); });
        }

        // A rounded key or button: rim, fill, a light edge.
        void Cap(Image& image, const Cell& c, float round, Rgba fill, int edgeAlpha, float s = 1)
        {
            float cx = (c.x0 + c.x1 + 1) * .5f, cy = (c.y0 + c.y1 + 1) * .5f;
            float hw = (c.x1 - c.x0 + 1) * .5f, hh = (c.y1 - c.y0 + 1) * .5f, rim = 2 * s, edge = 2.6f * s;
            image.Paint(c, kOutline, [&](float x, float y) { return Edge(SdRoundBox(x, y, cx, cy, hw - .3f, hh - .3f, round)); });
            image.Paint(c, fill, [&](float x, float y) { return Edge(SdRoundBox(x, y, cx, cy, hw - rim, hh - rim, std::max(round - rim, 1.0f))); });
            image.Paint(c, Col(255, 255, 255, edgeAlpha),
                [&](float x, float y) { return Edge(std::abs(SdRoundBox(x, y, cx, cy, hw - edge, hh - edge, std::max(round - edge, 1.0f))) - .5f * s); });
        }

        // A PlayStation face button in the place of the Xbox one `slot` (A:
        // cross, B: circle, X: square, Y: triangle) on a dark disc filling `c`.
        void FaceShape(Image& image, const Cell& c, Slot slot)
        {
            image.Clear(c);
            float s = (c.x1 - c.x0 + 1) / 30.0f;
            Disc(image, c, s);
            float cx = (c.x0 + c.x1 + 1) * .5f, cy = (c.y0 + c.y1 + 1) * .5f, k = 6.4f * s, line = 2.4f * s;
            std::function<float(float, float)> shape;
            Rgba color;
            if (slot == kA)
            {
                color = Col(124, 178, 232);
                shape = [=](float x, float y) {
                    return Edge(std::min(SdSegment(x, y, cx - k, cy - k, cx + k, cy + k), SdSegment(x, y, cx - k, cy + k, cx + k, cy - k)) - line * .5f);
                };
            }
            else if (slot == kB)
            {
                color = Col(232, 84, 92);
                shape = [=](float x, float y) { return Edge(std::abs(SdCircle(x, y, cx, cy, k * 1.02f)) - line * .5f); };
            }
            else if (slot == kX)
            {
                color = Col(222, 140, 200);
                shape = [=](float x, float y) { return Edge(std::abs(SdRoundBox(x, y, cx, cy, k * .88f, k * .88f, .8f)) - line * .5f); };
            }
            else
            {
                color = Col(64, 200, 170);
                const float p[6] = { cx, cy - k * 1.05f, cx + k * 1.1f, cy + k * .8f, cx - k * 1.1f, cy + k * .8f };
                shape = [=](float x, float y) { return Edge(std::abs(SdTriangle(x, y, p)) - line * .5f); };
            }
            image.Paint(c, color, shape);
        }

        // PlayStation: the four shapes in their colours on dark discs (as
        // ui/burnout_setup.cpp's ButtonGlyph), L1 R1 L2 R2, Options.
        void DrawPlayStation(Image& image)
        {
            for (Slot s : { kA, kB, kX, kY })
                FaceShape(image, kCells[s], s);
            const char* shoulders[] = { "L1", "R1", "L2", "R2" };
            for (int i = 0; i < 4; i++)
            {
                const Cell& c = kCells[kLB + i];
                image.Clear(c);
                Cap(image, c, 11, Col(40, 40, 44), 70);
                Text(image, shoulders[i], (c.x0 + c.x1 + 1) * .5f, (c.y0 + c.y1 + 1) * .5f, 17, float(c.x1 - c.x0 - 10), Col(236, 236, 236));
            }
            {
                // Options: a small pill with three lines.
                const Cell& c = kCells[kStart];
                image.Clear(c);
                Cap(image, c, 14, Col(40, 40, 44), 70);
                float cx = (c.x0 + c.x1 + 1) * .5f, cy = (c.y0 + c.y1 + 1) * .5f;
                image.Paint(c, Col(236, 236, 236), [=](float x, float y) {
                    float d = 1e9f;
                    for (float dy : { -6.0f, 0.0f, 6.0f })
                        d = std::min(d, SdSegment(x, y, cx - 8, cy + dy, cx + 8, cy + dy));
                    return Edge(d - 1.2f);
                });
            }
            // The sticks and the d-pad are the same on both pads: the game's own.
        }

        // A key cap's label: the key bound in Settings > Controls. A cell is
        // 30 texels and the game draws it at its own fixed size (two to three
        // times that at 4K), so a word does not fit: "SPACE" in 24 texels was
        // letters 4 texels tall, run together once scaled. Keys with a name
        // longer than three letters get the symbol a Mac keyboard prints
        // (Symbol below, as codes 1..11); others their name, three letters
        // at most.
        enum Sym : char { kUp = 1, kDown, kLeft, kRight, kSpace, kBackspace, kShift, kControl, kReturn, kTab, kOption };

        std::string KeyLabel(settings::Id id)
        {
            const char* token = settings::keynames::Token(uint16_t(settings::GetInt(id)));
            std::string t = token ? token : "";
            static const std::unordered_map<std::string, std::string> kShort = {
                { "up", { kUp } }, { "down", { kDown } }, { "left", { kLeft } }, { "right", { kRight } },
                { "space", { kSpace } }, { "backspace", { kBackspace } }, { "left_shift", { kShift } }, { "right_shift", { kShift } },
                { "left_ctrl", { kControl } }, { "right_ctrl", { kControl } }, { "return", { kReturn } }, { "tab", { kTab } },
                { "left_alt", { kOption } }, { "right_alt", { kOption } }, { "escape", "ESC" }, { "none", "" },
            };
            if (auto it = kShort.find(t); it != kShort.end())
                return it->second;
            std::string out;
            for (char ch : t)
                if (ch != '_')
                    out += char(std::toupper((unsigned char)ch));
            return out.substr(0, 3);
        }

        // An arrow head pointing `dir` (1 up, 2 down, 3 left, 4 right), size k.
        float Arrow(float x, float y, float cx, float cy, float k, int dir)
        {
            float p[6];
            switch (dir)
            {
            case 1: { float q[6] = { cx, cy - k, cx + k, cy + k * .6f, cx - k, cy + k * .6f }; memcpy(p, q, sizeof p); break; }
            case 2: { float q[6] = { cx, cy + k, cx - k, cy - k * .6f, cx + k, cy - k * .6f }; memcpy(p, q, sizeof p); break; }
            case 3: { float q[6] = { cx - k, cy, cx + k * .6f, cy - k, cx + k * .6f, cy + k }; memcpy(p, q, sizeof p); break; }
            default: { float q[6] = { cx + k, cy, cx - k * .6f, cy + k, cx - k * .6f, cy - k }; memcpy(p, q, sizeof p); break; }
            }
            return Edge(SdTriangle(x, y, p));
        }

        // A key symbol (Sym) in a box of half-size k centred on (cx, cy):
        // strokes of polylines, as the Mac prints them (␣ ⌫ ⇧ ⌃ ↵ ⇥ ⌥).
        void Symbol(Image& image, const Cell& c, char code, float cx, float cy, float k, Rgba color)
        {
            if (code >= kUp && code <= kRight)
            {
                image.Paint(c, color, [=](float x, float y) { return Arrow(x, y, cx, cy, k * .8f, code); });
                return;
            }
            using Line = std::vector<std::pair<float, float>>;  // in units of k from the centre
            std::vector<Line> lines;
            switch (code)
            {
            case kSpace: lines = { { { -1, -.15f }, { -1, .4f }, { 1, .4f }, { 1, -.15f } } }; break;
            case kBackspace:
                lines = { { { -1, 0 }, { -.45f, -.65f }, { 1, -.65f }, { 1, .65f }, { -.45f, .65f }, { -1, 0 } },
                    { { 0, -.28f }, { .56f, .28f } }, { { 0, .28f }, { .56f, -.28f } } };
                break;
            case kShift:
                lines = { { { 0, -1 }, { .95f, 0 }, { .42f, 0 }, { .42f, .9f }, { -.42f, .9f }, { -.42f, 0 }, { -.95f, 0 }, { 0, -1 } } };
                break;
            case kControl: lines = { { { -.8f, .4f }, { 0, -.45f }, { .8f, .4f } } }; break;
            case kReturn:
                lines = { { { .8f, -.8f }, { .8f, .3f }, { -.75f, .3f } }, { { -.35f, -.12f }, { -.8f, .3f }, { -.35f, .72f } } };
                break;
            case kTab: lines = { { { -.9f, 0 }, { .6f, 0 } }, { { .15f, -.45f }, { .6f, 0 }, { .15f, .45f } }, { { .9f, -.6f }, { .9f, .6f } } }; break;
            case kOption: lines = { { { -.95f, -.5f }, { -.4f, -.5f }, { .35f, .5f }, { .95f, .5f } }, { { .2f, -.5f }, { .95f, -.5f } } }; break;
            default: return;
            }
            const float half = std::max(.8f, k * .13f);
            image.Paint(c, color, [&](float x, float y) {
                float d = 1e9f;
                for (const Line& line : lines)
                    for (size_t i = 1; i < line.size(); i++)
                        d = std::min(d, SdSegment(x, y, cx + line[i - 1].first * k, cy + line[i - 1].second * k,
                            cx + line[i].first * k, cy + line[i].second * k));
                return Edge(d - half);
            });
        }

        // A cap with `label` (KeyLabel) its letters `capHeight` texels tall,
        // or its symbol filling `symbol` texels either side of the centre.
        void KeyCap(Image& image, const Cell& c, const std::string& label, float capHeight, float symbol, float scale = 1)
        {
            image.Clear(c);
            Cap(image, c, 5 * scale, Col(52, 50, 46), 110, scale);
            float cx = (c.x0 + c.x1 + 1) * .5f, cy = (c.y0 + c.y1 + 1) * .5f;
            if (label.size() == 1 && label[0] >= kUp && label[0] <= kOption)
                Symbol(image, c, label[0], cx, cy, symbol, Col(236, 236, 236));
            else if (!label.empty())
                Text(image, label.c_str(), cx, cy, label.size() <= 1 ? capHeight * 1.4f : capHeight, float(c.x1 - c.x0 + 1) - 8 * scale,
                    Col(236, 236, 236));
        }

        // The keyboard: key caps with the bound keys' names (the sheet has
        // no Back button).
        void DrawKeyboard(Image& image)
        {
            using settings::Id;
            const std::pair<Slot, Id> keys[] = {
                { kA, Id::KeyA }, { kB, Id::KeyB }, { kX, Id::KeyX }, { kY, Id::KeyY },
                { kLB, Id::KeyLB }, { kRB, Id::KeyRB }, { kLT, Id::KeyLeftTrigger }, { kRT, Id::KeyRightTrigger },
                { kStart, Id::KeyStart },
            };
            for (auto [slot, id] : keys)
            {
                const Cell& c = kCells[slot];
                std::string label = KeyLabel(id);
                KeyCap(image, c, label, slot <= kY ? 11.0f : 14.0f, slot <= kY ? 8.5f : 11.0f);
            }
            // A stick: a blank cap (the game prints LS or RS on it; the
            // keys of a stick are four, and its click is unbound).
            KeyCap(image, kCells[kStick], "", 0, 0);
            {
                // The d-pad: a cap with the four arrows.
                const Cell& c = kCells[kDpad];
                image.Clear(c);
                Cap(image, c, 5, Col(52, 50, 46), 110);
                float cx = (c.x0 + c.x1 + 1) * .5f, cy = (c.y0 + c.y1 + 1) * .5f;
                image.Paint(c, Col(236, 236, 236), [=](float x, float y) {
                    return std::max({ Arrow(x, y, cx, cy - 9, 4.5f, 1), Arrow(x, y, cx, cy + 9, 4.5f, 2),
                        Arrow(x, y, cx - 9, cy, 4.5f, 3), Arrow(x, y, cx + 9, cy, 4.5f, 4) });
                });
            }
        }

        // k_8_8_8_8, 8-in-32: each texel's guest bytes are A R G B.
        Image Decode8888(const uint8_t* guest, int width, int height)
        {
            Image image(width, height);
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                {
                    const uint8_t* p = guest + Tiled(x, y, width, 2);
                    image.at(x, y) = Col(p[1], p[2], p[3], p[0]);
                }
            return image;
        }

        uint8_t Byte(float v) { return uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); }

        void Encode8888(const Image& image, uint8_t* out)
        {
            for (int y = 0; y < image.height; y++)
                for (int x = 0; x < image.width; x++)
                {
                    const Rgba& c = image.px[size_t(y) * image.width + x];
                    uint8_t* p = out + Tiled(x, y, image.width, 2);
                    p[0] = Byte(c.a), p[1] = Byte(c.r), p[2] = Byte(c.g), p[3] = Byte(c.b);
                }
        }

        // DXT3 (BC2), 8-in-16: each 16-byte block's bytes swapped in pairs.
        Rgba From565(uint16_t v) { return { (v >> 11) / 31.0f, ((v >> 5) & 63) / 63.0f, (v & 31) / 31.0f, 1 }; }
        uint16_t To565(const Rgba& c)
        {
            return uint16_t(std::lround(std::clamp(c.r, 0.0f, 1.0f) * 31) << 11 | std::lround(std::clamp(c.g, 0.0f, 1.0f) * 63) << 5 |
                std::lround(std::clamp(c.b, 0.0f, 1.0f) * 31));
        }

        Image DecodeDxt3(const uint8_t* guest, int width, int height)
        {
            Image image(width, height);
            uint32_t pitch = (uint32_t(width / 4) + 31) & ~31u;
            for (int by = 0; by < height / 4; by++)
                for (int bx = 0; bx < width / 4; bx++)
                {
                    uint8_t b[16];
                    const uint8_t* p = guest + Tiled(bx, by, pitch, 4);
                    for (int i = 0; i < 16; i += 2)
                        b[i] = p[i + 1], b[i + 1] = p[i];
                    uint64_t alpha;
                    memcpy(&alpha, b, 8);
                    uint16_t c0 = uint16_t(b[8] | b[9] << 8), c1 = uint16_t(b[10] | b[11] << 8);
                    uint32_t bits = uint32_t(b[12] | b[13] << 8 | b[14] << 16 | b[15] << 24);
                    Rgba e0 = From565(c0), e1 = From565(c1);
                    for (int i = 0; i < 16; i++)
                    {
                        float t = std::array<float, 4>{ 0, 1, 1 / 3.0f, 2 / 3.0f }[(bits >> (2 * i)) & 3];
                        Rgba c{ e0.r + (e1.r - e0.r) * t, e0.g + (e1.g - e0.g) * t, e0.b + (e1.b - e0.b) * t, ((alpha >> (4 * i)) & 15) / 15.0f };
                        image.at(bx * 4 + i % 4, by * 4 + i / 4) = c;
                    }
                }
            return image;
        }

        // A plain encoder: the block's two extreme colours along its widest
        // axis (of the texels that show), each texel the nearest of the four.
        void EncodeDxt3(const Image& image, uint8_t* out)
        {
            uint32_t pitch = (uint32_t(image.width / 4) + 31) & ~31u;
            for (int by = 0; by < image.height / 4; by++)
                for (int bx = 0; bx < image.width / 4; bx++)
                {
                    Rgba t[16];
                    for (int i = 0; i < 16; i++)
                        t[i] = image.px[size_t(by * 4 + i / 4) * image.width + bx * 4 + i % 4];
                    Rgba lo{ 1, 1, 1, 0 }, hi{ 0, 0, 0, 0 };
                    bool any = false;
                    for (const Rgba& c : t)
                        if (c.a > 0.02f)
                        {
                            any = true;
                            lo = { std::min(lo.r, c.r), std::min(lo.g, c.g), std::min(lo.b, c.b), 0 };
                            hi = { std::max(hi.r, c.r), std::max(hi.g, c.g), std::max(hi.b, c.b), 0 };
                        }
                    Rgba axis{ hi.r - lo.r, hi.g - lo.g, hi.b - lo.b, 0 };
                    float minP = 1e9f, maxP = -1e9f;
                    Rgba e0{}, e1{};
                    for (const Rgba& c : t)
                        if (any && c.a > 0.02f)
                        {
                            float pr = c.r * axis.r + c.g * axis.g + c.b * axis.b;
                            if (pr < minP) minP = pr, e1 = c;
                            if (pr > maxP) maxP = pr, e0 = c;
                        }
                    uint16_t c0 = To565(e0), c1 = To565(e1);
                    Rgba q0 = From565(c0), q1 = From565(c1);
                    Rgba pal[4];
                    for (int k = 0; k < 4; k++)
                    {
                        float f = std::array<float, 4>{ 0, 1, 1 / 3.0f, 2 / 3.0f }[k];
                        pal[k] = { q0.r + (q1.r - q0.r) * f, q0.g + (q1.g - q0.g) * f, q0.b + (q1.b - q0.b) * f, 1 };
                    }
                    uint64_t alpha = 0;
                    uint32_t bits = 0;
                    for (int i = 0; i < 16; i++)
                    {
                        alpha |= uint64_t(std::lround(std::clamp(t[i].a, 0.0f, 1.0f) * 15)) << (4 * i);
                        int best = 0;
                        float bestD = 1e9f;
                        for (int k = 0; k < 4; k++)
                        {
                            float dr = t[i].r - pal[k].r, dg = t[i].g - pal[k].g, db = t[i].b - pal[k].b;
                            float d = dr * dr + dg * dg + db * db;
                            if (d < bestD) bestD = d, best = k;
                        }
                        bits |= uint32_t(best) << (2 * i);
                    }
                    uint8_t b[16];
                    memcpy(b, &alpha, 8);
                    b[8] = uint8_t(c0), b[9] = uint8_t(c0 >> 8), b[10] = uint8_t(c1), b[11] = uint8_t(c1 >> 8);
                    b[12] = uint8_t(bits), b[13] = uint8_t(bits >> 8), b[14] = uint8_t(bits >> 16), b[15] = uint8_t(bits >> 24);
                    uint8_t* p = out + Tiled(bx, by, pitch, 4);
                    for (int i = 0; i < 16; i += 2)
                        p[i] = b[i + 1], p[i + 1] = b[i];
                }
        }

        // The textures known here, as on the NTSC-U disc (FNV-1a of their
        // base level's guest bytes; tiled, 2D).
        struct Known
        {
            const char* name;
            uint32_t width, height, format, endian, pitchBlocks, size;
            uint64_t hash;
            Image (*decode)(const uint8_t*, int, int);
            void (*encode)(const Image&, uint8_t*);
            void (*draw)(Image&, Family);
        };

        void DrawSheet(Image& image, Family family)
        {
            if (family == Family::PlayStation)
                DrawPlayStation(image);
            else
                DrawKeyboard(image);
        }

        // "PRESS (A) TO BOOST!": the A disc alone, 128x128.
        void DrawBoostA(Image& image, Family family)
        {
            const Cell c{ 2, 2, 125, 125 };
            Cell all{ 0, 0, image.width - 1, image.height - 1 };
            image.Clear(all);
            if (family == Family::PlayStation)
                FaceShape(image, c, kA);
            else
            {
                KeyCap(image, Cell{ 6, 6, 121, 121 }, KeyLabel(settings::Id::KeyA), 40, 36, 3.5f);
            }
        }

        const Known kKnown[] = {
            // The prompt sheet (front end and HUD): k_8_8_8_8, 8-in-32.
            { "button sheet", 128, 256, 6, 2, 128, 128 * 256 * 4, 0x37B710FB6B3237E7ull, Decode8888, Encode8888, DrawSheet },
            // The race HUD's big A: DXT3, 8-in-16.
            { "boost prompt", 128, 128, 19, 1, 32, 128 * 128, 0xE38D9BD804031E1Bull, DecodeDxt3, EncodeDxt3, DrawBoostA },
        };

        struct Seen
        {
            const Known* known;
            std::vector<uint8_t> original;  // the guest bytes as the game loaded them
            Family applied = Family::Xbox;
            uint64_t appliedHash = 0;
            uint64_t sequence = 0;          // writewatch::WriteSequence after the last check
            uint64_t generation = 0;        // settings::Generation() when it was drawn
        };
        std::unordered_map<uint32_t, Seen> s_seen;  // by guest address
        // Other textures of a known shape, by address: the write sequence they
        // were hashed at (not hashed again until written).
        std::unordered_map<uint32_t, uint64_t> s_others;
        // The drawn pictures of each known texture and family (from the
        // first original seen), with the settings::Generation() they were
        // drawn at (a keyboard cap names the key bound).
        struct Drawing { std::vector<uint8_t> bytes; uint64_t generation = ~0ull; };
        std::unordered_map<uint64_t, Drawing> s_drawn;

        const std::vector<uint8_t>& Drawn(const Seen& seen, Family family)
        {
            if (family == Family::Xbox)
                return seen.original;
            Drawing& d = s_drawn[seen.known->hash ^ uint64_t(family)];
            if (d.bytes.empty() || (family == Family::Keyboard && d.generation != settings::Generation()))
            {
                const Known& k = *seen.known;
                d.generation = settings::Generation();
                Image image = k.decode(seen.original.data(), int(k.width), int(k.height));
                k.draw(image, family);
                d.bytes = seen.original;
                k.encode(image, d.bytes.data());
            }
            return d.bytes;
        }

        const char* Name(Family f) { return f == Family::PlayStation ? "PlayStation" : f == Family::Keyboard ? "keyboard" : "Xbox"; }

        // Draws the wanted family over `seen` (at `base`, `guest`) if it shows
        // another. Its memory must hold what it last showed.
        void Apply(Seen& seen, uint32_t base, uint8_t* guest)
        {
            Family wanted = Wanted();
            if (wanted == seen.applied && (wanted != Family::Keyboard || seen.generation == settings::Generation()))
                return;
            seen.generation = settings::Generation();
            const uint32_t n = seen.known->size;
            const std::vector<uint8_t>& bytes = Drawn(seen, wanted);
            uint64_t hash = Hash(bytes.data(), n);
            if (hash == seen.appliedHash)
            {
                seen.applied = wanted;  // (the same picture: nothing to load)
                return;
            }
            // Written as the GPU threads store: the pages count as written, so
            // the texture is untiled again from what is there now (and the
            // renderer's cached lookups of it miss).
            writewatch::GpuThreadStore(guest, bytes.data(), n);
            writewatch::MarkWritten(base, n, writewatch::Writer::kGpuStore, base);
            seen.applied = wanted;
            seen.appliedHash = hash;
            seen.sequence = writewatch::WriteSequence(base, n);
            fprintf(stderr, "[glyphs] %s at %08X now shows %s buttons\n", seen.known->name, base, Name(wanted));
        }
    }

    bool Enabled()
    {
        // BURNOUT_PAD_GLYPHS=off: nothing hooked (a diagnostic, not a setting).
        static const bool enabled = [] {
            const char* v = std::getenv("BURNOUT_PAD_GLYPHS");
            return !v || (std::string(v) != "off" && std::string(v) != "0");
        }();
        return enabled;
    }

    void Use(uint8_t* guest, uint32_t base, uint32_t size, uint32_t width, uint32_t height, uint32_t format,
        bool tiled, uint32_t endian, uint32_t pitchBlocks, uint32_t mipBase, uint32_t mipSize)
    {
        if (!tiled)
            return;
        const Known* known = nullptr;
        for (const Known& k : kKnown)
            if (width == k.width && height == k.height && format == k.format && endian == k.endian && pitchBlocks == k.pitchBlocks && size >= k.size)
                known = &k;
        if (!known)
            return;
        const uint32_t n = known->size;
        auto it = s_seen.find(base);
        uint64_t sequence = writewatch::WriteSequence(base, n);
        if (it == s_seen.end())
            if (auto other = s_others.find(base); other != s_others.end() && other->second == sequence)
                return;
        if (it == s_seen.end() || it->second.sequence != sequence)
        {
            // New, or written since: still the game's picture (as loaded, or as drawn here)?
            uint64_t hash = Hash(guest, n);
            if (it != s_seen.end() && it->second.known == known && hash == it->second.appliedHash)
                it->second.sequence = sequence;
            else if (hash == known->hash)
            {
                Seen& seen = s_seen[base];
                seen = { known };
                seen.original.assign(guest, guest + n);
                seen.appliedHash = hash;
                seen.sequence = sequence;
                s_others.erase(base);
                it = s_seen.find(base);
                fprintf(stderr, "[glyphs] the game's %s at %08X (mips %08X+%u)\n", known->name, base, mipBase, mipSize);
            }
            else
            {
                if (it != s_seen.end())
                    s_seen.erase(it);
                s_others[base] = sequence;
                return;
            }
        }
        Apply(it->second, base, guest);
    }

    void Refresh(uint8_t* memory)
    {
        // The renderer keeps its lookups while a texture's memory is
        // unwritten, so a texture already loaded is only redrawn here, once a
        // frame: each one known, if it still holds what it showed.
        for (auto it = s_seen.begin(); it != s_seen.end();)
        {
            Seen& seen = it->second;
            uint32_t base = it->first, n = seen.known->size;
            uint8_t* guest = memory + base;
            if (seen.sequence != writewatch::WriteSequence(base, n))
            {
                if (Hash(guest, n) != seen.appliedHash)
                {
                    it = s_seen.erase(it);  // the game's now (Use sees it again if it is the picture)
                    continue;
                }
                seen.sequence = writewatch::WriteSequence(base, n);
            }
            Apply(seen, base, guest);
            ++it;
        }
    }
}
