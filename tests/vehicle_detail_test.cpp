// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Settings > Graphics > Vehicle Detail (game/vehicle_detail.h): the factor,
// the six distances it asks for, and what a call does with what it finds.
#include <game/vehicle_detail.h>

#include <cassert>
#include <cstdio>

using namespace game::vehicle_detail;

int main()
{
    constexpr std::array<float, 3> stock = { 30, 60, 90 };

    // Unknown values read as the original game.
    assert(Factor(1) == 1 && Factor(2) == 2 && Factor(4) == 4);
    assert(Factor(0) == 1 && Factor(3) == 1 && Factor(-2) == 1 && Factor(8) == 1);

    const Distances original = Wanted(stock, 1);
    const Distances far = Wanted(stock, 2);
    const Distances farthest = Wanted(stock, 4);
    assert((original == Distances{ 30, 60, 90, 30, 60, 90 }));
    assert((far == Distances{ 60, 120, 180, 60, 120, 180 }));
    assert((farthest == Distances{ 120, 240, 360, 120, 240, 360 }));

    const Distances none{};
    // The set-up has just copied the stock table.
    assert(Check(original, original, none, stock) == Found::Keep);
    assert(Check(original, far, none, stock) == Found::Write);
    // Ours, then the setting changes either way.
    assert(Check(far, far, far, stock) == Found::Keep);
    assert(Check(far, farthest, far, stock) == Found::Write);
    assert(Check(farthest, original, farthest, stock) == Found::Write);
    // The set-up again (a new race) over our values.
    assert(Check(original, farthest, farthest, stock) == Found::Write);
    // Half written by someone else.
    Distances mixed = far;
    mixed[4] = 75;
    assert(Check(mixed, far, far, stock) == Found::Foreign);
    assert(Check(mixed, original, far, stock) == Found::Foreign);

    printf("vehicle_detail_test: ok\n");
    return 0;
}
