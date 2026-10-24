// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include "crash_launch_model.h"
#include <atomic>
#include <chrono>
#include <mutex>

namespace game::crash_launch
{
    // Rewind starts collecting only after the meter has finalized the car.
    // Its native snapshots can then restore all downstream car/event fields.
    inline std::atomic<bool> rewindBlocked{false};
    struct Status
    {
        bool visible{};
        bool waiting{};
        bool nativeMessage{};
        float time{}, alpha{1};
        Model model;
    };

    // The guest publishes values only. The presenter never reads guest memory
    // or the guest thread's launch state. Expire a stopped publisher promptly.
    class StatusExchange
    {
        std::mutex mutex;
        Status status;
        std::chrono::steady_clock::time_point updated{};
    public:
        void Publish(const Status& value)
        {
            std::lock_guard lock(mutex);
            status = value;
            updated = std::chrono::steady_clock::now();
        }
        Status Read()
        {
            std::lock_guard lock(mutex);
            auto value = status;
            if (std::chrono::steady_clock::now() - updated > std::chrono::milliseconds(250))
                value.visible = false;
            return value;
        }
    };
    inline StatusExchange hudStatus;
}
