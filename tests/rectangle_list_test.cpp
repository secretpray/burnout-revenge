// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include <gpu/rectangle_list.h>
#include <algorithm>
#include <array>
#include <cassert>

int main()
{
    using Point = std::array<float, 2>;
    auto distance = [](Point a, Point b) { return (a[0]-b[0])*(a[0]-b[0]) + (a[1]-b[1])*(a[1]-b[1]); };
    // Every missing corner and every vertex permutation must reconstruct
    // the same axis-aligned rectangle, with no triangle folded onto itself.
    std::array<Point,4> corners = {{{0,0},{1280,0},{0,720},{1280,720}}};
    for (int missing = 0; missing < 4; ++missing)
    {
        std::array<int,3> indices{};
        int next = 0;
        for (int i = 0; i < 4; ++i) if (i != missing) indices[next++] = i;
        do
        {
            std::array<Point,3> p = {corners[indices[0]],corners[indices[1]],corners[indices[2]]};
            unsigned pivot = gpu::RectanglePivot(distance(p[1],p[2]),distance(p[2],p[0]),distance(p[0],p[1]));
            unsigned a = (pivot + 1) % 3, b = (pivot + 2) % 3;
            Point fourth = {p[a][0]+p[b][0]-p[pivot][0],p[a][1]+p[b][1]-p[pivot][1]};
            assert(fourth == corners[missing]);
        } while (std::next_permutation(indices.begin(),indices.end()));
    }
    // Match the strict comparison tie-break used by the hardware reference.
    assert(gpu::RectanglePivot(1,1,1) == 2);
}
