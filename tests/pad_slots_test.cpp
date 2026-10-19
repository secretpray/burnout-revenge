// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Which controller is which player (hid/pad_slots.h).
#include <hid/pad_slots.h>

#include <cassert>
#include <cstdio>
#include <vector>

using namespace hid::padslots;

namespace
{
    bool Step(Ids& ids, std::vector<int64_t> connected)
    {
        return Update(ids, connected);
    }

    Ids Of(int64_t a, int64_t b = kEmpty, int64_t c = kEmpty, int64_t d = kEmpty)
    {
        return { a, b, c, d };
    }
}

int main()
{
    {
        // In the order they connect; nothing to do twice.
        Ids ids = Empty();
        assert(!Step(ids, {}) && ids == Empty());
        assert(Step(ids, { 7 }) && ids == Of(7));
        assert(Step(ids, { 7, 3 }) && ids == Of(7, 3));
        assert(!Step(ids, { 7, 3 }) && ids == Of(7, 3));
        // SDL's id 0 is a controller like any other.
        Ids zero = Empty();
        assert(Step(zero, { 0 }) && zero == Of(0));
    }
    {
        // Two players: player 2 leaves, player 1 stays; it comes back as 2.
        Ids ids = Of(7, 3);
        assert(Step(ids, { 7 }) && ids == Of(7));
        assert(Step(ids, { 7, 9 }) && ids == Of(7, 9));
    }
    {
        // Player 1 leaves with nobody waiting: player 2 becomes player 1,
        // and the controller that comes back is player 2 (also the case of
        // one controller seen over Bluetooth and USB, whose first copy goes).
        Ids ids = Of(7, 3);
        assert(Step(ids, { 3 }) && ids == Of(3));
        assert(Step(ids, { 3, 8 }) && ids == Of(3, 8));
    }
    {
        // Players 3 and 4 keep their slots when 2 leaves; the gap stays.
        Ids ids = Of(1, 2, 3, 4);
        assert(Step(ids, { 1, 3, 4 }) && ids == Of(1, kEmpty, 3, 4));
        // The next one fills it.
        assert(Step(ids, { 1, 3, 4, 5 }) && ids == Of(1, 5, 3, 4));
    }
    {
        // A fifth waits; it takes the first slot that empties, player 1's
        // included, and the others stay where they are.
        Ids ids = Empty();
        assert(Step(ids, { 1, 2, 3, 4, 5 }) && ids == Of(1, 2, 3, 4));
        assert(Step(ids, { 2, 3, 4, 5 }) && ids == Of(5, 2, 3, 4));
    }
    {
        // Player 1 leaves while 3 plays and 2 is empty: 3 moves up to 1.
        Ids ids = Of(1, kEmpty, 3);
        assert(Step(ids, { 3 }) && ids == Of(3));
    }
    {
        // Several go at once.
        Ids ids = Of(1, 2, 3, 4);
        assert(Step(ids, { 4 }) && ids == Of(4));
        assert(Step(ids, {}) && ids == Empty());
    }
    {
        // An id never takes two slots, and bad ids are ignored.
        Ids ids = Empty();
        assert(Step(ids, { 6, 6, -1 }) && ids == Of(6));
    }
    printf("pad_slots_test: ok\n");
    return 0;
}
