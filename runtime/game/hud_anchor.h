// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The race HUD at the edges of an ultrawide screen (Settings > Display > HUD
// at Screen Edges, on by default), and the menus' frame at them (always). Under Hor+ (game/ultrawide.h) the 2D is narrowed into a 16:9
// block in the middle; here each element of the race HUD moves to the edge it
// belongs to instead, keeping its shape and size, and keeps the margin it has
// from that edge at 16:9 (Takedawn's widescreen HUD does this through the
// title's layout rectangle; Revenge has no symbols to find it by, so the
// elements are found in what is drawn).
//
// The HUD is drawn piece by piece (game/burnout.cpp's batch flush: one strip
// per piece). Pieces near one another (12 px) over the last few frames form an
// element - the TAKEDOWNS label, its digits and the plates behind them; the
// title's three plates and the clock above the middle. An element across the
// frame's middle stays there; one wholly left or right of it moves by
// (640 / s - 640) pixels of the 16:9 frame, s the view's x scale: its margin
// to the frame's edge becomes its margin to the screen's. A row of pieces
// that together span the frame's width (the crash camera's tyre-track
// strips, five tiles of 256) is widened to the screen's edges like the full-
// width layers. Pieces of rows and full-width layers join no element.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace game::hud
{
    struct Box
    {
        float x0, y0, x1, y1;
    };

    enum class Place
    {
        Middle,  // stays in the 16:9 block
        Left,
        Right,
        Row,  // part of a row across the frame: widened to the screen's edges
    };

    // The shift in 16:9 frame pixels (before the narrowing by s) that keeps an
    // element's margin to its edge.
    inline float EdgeShift(float s)
    {
        return s > 0.0f && s < 1.0f ? 640.0f * (1.0f - s) / s : 0.0f;
    }

    // The front end's grimy frame (the menus' border, 18.10): twelve tiles 128
    // pixels thick along the 16:9 frame's edges - columns x -1..127 and
    // 1152..1280, the top and bottom rows between them in two halves
    // (127..639, 639..1152), the sides in two (y 127..384, 335..593). On an
    // ultrawide it hung as a 16:9 frame in the middle of the picture; it is
    // stretched to the screen instead: the columns move to the screen's
    // edges, the rows between them widen to meet them (by the batch flush's
    // constants, which gpu/renderer.cpp then leaves unclipped).
    inline bool IsMenuFrameTile(const Box& b)
    {
        auto near = [](float v, float t) { return std::fabs(v - t) <= 2.0f; };
        auto pair = [&](float a0, float a1, float t0, float t1) { return near(a0, t0) && near(a1, t1); };
        const bool left = pair(b.x0, b.x1, -1.0f, 127.0f), right = pair(b.x0, b.x1, 1152.0f, 1280.0f);
        const bool midX = pair(b.x0, b.x1, 127.0f, 639.0f) || pair(b.x0, b.x1, 639.0f, 1152.0f);
        const bool top = pair(b.y0, b.y1, -1.0f, 127.0f), bottom = pair(b.y0, b.y1, 593.0f, 722.0f);
        const bool midY = pair(b.y0, b.y1, 127.0f, 384.0f) || pair(b.y0, b.y1, 335.0f, 593.0f);
        return ((left || right) && (top || bottom || midY)) || (midX && (top || bottom));
    }

    // How a frame tile is placed: a column moves by -+EdgeShift(s), a row's
    // half between the columns widens about the frame's middle by this
    // factor, so that its ends (127 and 1152) meet the moved columns.
    inline float MenuFrameStretch(float s)
    {
        return (1025.0f + 2.0f * EdgeShift(s)) / 1025.0f;
    }

    class Anchor
    {
    public:
        // `now` in seconds (any steady clock). A piece of the race HUD with
        // its box in 1280x720 frame pixels.
        Place Decide(const Box& box, double now)
        {
            Expire(now);
            if (MarkRow(box, now))
            {
                // Its tiles seen before the row was complete join no element.
                std::erase_if(m_pieces, [&](const Piece& p) {
                    return std::lround(p.box.y0) == std::lround(box.y0) && std::lround(p.box.y1) == std::lround(box.y1);
                });
                return Place::Row;
            }
            // A piece drawn again where it was (the static HUD, frame after
            // frame) is the same piece, seen later.
            auto same = std::find_if(m_pieces.begin(), m_pieces.end(), [&](const Piece& p) {
                return std::fabs(p.box.x0 - box.x0) < 2.0f && std::fabs(p.box.x1 - box.x1) < 2.0f &&
                    std::fabs(p.box.y0 - box.y0) < 2.0f && std::fabs(p.box.y1 - box.y1) < 2.0f;
            });
            if (same != m_pieces.end())
                same->time = now;
            else if (m_pieces.size() < 512)
                m_pieces.push_back({ box, now });
            // The element: every recent piece reached from this one through
            // pieces within kGap of each other.
            Box e = box;
            std::vector<bool> taken(m_pieces.size(), false);
            for (bool grew = true; grew;)
            {
                grew = false;
                for (size_t i = 0; i < m_pieces.size(); i++)
                {
                    const Box& b = m_pieces[i].box;
                    if (taken[i] || b.x0 > e.x1 + kGap || b.x1 < e.x0 - kGap || b.y0 > e.y1 + kGap || b.y1 < e.y0 - kGap)
                        continue;
                    taken[i] = true;
                    e = { std::min(e.x0, b.x0), std::min(e.y0, b.y0), std::max(e.x1, b.x1), std::max(e.y1, b.y1) };
                    grew = true;
                }
            }
            if (e.x0 <= 640.0f && e.x1 >= 640.0f)
                return Place::Middle;
            return e.x1 < 640.0f ? Place::Left : Place::Right;
        }

    private:
        static constexpr float kGap = 12.0f;
        // Seconds a piece is remembered: long enough that the letters of an
        // animated banner ("CRASHBREAKER AVAILABLE!" flies in letter by
        // letter) are joined by where the others have been.
        static constexpr double kKeep = 0.5;
        static constexpr double kRowKeep = 0.1;  // a row's cells: a few frames
        static constexpr int kCells = 64;     // 20 px each

        struct Piece
        {
            Box box;
            double time;
        };
        struct Row
        {
            int y0, y1;
            std::array<double, kCells> seen;
        };
        std::vector<Piece> m_pieces;
        std::vector<Row> m_rows;

        void Expire(double now)
        {
            std::erase_if(m_pieces, [now](const Piece& p) { return now - p.time > kKeep; });
        }

        // Records the piece in its row (the same top and bottom, to the
        // pixel); true when the row's pieces seen lately cover the frame's
        // width, edge to edge.
        bool MarkRow(const Box& box, double now)
        {
            const int y0 = int(std::lround(box.y0)), y1 = int(std::lround(box.y1));
            if (y1 - y0 < 2 || box.x1 - box.x0 < 8.0f)
                return false;
            auto it = std::find_if(m_rows.begin(), m_rows.end(), [&](const Row& r) { return r.y0 == y0 && r.y1 == y1; });
            if (it == m_rows.end())
            {
                if (m_rows.size() >= 64)
                    m_rows.erase(m_rows.begin());
                Row r{ y0, y1, {} };
                r.seen.fill(-1e9);
                m_rows.push_back(r);
                it = m_rows.end() - 1;
            }
            // The cells whose middles the piece covers.
            const int c0 = std::clamp(int(std::ceil((box.x0 - 10.0f) / 20.0f)), 0, kCells);
            const int c1 = std::clamp(int(std::floor((box.x1 - 10.0f) / 20.0f)) + 1, 0, kCells);
            for (int c = c0; c < c1; c++)
                it->seen[c] = now;
            // Edge to edge but for 40 px at each side: a strip that scrolls
            // (the tyre tracks) may leave its outermost cell uncovered.
            for (int c = 2; c < kCells - 2; c++)
                if (now - it->seen[c] > kRowKeep)
                    return false;
            return true;
        }
    };
}
