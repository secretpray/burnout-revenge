// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// When the presenter builds its swapchain again, and at what size; and the
// size the settings UI lays out at. Pure decisions (no Vulkan, SDL or UIKit:
// presenter.cpp feeds them what it reads), tested with planted wrong rules in
// tests/swapchain_policy_test.cpp. Main thread.
//
// Why (v0.1.2, an iPhone 18 Pro Max on iOS 27): after an iOS screenshot the
// game's picture sat in a corner of the screen and the settings UI was blown
// up. SDL 3.4.16 sets its view's frame itself whenever a keyboard shows or
// hides (-[SDL_uikitviewcontroller updateKeyboard], UIKit_ComputeViewFrame),
// swapping width and height when it believes the interface is portrait:
// iOS 27's UIApplication.statusBarOrientation is "unknown" under the scene
// life cycle, and SDL thinks a resizable window may be portrait whatever the
// Info.plist says. The view became 440x956 points in a 956x440 window, its
// layer followed, and the presenter rebuilt its swapchain at the layer's
// 1320x2868 and never back (nothing moved the view again). So:
//   - nothing is built while the app is inactive or in the background, and a
//     size seen then is only a reason to look again once it is active;
//   - after a resume, a size event or a new swapchain the surface is read
//     every frame for a while (and once a second always), and the swapchain
//     follows it only when they differ;
//   - VK_SUBOPTIMAL_KHR and VK_ERROR_OUT_OF_DATE_KHR are reasons to build
//     again (a SUBOPTIMAL at an unchanged size at most once per backoff);
//   - on iOS a view that doesn't fill its window is fitted back first (the
//     surface is its layer: following it would follow SDL's mistake);
//   - a landscape-only app (BurnoutRevenge on iPhone) never takes a portrait
//     swapchain while its interface is landscape;
//   - the UI lays out at the swapchain's size in points, not a window size
//     that disagrees with it.
#pragma once

#include <cstdint>

namespace video
{
    struct Extent
    {
        uint32_t width = 0, height = 0;
        bool Empty() const { return width == 0 || height == 0; }
        bool Portrait() const { return height > width; }
        friend bool operator==(const Extent&, const Extent&) = default;
    };

    enum class Orientation : uint8_t
    {
        Unknown,
        Landscape,
        Portrait,
    };
    const char* OrientationName(Orientation orientation);

    // The size to build a swapchain at for a surface of `surface`. A
    // landscape-only app refuses a portrait surface unless its interface is
    // portrait too: then it takes the same size turned to landscape (which
    // is what its view's layer measures once fitted back to its window).
    struct ExtentChoice
    {
        Extent extent;
        bool refused = false;  // `surface` itself was refused
    };
    ExtentChoice ChooseExtent(Extent surface, Orientation interface, bool landscapeOnly, bool refusePortrait = true);

    // What vkAcquireNextImageKHR or vkQueuePresentKHR said, other than success.
    enum class SurfaceResult : uint8_t
    {
        Suboptimal,
        OutOfDate,
    };

    // What the presenter read this frame.
    struct SurfaceState
    {
        bool active = true;             // the app is in front (not inactive, not in the background)
        bool surfaceRead = false;       // `surface` (and the fields after it) were read this frame (WantsSurface)
        Extent surface;                 // the surface's extent now (Vulkan's currentExtent, else the window's pixels)
        Extent swapchain;               // the current swapchain's (0x0: none)
        Orientation interface = Orientation::Unknown;  // the interface's (iOS: the window scene's), Unknown elsewhere
        bool landscapeOnly = false;     // the app shows only in landscape here (iPhone)
        bool viewFitsWindow = true;     // iOS: SDL's view fills its UIWindow (always elsewhere)
    };

    enum class SwapAction : uint8_t
    {
        None,
        Recreate,    // build the swapchain again, at `extent`
        RepairView,  // fit the view to its window (iOS), then look again next frame
    };

    struct SwapDecision
    {
        SwapAction action = SwapAction::None;
        const char* reason = "";
        Extent extent;          // Recreate: the size (ChooseExtent's)
        bool refused = false;   // the surface's own size was refused
        bool settled = false;   // the watch after a resume ended this frame: the presenter logs where it stands
    };
    const char* SwapActionName(SwapAction action);

    // The rules, a field each, so the tests can plant a wrong one.
    struct SwapchainRules
    {
        bool holdWhileInactive = true;    // nothing built while the app is inactive or in the background
        bool refusePortrait = true;       // landscape-only: no portrait swapchain under a landscape interface
        bool repairView = true;           // a view that doesn't fill its window is fitted back before anything else
        bool followSuboptimal = true;     // VK_SUBOPTIMAL_KHR builds again
        uint32_t settleFrames = 120;      // frames the surface is read every frame after a resume, a size event or a new swapchain
        uint32_t periodFrames = 60;       // otherwise once every this many frames
        uint32_t suboptimalBackoff = 30;  // a SUBOPTIMAL at an unchanged size builds again at most once per this many frames
        uint32_t repairEvery = 30;        // after the first three, a repair is tried once per this many frames
        uint32_t repairPatience = 180;    // a view that won't fit: after this many frames the swapchain follows its layer anyway
    };

    class SwapchainPolicy
    {
    public:
        explicit SwapchainPolicy(SwapchainRules rules = {}) : rules_(rules) {}

        void Suspended();                  // the app went inactive (or to the background)
        void Resumed();                    // it is active again: watch the surface
        void SizeChanged();                // a window, pixel-size or view-size event
        void Reported(SurfaceResult result);
        void Created(Extent extent);       // a swapchain was built (on any path)

        // Whether to read the surface (and on iOS the view) this frame.
        bool WantsSurface() const;
        // Once a frame, before acquiring an image.
        SwapDecision Decide(const SurfaceState& state);

        const SwapchainRules& Rules() const { return rules_; }

    private:
        SwapchainRules rules_;
        uint64_t frame_ = 0;           // active frames decided
        uint32_t settleLeft_ = 0;      // frames left reading the surface every frame
        bool sizePending_ = false;
        bool suboptimal_ = false;
        bool outOfDate_ = false;
        bool resumeWatch_ = false;     // settleLeft_ counts down from a resume
        uint32_t misfitFrames_ = 0;    // frames the view hasn't filled its window
        uint32_t refusedFrames_ = 0;   // frames the surface has been refused
        uint64_t lastSameRecreate_ = 0;  // frame_ of the last SUBOPTIMAL rebuild at an unchanged size (0: none)
    };

    // The UI's display size (points) and framebuffer scale for a swapchain
    // of `framebuffer` pixels, given the window's size in points. While they
    // agree (to 2 points) the window's size stands, as before; when they
    // don't (a view SDL turned, or a swapchain kept while it is fitted back)
    // the swapchain's own shape wins, at the window's pixels per point.
    struct UiDisplay
    {
        float width = 0, height = 0;   // ImGui's DisplaySize
        float scaleX = 1, scaleY = 1;  // DisplayFramebufferScale
        bool fromSwapchain = false;    // the window's size was overruled
    };
    UiDisplay FitUiDisplay(float windowWidth, float windowHeight, Extent framebuffer);
}
