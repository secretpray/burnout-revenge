// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The swapchain the present waiter's thread may use, against the main thread
// destroying it and creating another (video/presenter.cpp).
//
// The waiter (PresentWaiter) waits for each present to reach the screen
// (vkWaitForPresentKHR) and, on a Mac showing frames at a time, reads the
// driver's reports on the presents shown (vkGetPastPresentationTimingGOOGLE,
// for present_schedule.h's watchdog). Both take the swapchain. The main
// thread destroys it and makes another whenever the surface changes: a
// fullscreen toggle, a resize, V-Sync. In v0.1.4 and v0.1.5 the wait looked
// at the swapchain under a mutex, but the reports were read after it,
// outside it: when the main thread destroyed the swapchain in between,
// MoltenVK read a freed swapchain and the game aborted ("mutex lock failed"
// in vkGetPastPresentationTimingGOOGLE: a MacBook's 120 Hz panel, the
// default settings, fullscreen toggled again and again). The reports had
// been read that way only for BURNOUT_VBLANK_LOG until v0.1.4's watchdog read
// them every frame.
//
// Every use of the swapchain off the main thread goes through Use(), which
// runs it only while the swapchain it is for is still the one published, and
// holds Retire() off until it returns. Retire() (before the swapchain is
// destroyed) returns once no use is under way, and none starts after it.
// Uses name the swapchain by its generation, not its handle: a new swapchain
// may get a destroyed one's handle (MoltenVK's and Mesa's are pointers), and
// a present to the old one must not be waited for on the new one.
//
// Header only (tests/swapchain_use_test.cpp).
#pragma once
#include <cstdint>
#include <mutex>

namespace video
{
    template <typename Handle>
    class SwapchainUse
    {
    public:
        // Main thread, once `swapchain` is made: uses may take it from now
        // on. Its generation (never 0), which they name it by.
        uint64_t Publish(Handle swapchain)
        {
            std::lock_guard lock(m_mutex);
            m_swapchain = swapchain;
            m_generation = ++m_lastGeneration;
            return m_generation;
        }

        // Main thread, before the published swapchain is destroyed: on
        // return no use of it is under way, and none will start.
        void Retire()
        {
            std::lock_guard lock(m_mutex);
            m_swapchain = Handle{};
            m_generation = 0;
        }

        // Any thread: use(swapchain) if `generation` is still the published
        // one, with Retire() held off until it returns. False: that
        // swapchain is gone (or about to be), and `use` didn't run.
        template <typename F>
        bool Use(uint64_t generation, F&& use)
        {
            std::lock_guard lock(m_mutex);
            if (generation == 0 || generation != m_generation)
                return false;
            use(m_swapchain);
            return true;
        }

    private:
        std::mutex m_mutex;
        Handle m_swapchain{};
        uint64_t m_generation = 0;  // the published one's; 0: none
        uint64_t m_lastGeneration = 0;
    };
}
