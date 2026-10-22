// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include "achievement_data.h"
#include <array>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>

namespace achievements
{
    struct Entry { uint32_t id; int64_t unlocked = 0; };
    struct Notice { uint32_t id, user; };
    // A console profile per local user slot, shared by offline and online play.
    // The game decides when to earn an achievement. The runtime only records it.
    class Store
    {
        mutable std::mutex mutex;
        std::filesystem::path path;
        std::vector<Definition> definitions;
        std::array<std::vector<Entry>, 4> users;
        std::deque<Notice> notices;
        std::string error;
        bool unreadable = false;
        bool Save(); // under mutex
    public:
        bool Initialize(std::span<const uint8_t> database, std::filesystem::path file, uint32_t language = 1);
        bool Earn(uint32_t user, uint32_t id); // true also for a duplicate; false on invalid input or disk failure
        std::vector<Entry> Entries(uint32_t user = 0) const;
        const std::vector<Definition>& Definitions() const { return definitions; } // immutable after startup
        std::string Error() const;
        bool HasNotices() const;
        std::optional<Notice> PopNotice();
    };
    Store& Current();
}
