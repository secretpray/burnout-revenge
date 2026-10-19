// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Which controller is which player: the game polls XInput users 0 to 3
// every frame, and each user is one slot here. A controller that turns up
// takes the lowest empty slot, so the first is player 1 and the second
// player 2. When one goes, a controller still waiting for a slot (a fifth)
// takes the empty one; and if player 1's slot is left empty with nobody
// waiting, the lowest other player's controller moves up to it, so a lone
// controller is always player 1. That is also what keeps one controller
// seen twice (over Bluetooth and USB at once) from being stuck as player 2
// with nothing as player 1. The price: with two players, if player 1's
// controller drops out, player 2's becomes player 1, and the one that comes
// back is player 2. A controller never holds two slots.
//
// The rule is Fukami's (danisandoval/fukami, src/host/rrv_pad_slots.h,
// GPL-3.0: two ports there), here for four users and written as one step
// that takes the controllers connected now. Separate from SDL so it can be
// tested without a device (tests/pad_slots_test.cpp); hid/input.cpp keeps
// the open controllers in step with it.
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace hid::padslots
{
    constexpr unsigned kSlots = 4;
    constexpr int64_t kEmpty = -1;  // SDL's ids are never negative

    using Ids = std::array<int64_t, kSlots>;

    constexpr Ids Empty() { return { kEmpty, kEmpty, kEmpty, kEmpty }; }

    // The slot holding `id`, or -1.
    constexpr int SlotOf(const Ids& ids, int64_t id)
    {
        for (unsigned slot = 0; slot < kSlots; slot++)
            if (ids[slot] == id)
                return int(slot);
        return -1;
    }

    constexpr bool Contains(std::span<const int64_t> list, int64_t id)
    {
        for (int64_t v : list)
            if (v == id)
                return true;
        return false;
    }

    // Brings `ids` up to date with the controllers usable now, in the order
    // they connected: a slot whose controller is gone is freed; a
    // controller without a slot takes the lowest empty one (none: it waits);
    // then an empty slot 0 takes the lowest other slot's controller.
    // Returns whether anything changed.
    constexpr bool Update(Ids& ids, std::span<const int64_t> connected)
    {
        const Ids before = ids;
        for (int64_t& id : ids)
            if (id != kEmpty && !Contains(connected, id))
                id = kEmpty;
        for (int64_t id : connected)
        {
            if (id < 0 || SlotOf(ids, id) >= 0)
                continue;
            const int slot = SlotOf(ids, kEmpty);
            if (slot < 0)
                break;
            ids[unsigned(slot)] = id;
        }
        if (ids[0] == kEmpty)
            for (unsigned slot = 1; slot < kSlots; slot++)
                if (ids[slot] != kEmpty)
                {
                    ids[0] = ids[slot];
                    ids[slot] = kEmpty;
                    break;
                }
        return ids != before;
    }
}
