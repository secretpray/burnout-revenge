// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The display's refresh period and phase, measured from the times presents
// were seen on screen (VK_KHR_present_wait, video/presenter.cpp). Neither the
// display mode nor the windowing system can be asked: under gamescope, SDL
// and XRandR report gamescope's own CVT mode (59.9356 Hz for a monitor that
// runs at 59.9726 Hz).
//
// Completions land on the display's refresh grid, one refresh or more apart
// (a present that missed, or none to show). Each gets an integer refresh
// index from its gap to the one before, so missed refreshes don't bend the
// fit, and a least-squares line through (index, time) gives the period; gaps
// longer than kMaxGap refreshes start a new segment, which shares the period
// but has its own offset. The window is the last 20 s (at least the newest
// 720 completions, so a long load keeps the estimate). Completions far off
// the line (a late wake-up) are set aside and the line refitted without them.
// A new display rate shows as a window that fits badly while its last 3 s fit
// well: the older completions are dropped and the window refills.
//
// A display at a whole multiple of 60 Hz (120, 180, 240: SetMultiple, from
// the display mode's rate) shows a 60 fps game a frame every `multiple`
// refreshes, so its completions alone can't tell it from a 60 Hz one (all
// gaps even). Told the multiple, the fit indexes completions in the display's
// own refreshes (gaps of 2, and 1 or 3 where a frame came early or late, at
// 120 Hz) and accepts `multiple` times 58.5-61.5 Hz: the guest vblank then
// locks to every `multiple`th refresh (video/vblank_lock.h). At 1, the default,
// everything is as it was. A rate that isn't such a multiple (90 Hz, 144 Hz)
// stays at 1 and is never accepted (MultipleOf: 0).
#pragma once
#include <cstdint>
#include <deque>
#include <vector>

namespace video
{
    struct RefreshEstimate
    {
        bool valid = false;     // accepted: 58.5-61.5 Hz (times `multiple`), at least 10 s of refreshes, small residuals
        double periodNs = 0;    // one refresh (of the display: 8.33 ms at 120 Hz)
        int64_t gridNs = 0;     // a refresh (the fitted time of the newest completion)
        double rmsNs = 0;       // of the completions kept
        uint32_t samples = 0;   // completions in the window
        uint32_t rejected = 0;  // of which set aside as off the line
        double spanNs = 0;      // refresh time the window covers (its segments together)
        int multiple = 1;       // display refreshes per 60 Hz guest vblank: 1, or 2-4 (SetMultiple); 0: a grid check (SetGridHz)
    };

    class RefreshFit
    {
    public:
        // `t`, ns on the steady clock: a present was seen complete. In order.
        void Add(int64_t t);
        const RefreshEstimate& Estimate() const { return m_estimate; }
        // The display refreshes `multiple` (1-4) times per 60 Hz guest
        // vblank: its mode's rate is that multiple of 60 Hz (MultipleOf). A
        // change starts the fit over (the old completions are on another
        // grid). Out of range: 1.
        void SetMultiple(int multiple);
        int Multiple() const { return m_multiple; }
        // The whole multiple of 60 Hz (1-4) that `hz` lies within the
        // acceptance limits of (58.5-61.5 Hz times it): 1 for 59.94, 2 for
        // 119.88, 3 for 180, 4 for 239.76; 0 for 90, 143.9, 165 or an unknown
        // rate (0 or below).
        static int MultipleOf(double hz);
        // A grid check, not a lock's measure: completions indexed at `hz`
        // (the display mode's rate, any: 90, 144), accepted within 2.5% of
        // it, so a valid estimate says the display shows frames on a fixed
        // refresh grid at that rate (a variable-refresh one shows them as
        // they come, 16.7 ms apart, which fits no grid of 1/144 s). The
        // estimate's multiple is 0: never for the guest vblank. A change
        // starts the fit over.
        void SetGridHz(double hz);

        // Acceptance limits: a 60 Hz display, give or take 2.5% (the guest
        // runs at the display's rate, and its game time with it), or
        // `multiple` times that.
        static constexpr double kMinHz = 58.5, kMaxHz = 61.5;
        static constexpr int kMaxMultiple = 4;  // 240 Hz
        static constexpr double kMaxRmsNs = 500'000;      // 0.5 ms
        static constexpr double kMinSpanNs = 10e9;        // 10 s
        static constexpr double kMaxRejected = 0.1;       // of the window

    private:
        struct Sample
        {
            int64_t t;
            int64_t k;         // refresh index within its segment
            uint32_t segment;
        };
        struct Line
        {
            bool ok = false;
            double periodNs = 0, rmsNs = 0, spanNs = 0;
            int64_t gridNs = 0;
            uint32_t used = 0, rejected = 0;
        };
        // Fits samples [first, end): two passes, the second without outliers.
        Line Fit(size_t first) const;
        bool Acceptable(const Line& line, bool needSpan) const;

        static constexpr int64_t kWindowNs = 20'000'000'000;
        static constexpr size_t kKeepSamples = 720;
        static constexpr int64_t kRecentNs = 3'000'000'000;
        static constexpr int64_t kMaxGap = 8;  // refreshes (times the multiple: 8 vblanks); longer gaps start a segment
        static constexpr double kNominalNs = 1e9 / 60.0;

        std::deque<Sample> m_samples;
        uint32_t m_segment = 0;
        int m_multiple = 1;
        double m_rate = 1.0;      // refreshes per 1/60 s: the multiple, or a grid check's hz / 60
        bool m_lineFits = false;  // the last fit was a good line (Acceptable without the span)
        RefreshEstimate m_estimate;
        mutable std::vector<double> m_scratch;
    };
}
