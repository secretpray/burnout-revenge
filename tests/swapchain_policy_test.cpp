// Tests for video::SwapchainPolicy, ChooseExtent and FitUiDisplay
// (runtime/video/swapchain_policy.cpp): when the presenter builds its
// swapchain again, at what size, and the size the UI lays out at. Each
// scenario drives the policy as presenter.cpp does (the surface read when it
// asks, a rebuild at the size it decides, a view fitted back when it says,
// MoltenVK's SUBOPTIMAL after a present while the layer and the swapchain
// disagree); then every planted wrong rule must fail at least one scenario.
// From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/swapchain_policy_test.cpp runtime/video/swapchain_policy.cpp -o build/swapchain_policy_test
//   build/swapchain_policy_test
#include <video/swapchain_policy.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace video;

static int g_failures = 0;
static bool g_quiet = false;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; if (!g_quiet) { printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static const Extent kLandscape{ 2868, 1320 };  // an iPhone 18 Pro Max's screen, in pixels
static const Extent kPortrait{ 1320, 2868 };   // the same, as SDL's turned view measured it

// The presenter, the layer and the view, around one policy.
struct Screen
{
    SwapchainPolicy policy;
    Extent swapchain = kLandscape;
    Extent surface = kLandscape;   // the layer's size (MoltenVK's currentExtent)
    Extent windowPixels = kLandscape;  // what a fitted-back view measures
    bool active = true;
    Orientation interface = Orientation::Landscape;
    bool landscapeOnly = false;
    bool viewFits = true;
    bool repairWorks = true;       // fitting the view back takes
    int frames = 0;
    int recreates = 0, repairs = 0;
    int recreatesWhileInactive = 0;
    std::vector<Extent> built;

    explicit Screen(SwapchainRules rules) : policy(rules) {}

    // One RunFrame's check.
    SwapDecision Frame()
    {
        frames++;
        SurfaceState s;
        s.active = active;
        s.swapchain = swapchain;
        // Read whenever asked, active or not: holding off is the policy's.
        if (policy.WantsSurface())
        {
            s.surfaceRead = true;
            s.surface = surface;
            s.interface = interface;
            s.landscapeOnly = landscapeOnly;
            s.viewFitsWindow = viewFits;
        }
        SwapDecision d = policy.Decide(s);
        if (d.action == SwapAction::Recreate)
        {
            recreates++;
            if (!active)
                recreatesWhileInactive++;
            swapchain = d.extent;
            built.push_back(d.extent);
            if (!swapchain.Empty())
                policy.Created(swapchain);
        }
        else if (d.action == SwapAction::RepairView)
        {
            repairs++;
            if (repairWorks)
            {
                // SDL lays the view out again: its drawables and resize events.
                viewFits = true;
                surface = windowPixels;
                policy.SizeChanged();
            }
        }
        return d;
    }

    // The frame's present (none while inactive): MoltenVK says SUBOPTIMAL
    // while the layer and the swapchain disagree, for the next frame's check.
    void Present()
    {
        if (active && !swapchain.Empty() && surface != swapchain)
            policy.Reported(SurfaceResult::Suboptimal);
    }

    void Run(int n)
    {
        for (int i = 0; i < n; i++)
        {
            Frame();
            Present();
        }
    }

    bool BuiltPortrait() const
    {
        for (const Extent& e : built)
            if (e.Portrait())
                return true;
        return false;
    }

    void Suspend()
    {
        policy.Suspended();
        active = false;
    }

    void Resume()
    {
        active = true;
        policy.Resumed();
    }
};

// The v0.1.1 bug on the iPhone: after a screenshot, SDL turns its view
// portrait (440x956 points in a 956x440 window) as a keyboard hides.
static void IphoneViewTurned(const SwapchainRules& rules)
{
    Screen s(rules);
    s.landscapeOnly = true;
    s.Run(10);
    s.Suspend();
    s.Run(30);
    s.Resume();
    s.Run(5);
    // SDL's -updateKeyboard: the view's frame swapped.
    s.surface = kPortrait;
    s.viewFits = false;
    s.policy.SizeChanged();
    s.Run(200);
    CHECK(!s.BuiltPortrait(), "iPhone, the view turned: a portrait swapchain was built");
    CHECK(s.repairs >= 1, "iPhone, the view turned: never fitted back");
    CHECK(s.swapchain == kLandscape && s.surface == kLandscape, "iPhone, the view turned: ends at swapchain %ux%u, layer %ux%u",
        s.swapchain.width, s.swapchain.height, s.surface.width, s.surface.height);
    CHECK(s.recreates == 0, "iPhone, the view turned: %d rebuilds (none needed: the size never really changed)", s.recreates);
}

// The layer is portrait though the view fills its window (the window itself
// turned under a landscape interface), and fitting back doesn't take: the
// swapchain stays landscape for as long as it lasts, repairs spaced out.
static void IphoneLayerPortraitStays(const SwapchainRules& rules)
{
    Screen s(rules);
    s.landscapeOnly = true;
    s.repairWorks = false;
    s.Run(10);
    s.surface = kPortrait;
    s.policy.SizeChanged();
    s.Run(300);
    CHECK(!s.BuiltPortrait(), "iPhone, a portrait layer: a portrait swapchain was built under a landscape interface");
    CHECK(s.swapchain == kLandscape, "iPhone, a portrait layer: swapchain %ux%u", s.swapchain.width, s.swapchain.height);
    CHECK(s.repairs >= 1 && s.repairs <= 20, "iPhone, a portrait layer: %d repairs in 300 frames (want some, spaced out)", s.repairs);
    CHECK(s.recreates <= 1, "iPhone, a portrait layer: %d rebuilds", s.recreates);
}

// The interface really turned portrait (not BurnoutRevenge on iPhone, but
// any landscape-only app iOS forces): followed.
static void IphoneInterfacePortrait(const SwapchainRules& rules)
{
    Screen s(rules);
    s.landscapeOnly = true;
    s.Run(10);
    s.interface = Orientation::Portrait;
    s.surface = s.windowPixels = kPortrait;
    s.policy.SizeChanged();
    s.Run(3);
    CHECK(s.swapchain == kPortrait, "a portrait interface: swapchain %ux%u, want the portrait surface", s.swapchain.width,
        s.swapchain.height);
}

// iPad (not landscape-only): SDL's turned view is fitted back before the
// swapchain follows the layer anywhere.
static void IpadViewTurned(const SwapchainRules& rules)
{
    Screen s(rules);
    s.swapchain = s.surface = s.windowPixels = { 2732, 2048 };
    s.Run(10);
    s.surface = { 2048, 2732 };
    s.viewFits = false;
    s.policy.SizeChanged();
    s.Run(60);
    CHECK(!s.BuiltPortrait(), "iPad, the view turned: the swapchain followed SDL's portrait guess");
    CHECK(s.swapchain == Extent(2732, 2048) && s.repairs >= 1, "iPad, the view turned: swapchain %ux%u, %d repairs", s.swapchain.width,
        s.swapchain.height, s.repairs);
}

// A view that won't fit: after the patience the swapchain follows its layer
// (better a picture that fills the layer than one stretched into it).
static void ViewNeverFits(const SwapchainRules& tested)
{
    const SwapchainRules rules;  // the expected patience, not the one under test
    Screen s(tested);
    s.swapchain = s.surface = s.windowPixels = { 2732, 2048 };
    s.repairWorks = false;
    s.Run(5);
    s.surface = { 2048, 2732 };
    s.viewFits = false;
    s.policy.SizeChanged();
    s.Run(rules.repairPatience + 20);
    CHECK(s.swapchain == Extent(2048, 2732), "a view that never fits: swapchain %ux%u after %u frames", s.swapchain.width,
        s.swapchain.height, rules.repairPatience + 20);
    CHECK(s.repairs >= 3 && s.repairs <= 3 + int(rules.repairPatience / std::max(rules.repairEvery, 1u)) + 1,
        "a view that never fits: %d repairs", s.repairs);
}

// iOS snapshots the app turned while it is inactive (the app switcher, the
// screenshot editor), and turns it back before it is active: nothing built.
static void SnapshotWhileInactive(const SwapchainRules& rules)
{
    Screen s(rules);
    s.Run(10);
    s.Suspend();
    s.Run(5);
    s.surface = kPortrait;
    s.policy.SizeChanged();
    s.Run(30);
    s.surface = kLandscape;
    s.policy.SizeChanged();
    s.Run(5);
    s.Resume();
    s.Run(10);
    CHECK(s.recreatesWhileInactive == 0, "a snapshot while inactive: %d rebuilds while inactive", s.recreatesWhileInactive);
    CHECK(s.recreates == 0, "a snapshot while inactive: %d rebuilds (the size is the same again)", s.recreates);
}

// A real change while inactive: built on the first active frame, not before.
static void ResizeWhileInactive(const SwapchainRules& rules)
{
    Screen s(rules);
    s.swapchain = s.surface = { 1280, 720 };
    s.Run(10);
    s.Suspend();
    s.surface = { 1600, 900 };
    s.policy.SizeChanged();
    s.Run(20);
    CHECK(s.recreatesWhileInactive == 0, "a resize while inactive: %d rebuilds while inactive", s.recreatesWhileInactive);
    s.Resume();
    s.Frame();
    CHECK(s.swapchain == Extent(1600, 900), "a resize while inactive: first active frame's swapchain %ux%u", s.swapchain.width,
        s.swapchain.height);
}

// After a resume the layer settles late, with no event: caught that frame.
static void LateSettleAfterResume(const SwapchainRules& rules)
{
    Screen s(rules);
    s.Run(500);  // off the once-a-second beat below
    s.Suspend();
    s.Run(10);
    s.Resume();
    for (int i = 0; i < 40; i++)
        s.Frame();
    s.surface = { 2868, 1200 };
    SwapDecision d = s.Frame();
    CHECK(d.action == SwapAction::Recreate && s.swapchain == Extent(2868, 1200),
        "after a resume, the layer moved without an event: %s, swapchain %ux%u", SwapActionName(d.action), s.swapchain.width,
        s.swapchain.height);
}

// Long after anything happened, the layer moves with no event and no
// SUBOPTIMAL (a driver that doesn't say): caught within a second.
static void PeriodicCheck(const SwapchainRules& rules)
{
    Screen s(rules);
    for (int i = 0; i < 1000; i++)
        s.Frame();
    s.surface = { 2560, 1440 };
    int caughtAfter = -1;
    for (int i = 0; i < 200 && caughtAfter < 0; i++)
        if (s.Frame().action == SwapAction::Recreate)
            caughtAfter = i;
    CHECK(caughtAfter >= 0 && caughtAfter < 61, "an unannounced size: caught after %d frames (want within 60)", caughtAfter);
}

// MoltenVK's SUBOPTIMAL at the size the swapchain already has (drawables
// rounded another way): rebuilt, but not every frame.
static void SuboptimalSameSize(const SwapchainRules& rules)
{
    Screen s(rules);
    s.Run(200);
    for (int i = 0; i < 90; i++)
    {
        s.policy.Reported(SurfaceResult::Suboptimal);
        s.Frame();
    }
    CHECK(s.recreates >= 2 && s.recreates <= 4, "SUBOPTIMAL at the same size for 90 frames: %d rebuilds (want 2-4)", s.recreates);
}

// SUBOPTIMAL from a new size, no event, between checks: rebuilt at once.
static void SuboptimalNewSize(const SwapchainRules& rules)
{
    Screen s(rules);
    s.Run(500);
    s.surface = { 2400, 1320 };
    s.policy.Reported(SurfaceResult::Suboptimal);
    SwapDecision d = s.Frame();
    CHECK(d.action == SwapAction::Recreate && s.swapchain == Extent(2400, 1320), "SUBOPTIMAL at a new size: %s, swapchain %ux%u",
        SwapActionName(d.action), s.swapchain.width, s.swapchain.height);
}

// OUT_OF_DATE: rebuilt whatever the size; on a landscape-only iPhone still
// not portrait.
static void OutOfDate(const SwapchainRules& rules)
{
    Screen s(rules);
    s.Run(500);
    s.policy.Reported(SurfaceResult::OutOfDate);
    SwapDecision d = s.Frame();
    CHECK(d.action == SwapAction::Recreate && s.swapchain == kLandscape, "OUT_OF_DATE, same size: %s", SwapActionName(d.action));

    Screen p(rules);
    p.landscapeOnly = true;
    p.repairWorks = false;
    p.Run(10);
    p.surface = kPortrait;
    p.policy.Reported(SurfaceResult::OutOfDate);
    d = p.Frame();
    CHECK(d.action == SwapAction::Recreate && p.swapchain == kLandscape, "OUT_OF_DATE on a portrait layer (iPhone): %s, swapchain %ux%u",
        SwapActionName(d.action), p.swapchain.width, p.swapchain.height);
}

// The Mac and Linux: window resizes, a fullscreen toggle, a Space change
// (each a pixel-size event, built that frame); an event with no change
// builds nothing; minimised tears down and comes back.
static void Desktop(const SwapchainRules& rules)
{
    Screen s(rules);
    s.swapchain = s.surface = { 2560, 1440 };
    s.Run(100);
    const Extent sizes[] = { { 2000, 1200 }, { 1800, 1100 }, { 3456, 2234 }, { 2560, 1440 }, { 3024, 1964 } };
    for (const Extent& e : sizes)
    {
        s.surface = e;
        s.policy.SizeChanged();
        SwapDecision d = s.Frame();
        CHECK(d.action == SwapAction::Recreate && s.swapchain == e, "desktop resize to %ux%u: %s, swapchain %ux%u", e.width, e.height,
            SwapActionName(d.action), s.swapchain.width, s.swapchain.height);
        s.Run(3);
    }
    const int before = s.recreates;
    s.policy.SizeChanged();  // focus, a move: the same size
    s.Run(10);
    CHECK(s.recreates == before, "a size event at the same size: %d rebuilds", s.recreates - before);
    // Minimised: the presenter's wait for a size, as before.
    s.surface = {};
    s.policy.SizeChanged();
    SwapDecision d = s.Frame();
    CHECK(d.action == SwapAction::Recreate && s.swapchain.Empty(), "minimised: %s, swapchain %ux%u", SwapActionName(d.action),
        s.swapchain.width, s.swapchain.height);
    s.surface = { 3024, 1964 };
    s.policy.SizeChanged();
    d = s.Frame();
    CHECK(d.action == SwapAction::Recreate && s.swapchain == Extent(3024, 1964), "restored: %s, swapchain %ux%u",
        SwapActionName(d.action), s.swapchain.width, s.swapchain.height);
    CHECK(!s.BuiltPortrait(), "desktop: a portrait swapchain");
}

// A tall desktop window is portrait, and that's fine there.
static void DesktopPortraitWindow(const SwapchainRules& rules)
{
    Screen s(rules);
    s.swapchain = s.surface = { 1280, 720 };
    s.Run(10);
    s.surface = { 800, 1200 };
    s.policy.SizeChanged();
    s.Frame();
    CHECK(s.swapchain == Extent(800, 1200), "a tall desktop window: swapchain %ux%u", s.swapchain.width, s.swapchain.height);
}

// The settled report: once, at the end of the watch after a resume.
static void SettledReport(const SwapchainRules& tested)
{
    const SwapchainRules rules;  // when it is expected
    Screen s(tested);
    s.Run(10);
    s.Suspend();
    s.Run(3);
    s.Resume();
    int settled = 0, at = -1;
    for (int i = 0; i < 400; i++)
        if (s.Frame().settled)
        {
            settled++;
            at = i;
        }
    CHECK(settled == 1 && at == int(rules.settleFrames) - 1, "after a resume: %d settled reports (frame %d)", settled, at);
}

static const std::vector<std::pair<const char*, void (*)(const SwapchainRules&)>> kScenarios = {
    { "iPhone, the view turned", IphoneViewTurned },
    { "iPhone, a portrait layer stays", IphoneLayerPortraitStays },
    { "iPhone, a portrait interface", IphoneInterfacePortrait },
    { "iPad, the view turned", IpadViewTurned },
    { "a view that never fits", ViewNeverFits },
    { "a snapshot while inactive", SnapshotWhileInactive },
    { "a resize while inactive", ResizeWhileInactive },
    { "late settle after a resume", LateSettleAfterResume },
    { "the periodic check", PeriodicCheck },
    { "SUBOPTIMAL, same size", SuboptimalSameSize },
    { "SUBOPTIMAL, new size", SuboptimalNewSize },
    { "OUT_OF_DATE", OutOfDate },
    { "desktop", Desktop },
    { "a tall desktop window", DesktopPortraitWindow },
    { "the settled report", SettledReport },
};

static int RunScenarios(const SwapchainRules& rules)
{
    int failed = 0;
    for (const auto& [name, run] : kScenarios)
    {
        const int before = g_failures;
        run(rules);
        if (g_failures != before)
        {
            failed++;
            if (!g_quiet)
                printf("  scenario \"%s\" failed\n", name);
        }
    }
    return failed;
}

// The same scenarios under a wrong rule must fail at least one.
static void Planted(const char* what, const std::function<void(SwapchainRules&)>& plant)
{
    SwapchainRules wrong;
    plant(wrong);
    const int saved = g_failures;
    g_quiet = true;
    const int failed = RunScenarios(wrong);
    g_quiet = false;
    g_failures = saved;
    printf("planted %-36s %d scenario(s) fail\n", what, failed);
    CHECK(failed > 0, "planted %s: every scenario still passes", what);
}

static void Choose(Extent surface, Orientation interface, bool landscapeOnly, Extent want, bool refused)
{
    ExtentChoice c = ChooseExtent(surface, interface, landscapeOnly);
    CHECK(c.extent == want && c.refused == refused, "ChooseExtent(%ux%u, %s, %s): %ux%u%s, want %ux%u%s", surface.width, surface.height,
        OrientationName(interface), landscapeOnly ? "landscape-only" : "any", c.extent.width, c.extent.height, c.refused ? " refused" : "",
        want.width, want.height, refused ? " refused" : "");
}

static void Ui(float ww, float wh, Extent fb, float wantW, float wantH, float wantScale, bool fromSwapchain)
{
    UiDisplay u = FitUiDisplay(ww, wh, fb);
    printf("UI for a %.0fx%.0f window, swapchain %ux%u: %.1fx%.1f points, scale %.3f x %.3f%s\n", ww, wh, fb.width, fb.height, u.width,
        u.height, u.scaleX, u.scaleY, u.fromSwapchain ? " (the swapchain's)" : "");
    CHECK(std::fabs(u.width - wantW) < 0.6f && std::fabs(u.height - wantH) < 0.6f && u.fromSwapchain == fromSwapchain,
        "%.0fx%.0f window, %ux%u: %.1fx%.1f%s", ww, wh, fb.width, fb.height, u.width, u.height, u.fromSwapchain ? " from the swapchain" : "");
    CHECK(std::fabs(u.scaleX - wantScale) < 0.01f && std::fabs(u.scaleY - wantScale) < 0.01f, "scale %.3f x %.3f, want %.3f", u.scaleX,
        u.scaleY, wantScale);
    // The UI's own scale follows the display's height (ui.cpp UpdateScale).
}

int main()
{
    printf("ChooseExtent:\n");
    Choose(kLandscape, Orientation::Landscape, true, kLandscape, false);
    Choose(kPortrait, Orientation::Landscape, true, kLandscape, true);
    Choose(kPortrait, Orientation::Unknown, true, kLandscape, true);
    Choose(kPortrait, Orientation::Portrait, true, kPortrait, false);
    Choose(kPortrait, Orientation::Landscape, false, kPortrait, false);
    Choose({ 1320, 1320 }, Orientation::Landscape, true, { 1320, 1320 }, false);
    Choose({}, Orientation::Landscape, true, {}, false);

    printf("\nscenarios:\n");
    const int failed = RunScenarios(SwapchainRules{});
    printf("%zu scenarios, %d failed\n", kScenarios.size(), failed);

    printf("\nplanted (each must fail a scenario):\n");
    Planted("holdWhileInactive off", [](SwapchainRules& r) { r.holdWhileInactive = false; });
    Planted("refusePortrait off", [](SwapchainRules& r) { r.refusePortrait = false; });
    Planted("repairView off", [](SwapchainRules& r) { r.repairView = false; });
    Planted("followSuboptimal off", [](SwapchainRules& r) { r.followSuboptimal = false; });
    Planted("settleFrames 0", [](SwapchainRules& r) { r.settleFrames = 0; });
    Planted("settleFrames 20", [](SwapchainRules& r) { r.settleFrames = 20; });
    Planted("periodFrames 0 (never)", [](SwapchainRules& r) { r.periodFrames = 0; });
    Planted("periodFrames 120", [](SwapchainRules& r) { r.periodFrames = 120; });
    Planted("suboptimalBackoff 0", [](SwapchainRules& r) { r.suboptimalBackoff = 0; });
    Planted("suboptimalBackoff 100", [](SwapchainRules& r) { r.suboptimalBackoff = 100; });
    Planted("repairEvery 1", [](SwapchainRules& r) { r.repairEvery = 1; });
    Planted("repairPatience 100000", [](SwapchainRules& r) { r.repairPatience = 100000; });

    printf("\nUI display:\n");
    Ui(956, 440, kLandscape, 956, 440, 3.0f, false);           // iPhone 18 Pro Max, as it should be
    Ui(440, 956, kLandscape, 956, 440, 3.0f, true);            // the window turned, the swapchain kept landscape
    Ui(440, 956, kPortrait, 440, 956, 3.0f, false);            // both portrait (v0.1.1's state: consistent, if wrong)
    Ui(956, 440, { 2150, 989 }, 956, 440, 2.249f, false);      // Screen Resolution 75%: drawables a hair under 2.25x
    Ui(1512, 982, { 3024, 1964 }, 1512, 982, 2.0f, false);     // a MacBook Pro's Retina window
    Ui(1280, 800, { 1280, 800 }, 1280, 800, 1.0f, false);      // the Steam Deck
    Ui(1512, 982, { 3020, 1964 }, 1512, 982, 1.997f, false);   // mid-resize, a few pixels behind: the window's size stands
    Ui(1512, 982, {}, 1512, 982, 1.0f, false);                 // no swapchain yet

    printf(g_failures ? "\n%d FAILED\n" : "\nall passed\n", g_failures);
    return g_failures ? 1 : 0;
}
