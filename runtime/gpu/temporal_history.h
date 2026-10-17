// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace gpu::temporal
{
    struct Rect { uint32_t x, y, w, h; };
    // Reject incomplete, overlapping or out-of-range tile snapshots. A union
    // bounding box alone accepts holes, and is not a usable depth history.
    struct Coverage
    {
        std::array<Rect, 8> tiles{};
        uint32_t count = 0, width = 0, height = 0;
        uint64_t area = 0;
        bool invalid = false;
        void Reset(uint32_t w, uint32_t h) { *this = {}; width = w; height = h; }
        bool Add(Rect r)
        {
            if (invalid || !r.w || !r.h || r.x > width || r.y > height ||
                r.w > width-r.x || r.h > height-r.y || count == tiles.size())
            { invalid = true; return false; }
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto t = tiles[i];
                if (r.x < t.x+t.w && t.x < r.x+r.w && r.y < t.y+t.h && t.y < r.y+r.h)
                { invalid = true; return false; }
            }
            tiles[count++] = r; area += uint64_t(r.w)*r.h;
            return true;
        }
        bool Complete() const { return !invalid && width && height && area == uint64_t(width)*height; }
    };

    // A conservative camera-cut heuristic. The dominant matrix must still
    // be identified by the game-specific producer; this is not an object ID.
    inline bool CameraCut(const std::array<float,16>& a, const std::array<float,16>& b)
    {
        auto dot = [](const auto& x, const auto& y) { return x[0]*y[0]+x[1]*y[1]+x[2]*y[2]; };
        auto cross = [](const auto& x, const auto& y) { return std::array<double,3>{
            x[1]*y[2]-x[2]*y[1], x[2]*y[0]-x[0]*y[2], x[0]*y[1]-x[1]*y[0]}; };
        auto origin = [&](const auto& m, std::array<double,3>& o) {
            const std::array<double,3> x{m[0],m[1],m[2]}, y{m[4],m[5],m[6]}, z{m[12],m[13],m[14]};
            const auto yz=cross(y,z), zx=cross(z,x), xy=cross(x,y);
            const double det=dot(x,yz);
            if (!std::isfinite(det) || std::abs(det)<1e-8) return false;
            for (int k=0;k<3;++k) o[k]=-(m[3]*yz[k]+m[7]*zx[k]+m[15]*xy[k])/det;
            return true;
        };
        for (int k=0;k<16;++k) if (!std::isfinite(a[k]) || !std::isfinite(b[k])) return true;
        std::array<double,3> ao,bo;
        if (!origin(a,ao) || !origin(b,bo)) return true;
        double distance=0, forwardDot=0, an=0,bn=0;
        for (int k=0;k<3;++k)
        {
            distance+=(ao[k]-bo[k])*(ao[k]-bo[k]);
            forwardDot+=a[12+k]*b[12+k]; an+=a[12+k]*a[12+k]; bn+=b[12+k]*b[12+k];
        }
        if (distance>400 || an<1e-8 || bn<1e-8 || forwardDot/std::sqrt(an*bn)<.965925826) return true;
        for (int row : {0,4})
        {
            double na=0,nb=0;
            for (int k=0;k<3;++k) { na+=a[row+k]*a[row+k]; nb+=b[row+k]*b[row+k]; }
            if (na<1e-8 || nb/na<.9025 || nb/na>1.1025) return true;
        }
        return false;
    }
}
