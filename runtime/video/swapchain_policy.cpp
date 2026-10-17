// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See swapchain_policy.h.
#include "swapchain_policy.h"

#include <algorithm>
#include <cmath>

namespace video
{
    const char* OrientationName(Orientation orientation)
    {
        switch (orientation)
        {
        case Orientation::Landscape:
            return "landscape";
        case Orientation::Portrait:
            return "portrait";
        default:
            return "unknown";
        }
    }

    const char* SwapActionName(SwapAction action)
    {
        switch (action)
        {
        case SwapAction::Recreate:
            return "recreate";
        case SwapAction::RepairView:
            return "repair the view";
        default:
            return "none";
        }
    }

    ExtentChoice ChooseExtent(Extent surface, Orientation interface, bool landscapeOnly, bool refusePortrait)
    {
        // Unknown counts as landscape: the app shows in nothing else.
        if (!refusePortrait || !landscapeOnly || !surface.Portrait() || interface == Orientation::Portrait)
            return { surface, false };
        return { Extent{ surface.height, surface.width }, true };
    }

    void SwapchainPolicy::Suspended()
    {
        resumeWatch_ = false;
    }

    void SwapchainPolicy::Resumed()
    {
        settleLeft_ = std::max(settleLeft_, rules_.settleFrames);
        resumeWatch_ = rules_.settleFrames > 0;
    }

    void SwapchainPolicy::SizeChanged()
    {
        sizePending_ = true;
        settleLeft_ = std::max(settleLeft_, rules_.settleFrames);
    }

    void SwapchainPolicy::Reported(SurfaceResult result)
    {
        if (result == SurfaceResult::OutOfDate)
            outOfDate_ = true;
        else if (rules_.followSuboptimal)
            suboptimal_ = true;
    }

    void SwapchainPolicy::Created(Extent)
    {
        outOfDate_ = false;
        sizePending_ = false;
        settleLeft_ = std::max(settleLeft_, rules_.settleFrames);
    }

    bool SwapchainPolicy::WantsSurface() const
    {
        return sizePending_ || suboptimal_ || outOfDate_ || settleLeft_ > 0 || misfitFrames_ > 0 || refusedFrames_ > 0 ||
            (rules_.periodFrames > 0 && (frame_ + 1) % rules_.periodFrames == 0);
    }

    SwapDecision SwapchainPolicy::Decide(const SurfaceState& s)
    {
        SwapDecision d;
        if (!s.active && rules_.holdWhileInactive)
        {
            // What was seen meanwhile waits for the resume: a size reported
            // now (iOS turning or snapshotting the window) is not the one the
            // app will show at.
            d.reason = "inactive";
            return d;
        }
        frame_++;
        if (settleLeft_ > 0 && --settleLeft_ == 0 && resumeWatch_)
        {
            resumeWatch_ = false;
            d.settled = true;
        }

        if (outOfDate_)
        {
            // The old one can't be presented to: build again whatever the size.
            outOfDate_ = suboptimal_ = sizePending_ = false;
            ExtentChoice choice = ChooseExtent(s.surface, s.interface, s.landscapeOnly, rules_.refusePortrait);
            d.action = SwapAction::Recreate;
            d.reason = "VK_ERROR_OUT_OF_DATE_KHR";
            d.extent = choice.extent;
            d.refused = choice.refused;
            return d;
        }
        if (!s.surfaceRead)
            return d;
        if (s.surface.Empty())
        {
            // Minimised (or no layer): as before, a size event tears the
            // swapchain down and the presenter waits for a size again.
            refusedFrames_ = misfitFrames_ = 0;
            if (sizePending_ && !s.swapchain.Empty())
            {
                sizePending_ = suboptimal_ = false;
                d.action = SwapAction::Recreate;
                d.reason = "no surface (minimised)";
            }
            return d;
        }

        // iOS: the surface is the view's layer. A view that doesn't fill its
        // window was sized by someone guessing (SDL's portrait guess): fit it
        // back, and look again next frame, before following it anywhere.
        if (!s.viewFitsWindow && rules_.repairView)
        {
            misfitFrames_++;
            if (misfitFrames_ <= rules_.repairPatience)
            {
                if (misfitFrames_ <= 3 || (rules_.repairEvery && misfitFrames_ % rules_.repairEvery == 0))
                {
                    d.action = SwapAction::RepairView;
                    d.reason = "the view doesn't fill its window";
                }
                else
                    d.reason = "waiting for the view to fill its window";
                return d;
            }
            // It won't: the swapchain follows the layer after all (below).
        }
        else
            misfitFrames_ = 0;

        const ExtentChoice choice = ChooseExtent(s.surface, s.interface, s.landscapeOnly, rules_.refusePortrait);
        if (choice.extent != s.swapchain)
        {
            d.action = SwapAction::Recreate;
            d.reason = s.swapchain.Empty() ? "no swapchain"
                : sizePending_             ? "the size changed"
                : resumeWatch_             ? "after the resume"
                : suboptimal_              ? "VK_SUBOPTIMAL_KHR"
                                           : "the surface's size moved";
            d.extent = choice.extent;
            d.refused = choice.refused;
            sizePending_ = suboptimal_ = false;
            refusedFrames_ = choice.refused ? refusedFrames_ + 1 : 0;
            return d;
        }
        sizePending_ = false;
        if (choice.refused)
        {
            // A portrait layer under a landscape-only interface: the swapchain
            // keeps its landscape size (its SUBOPTIMALs say only that), and the
            // view is fitted back to its window.
            suboptimal_ = false;
            refusedFrames_++;
            d.refused = true;
            if (rules_.repairView && (refusedFrames_ <= 3 || (rules_.repairEvery && refusedFrames_ % rules_.repairEvery == 0)))
            {
                d.action = SwapAction::RepairView;
                d.reason = "a portrait surface under a landscape interface";
            }
            else
                d.reason = "a portrait surface refused";
            return d;
        }
        refusedFrames_ = 0;
        if (suboptimal_)
        {
            suboptimal_ = false;
            // The same size: the drawables or the layer's scale disagree with
            // it. Build again, but not every frame if that doesn't settle it.
            if (lastSameRecreate_ == 0 || frame_ - lastSameRecreate_ >= rules_.suboptimalBackoff)
            {
                lastSameRecreate_ = frame_;
                d.action = SwapAction::Recreate;
                d.reason = "VK_SUBOPTIMAL_KHR at the same size";
                d.extent = choice.extent;
                return d;
            }
            d.reason = "VK_SUBOPTIMAL_KHR again (backing off)";
        }
        return d;
    }

    UiDisplay FitUiDisplay(float windowWidth, float windowHeight, Extent fb)
    {
        UiDisplay u;
        u.width = windowWidth;
        u.height = windowHeight;
        if (fb.Empty() || windowWidth <= 0 || windowHeight <= 0)
            return u;
        // Pixels per point, whichever way round the two are.
        const float ppp = float(std::max(fb.width, fb.height)) / std::max(windowWidth, windowHeight);
        const float w = float(fb.width) / ppp, h = float(fb.height) / ppp;
        if (std::fabs(w - windowWidth) > 2.0f || std::fabs(h - windowHeight) > 2.0f)
        {
            u.width = w;
            u.height = h;
            u.fromSwapchain = true;
        }
        u.scaleX = float(fb.width) / u.width;
        u.scaleY = float(fb.height) / u.height;
        return u;
    }
}
