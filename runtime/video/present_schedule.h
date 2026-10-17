// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// When a new frame is shown at a set time after its guest vblank (macOS:
// VK_GOOGLE_display_timing's desiredPresentTime, video/presenter.cpp), and
// the watchdog that turns that off for the session when such frames don't
// reach the screen.
//
// It helps where it was measured to: a MacBook Pro's 120 Hz ProMotion panel
// in fullscreen, which shows each frame when its GPU work is done (on a 240 Hz
// timing grid), so frames shown when ready came 8-29 ms apart while driving,
// and 99.9% of them 16.67 ms apart when shown at a time. On a 60 Hz display
// the guest vblank locks to the refresh grid instead (VblankLock), and frames
// are shown when ready, as in v0.1.3: in a macOS 15 VM (Apple's paravirtual
// display, 60 Hz) frames shown at a time left the fullscreen picture black
// while the game ran at 60 fps, and spoiled the present-wait times the lock
// fits (rms 1.3-4.3 ms against its 0.5 ms limit, where v0.1.3 had 0.3-0.4),
// so it never locked. There Metal didn't keep the time asked for: from ~10 s
// in, 97% of the frames were reported shown 9 ms (median) BEFORE it, right
// after their present, while the screen stayed black (no time-base mix-up:
// the steady clock and Metal's host time agreed there to the microsecond).
// On the ProMotion panel no frame of 46,000 was reported more than 1.2 ms
// before its time. So, with no BURNOUT_PRESENT_AT (Auto):
//   - only on a display whose mode is a 120 Hz one (117-123 Hz: twice the
//     lock's 60 Hz range), and only while the vblank isn't locked to it (a
//     ProMotion panel has no refresh grid to lock to; should the lock take
//     it, frames are shown when ready until it lets go);
//   - never once the watchdog has tripped.
// BURNOUT_PRESENT_AT=<ms> > 0 shows them at a time on any display (On, still
// watched); BURNOUT_PRESENT_AT=0 never (Off).
//
// The watchdog reads the driver's report on each frame shown at a time
// (VK_GOOGLE_display_timing's past presentation timing: Metal's presentedTime,
// 0 when the drawable was never shown) and trips when, of the last kWindow
// such frames reported, kBadOfWindow or more were shown more than kEarlyNs
// before their time (the time not kept: the VM's black screen), never shown,
// or shown more than kLateNs after it; or when kUnreported frames shown at a
// time in a row have been presented without any report coming back. One
// frame shown late now and then (its GPU work not done in time) is normal:
// on the ProMotion panel up to 21 ms. In the VM the first seconds' frames were
// each reported ~100 ms after their present (88 ms after their time), and
// then, from ~10 s, the display froze on its last picture for the rest of the
// run, frames reported early (above); the freeze outlasted the trip, so the
// trip must come before it.
#pragma once
#include <cstdint>

namespace video
{
    class PresentSchedule
    {
    public:
        enum class Mode
        {
            Auto,  // BURNOUT_PRESENT_AT unset
            On,    // BURNOUT_PRESENT_AT=<ms> > 0
            Off,   // BURNOUT_PRESENT_AT=0
        };
        static constexpr double kMinFastHz = 117.0, kMaxFastHz = 123.0;
        static constexpr uint32_t kWindow = 120;      // 2 s of frames
        static constexpr uint32_t kBadOfWindow = 60;
        static constexpr int64_t kEarlyNs = 2'000'000;
        static constexpr int64_t kLateNs = 50'000'000;
        static constexpr uint32_t kUnreported = 180;  // 3 s of frames

        explicit PresentSchedule(Mode mode = Mode::Auto) : m_mode(mode) {}

        Mode GetMode() const { return m_mode; }
        // The display's mode, at each swapchain and display change (0: unknown).
        void SetDisplayHz(double hz) { m_fastDisplay = hz >= kMinFastHz && hz <= kMaxFastHz; }
        bool FastDisplay() const { return m_fastDisplay; }

        // A new frame, just before its present: show it at its vblank's
        // time? `locked`: the guest vblank is locked to the display.
        bool Wanted(bool locked) const
        {
            if (m_tripped || m_mode == Mode::Off)
                return false;
            return m_mode == Mode::On || (m_fastDisplay && !locked);
        }

        // A frame shown at a time was presented (its present wait returned).
        // True: this trips the watchdog (no report for kUnreported of them).
        bool Presented()
        {
            if (m_tripped)
                return false;
            if (++m_unreported < kUnreported)
                return false;
            m_tripped = true;
            m_reason = Reason::Unreported;
            return true;
        }

        // The driver's report on a frame shown at a time: whether it was
        // shown, and how long after its time (ns; below 0: before). True:
        // this trips the watchdog.
        bool Reported(bool shown, int64_t lateNs)
        {
            m_unreported = 0;
            if (m_tripped)
                return false;
            const bool bad = !shown || lateNs < -kEarlyNs || lateNs > kLateNs;
            m_early += shown && lateNs < -kEarlyNs;
            if (m_count == kWindow)
                m_bad -= m_window[m_next];
            else
                m_count++;
            m_window[m_next] = bad;
            m_bad += bad;
            m_next = (m_next + 1) % kWindow;
            m_notShown += !shown;
            if (m_bad < kBadOfWindow)
                return false;
            m_tripped = true;
            m_reason = Reason::NotKept;
            return true;
        }

        bool Tripped() const { return m_tripped; }
        enum class Reason
        {
            None,
            NotKept,     // kBadOfWindow of the last kWindow shown early, never, or far late
            Unreported,  // kUnreported presented without a report
        };
        Reason GetReason() const { return m_reason; }
        uint32_t BadInWindow() const { return m_bad; }
        uint32_t ReportedInWindow() const { return m_count; }
        uint64_t NotShown() const { return m_notShown; }
        uint64_t Early() const { return m_early; }

    private:
        Mode m_mode;
        bool m_fastDisplay = false;
        bool m_tripped = false;
        Reason m_reason = Reason::None;
        bool m_window[kWindow] = {};
        uint32_t m_next = 0, m_count = 0, m_bad = 0;
        uint32_t m_unreported = 0;
        uint64_t m_notShown = 0, m_early = 0;
    };
}
