// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>

namespace game::rewind::gate
{
    // A file transaction may be opened and closed by different guest threads.
    // The mutex protects registration, not a lock held across that lifetime.
    inline std::mutex mutex;
    inline unsigned writers{};
    inline std::atomic<uint64_t> epoch{0};
    inline void BeginWrite()
    {
        std::lock_guard lock(mutex); ++writers; epoch.fetch_add(1,std::memory_order_release);
    }
    inline void EndWrite()
    {
        std::lock_guard lock(mutex); --writers; epoch.fetch_add(1,std::memory_order_release);
    }
    struct Boundary
    {
        std::unique_lock<std::mutex> lock{mutex,std::try_to_lock};
        explicit operator bool() const { return lock.owns_lock() && writers==0; }
    };
}
