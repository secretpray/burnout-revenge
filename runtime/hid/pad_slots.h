// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Solo keeps the original Fukami fallback below. A local multiplayer session
// reserves its participating slots: only an explicit press on a replacement
// controller can fill a missing slot. SDL instance IDs are never identities.
// Original fallback adapted from danisandoval/fukami, rrv_pad_slots.h (GPL-3.0).
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
    class Assignments
    {
    public:
        Ids ids = Empty();
        unsigned players = 0;
        std::array<bool, kSlots> physical{};

        bool SetPlayers(unsigned count)
        {
            count = count > 1 && count <= kSlots ? count : 0;
            if (count == players) return false;
            players = count;
            physical = {};
            if (players)
                for (unsigned slot = 0; slot < kSlots; ++slot)
                {
                    if (slot >= players) ids[slot] = kEmpty;
                    else physical[slot] = ids[slot] != kEmpty || slot != 0;
                }
            return true;
        }

        bool Refresh(std::span<const int64_t> connected)
        {
            if (!players) return Update(ids, connected);
            const Ids before = ids;
            for (auto& id : ids)
                if (id != kEmpty && !Contains(connected, id)) id = kEmpty;
            return ids != before;
        }

        int Missing() const
        {
            for (unsigned slot = 0; slot < players; ++slot)
                if (physical[slot] && ids[slot] == kEmpty) return int(slot);
            return -1;
        }

        // Caller detects a fresh Start edge after opening/releasing the device.
        // One press fills the first missing participating slot, never two.
        bool Confirm(int64_t id, std::span<const int64_t> connected)
        {
            const int slot = Missing();
            if (slot < 0 || id < 0 || SlotOf(ids, id) >= 0 || !Contains(connected, id)) return false;
            ids[unsigned(slot)] = id;
            return true;
        }

        bool KeyboardAllowed() const { return !players || !physical[0]; }
    };

}
