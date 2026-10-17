// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// iOS: what UIKit needs told for the runtime's own screens (ios_ui.mm).
// Main thread.
#pragma once
#include <string>

#include <video/swapchain_policy.h>

struct SDL_Window;

namespace platform::ios
{
    // Turns iOS's editing gestures off over `window`. Since iOS 13 a
    // three-finger tap brings up the edit menu and a three-finger swipe or
    // double tap is undo/redo, and the system takes those touches from the
    // app (they arrive cancelled), so the three-finger tap that opens the
    // settings would never get through. UIKit asks the first responder,
    // which SDL leaves unset outside text input, whether to: here the game's
    // view becomes it and answers no (its view controller answers no too).
    // Call once the window is up and then every frame (ui::Tick): something
    // else can take first responder for a while (SDL's hidden text field
    // while text input is on, the Files picker), and this takes it back
    // after. Cheap once done: a check that the view still has it.
    void DisableSystemEditGestures(SDL_Window* window);

    // The present scale (video/presenter.cpp: Screen Resolution, or
    // BURNOUT_PRESENT_SCALE): the game's Metal view at `fraction` of the
    // screen's pixels each way (its contentScaleFactor; a hair less where
    // the drawable sizes would round two ways), which Core Animation scales
    // up to the screen. SDL sizes the drawables from it, and MoltenVK's
    // surface extent follows them. Call after SDL_Vulkan_CreateSurface
    // (which makes the view), before each swapchain: it snaps again only
    // when the view's bounds or `fraction` changed (1: the screen's own
    // scale again). False: no Metal view.
    bool SetDrawableScale(SDL_Window* window, float fraction);

    // The default Metal device's name, as MoltenVK names the Vulkan device
    // ("Apple A17 Pro GPU", "Apple M2 GPU"), before Vulkan starts (Frame
    // Rate's default follows it: main.cpp). "" without one. Any thread.
    std::string GpuName();

    // An iPhone (UIDevice's interface idiom), not an iPad: Frame Rate's
    // default (main.cpp). Main thread.
    bool IsPhone();

    // Orientation (video/swapchain_policy.h says why). SDL 3.4.16 sets its
    // view's frame itself when a keyboard shows or hides (-[SDL_uikitview-
    // controller updateKeyboard] -> UIKit_ComputeViewFrame, SDL_uikitvideo.m)
    // and swaps its width and height unless it believes the interface is
    // landscape: UIApplication.statusBarOrientation is landscape (on iOS 27
    // it reads "unknown") or the window can't be portrait. A resizable
    // window can be, to SDL, unless SDL_HINT_ORIENTATIONS says otherwise
    // (UIKit_GetSupportedOrientations ignores the Info.plist then).

    // Before SDL_CreateWindow: on a device where the app is landscape-only
    // (LandscapeOnly), SDL_HINT_ORIENTATIONS = "LandscapeLeft LandscapeRight"
    // (unless set already), so SDL never turns the view portrait. Main thread.
    void PrepareOrientation();

    // Once the window is up: the log notes keyboards showing and hiding, with
    // the view's geometry (the trigger of any turn that gets past the hint),
    // at most 20 times.
    void WatchKeyboards(SDL_Window* window);

    // The app shows only in landscape here: an iPhone, whose Info.plist
    // lists no portrait orientation. (An iPad's window can take any shape.)
    bool LandscapeOnly();

    // The interface's orientation: the window's scene's, else its bounds'
    // shape. Unknown without a window.
    video::Orientation InterfaceOrientation(SDL_Window* window);

    // SDL's view (the Metal view the surface is) fills its UIWindow: the
    // same size (an offset for a keyboard is SDL's to make).
    bool ViewFitsWindow(SDL_Window* window);

    // Fits SDL's view to its UIWindow's bounds again, and lays it out (SDL
    // then sizes the drawables and sends its resize events). True: the view
    // had to be changed. Logs what it did.
    bool FitViewToWindow(SDL_Window* window);

    // The scene's interface orientation by name ("landscape left", ...),
    // for the log: a string literal, so a change shows as another pointer.
    const char* SceneOrientationName(SDL_Window* window);

    // The view, its window and the scene in one line, for the log.
    std::string DescribeGeometry(SDL_Window* window);
    // The battery, for the log: its level and charging state once known
    // ("[power] battery 87%, charging, at 0.4 s"), then each change ("[power]
    // battery 87% -> 86%, on battery, at 312.0 s"), so a session's drain per
    // minute reads off its log beside the [thermal] lines. Main thread, every
    // frame (video::RunFrame): reads UIDevice every 5 seconds at most, with
    // battery monitoring left on from the first call.
    void PollBattery();
}
