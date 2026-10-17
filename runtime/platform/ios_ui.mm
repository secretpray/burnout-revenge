// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See ios_ui.h.
//
// Manual reference counting (the runtime is built without -fobjc-arc),
// though nothing here is allocated: SDL's own view controller and view only
// gain methods.
//
// SDL 3.4.16 and the first responder (src/video/uikit):
//   - Its view (SDL_uikitview; with MoltenVK the SDL_uikitmetalview that
//     SDL_Metal_CreateView puts in its place) and its view controller define
//     neither -canBecomeFirstResponder nor -editingInteractionConfiguration,
//     so class_addMethod adds them rather than replacing anything of SDL's.
//   - The one first responder SDL makes is its hidden text field, a subview
//     of the view, while text input is on: SDL_StartTextInput makes it first
//     responder (taking it from the view, which is fine) and
//     SDL_StopTextInput resigns it, leaving none. So the view is never made
//     first responder while text input is on, and is made it again after.
//   - With the view first responder, key presses reach SDL's -pressesBegan
//     and the controller's -keyCommands (arrows, Esc). SDL turns presses into
//     keys only while it knows no keyboard (GCKeyboard tells it of one as it
//     connects, and then reports the keys itself), and a controller's presses
//     (iOS 26) only while no controller is open. The key commands send keys
//     that release themselves; SDL drops a second source's press of a key
//     that is already down (SDL_keyboard.c), so with GCKeyboard an arrow
//     press still comes through once.
#include "ios_ui.h"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>
#import <objc/runtime.h>

#include <report/report.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace
{
    UIEditingInteractionConfiguration NoEditingInteractions(id, SEL)
    {
        return UIEditingInteractionConfigurationNone;
    }

    BOOL CanBecomeFirstResponder(id, SEL)
    {
        return YES;
    }

    // Gives `cls` its own `sel` (UIResponder's, overridden): `imp`. A class
    // that already has its own (a later SDL's) keeps it, and the log says so.
    void Override(Class cls, SEL sel, IMP imp)
    {
        if (class_getMethodImplementation(cls, sel) == imp)
            return;  // done already (for this class or one it inherits from)
        Method inherited = class_getInstanceMethod([UIResponder class], sel);
        if (!inherited)
            return;  // (an iOS that doesn't ask)
        if (!class_addMethod(cls, sel, imp, method_getTypeEncoding(inherited)))
            fprintf(stderr, "[ui] %s has its own -%s: left as it is\n", class_getName(cls), sel_getName(sel));
    }

    // The classes given the methods (a new view may be another class), and
    // how much has been logged: a missing view once, the first try, then up
    // to kRetakesLogged times taking first responder back.
    Class s_controllerClass = nil, s_viewClass = nil;
    bool s_noViewLogged = false, s_logged = false;
    int s_retakes = 0;
    constexpr int kRetakesLogged = 10;

    UIWindow* UiWindow(SDL_Window* window)
    {
        return window ? static_cast<UIWindow*>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr)) : nil;
    }

    const char* InterfaceName(UIInterfaceOrientation o)
    {
        switch (o)
        {
        case UIInterfaceOrientationPortrait:
            return "portrait";
        case UIInterfaceOrientationPortraitUpsideDown:
            return "portrait upside down";
        case UIInterfaceOrientationLandscapeLeft:
            return "landscape left";
        case UIInterfaceOrientationLandscapeRight:
            return "landscape right";
        default:
            return "unknown";
        }
    }

    bool SameSize(CGSize a, CGSize b)
    {
        return std::fabs(a.width - b.width) < 0.5 && std::fabs(a.height - b.height) < 0.5;
    }

    // Keyboard notifications reach SDL's view controller, which sets the
    // view's frame on each (see ios_ui.h): the log says when, and whose.
    int s_keyboardNotes = 0;
    constexpr int kKeyboardNotesLogged = 20;
    SDL_Window* s_window = nullptr;  // the game's, once it exists (FitViewToWindow)
}

namespace platform::ios
{
    void DisableSystemEditGestures(SDL_Window* window)
    {
        @autoreleasepool
        {
            UIWindow* uiWindow = window ? static_cast<UIWindow*>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr)) : nil;
            UIViewController* root = uiWindow.rootViewController;
            UIView* view = root.viewIfLoaded;
            if (!view)
            {
                if (!s_noViewLogged)
                    fprintf(stderr, "[ui] no view to turn iOS's editing gestures off on: a three-finger tap may not reach the game\n");
                s_noViewLogged = true;
                return;
            }
            if ([root class] != s_controllerClass)
            {
                s_controllerClass = [root class];
                Override(s_controllerClass, @selector(editingInteractionConfiguration), reinterpret_cast<IMP>(&NoEditingInteractions));
            }
            if ([view class] != s_viewClass)
            {
                s_viewClass = [view class];
                Override(s_viewClass, @selector(editingInteractionConfiguration), reinterpret_cast<IMP>(&NoEditingInteractions));
                Override(s_viewClass, @selector(canBecomeFirstResponder), reinterpret_cast<IMP>(&CanBecomeFirstResponder));
            }

            // Already the first responder, or something else has it for now:
            // SDL's hidden text field while text input is on, or a sheet
            // presented over the game (the Files picker), whose own text
            // fields it mustn't be taken from.
            if ([view isFirstResponder] || SDL_TextInputActive(window) || root.presentedViewController || !view.window)
                return;
            const bool taken = [view becomeFirstResponder];
            if (!s_logged)
                fprintf(stderr, taken ? "[ui] iOS's three-finger editing gestures are off (the game's view is the first responder)\n"
                                      : "[ui] the game's view can't be the first responder: iOS may take three-finger taps for editing\n");
            else if (s_retakes < kRetakesLogged)
            {
                s_retakes++;
                fprintf(stderr, taken ? "[ui] the game's view is the first responder again (three-finger editing gestures off)\n"
                                      : "[ui] the game's view couldn't be the first responder again\n");
            }
            s_logged = true;
        }
    }

    bool SetDrawableScale(SDL_Window* window, float fraction)
    {
        @autoreleasepool
        {
            UIWindow* uiWindow = window ? static_cast<UIWindow*>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr)) : nil;
            UIView* view = uiWindow.rootViewController.viewIfLoaded;
            if (![view.layer isKindOfClass:[CAMetalLayer class]])
                return false;
            // SDL made it the screen's native scale; its -layoutSubviews
            // sizes the drawables from it (the view's points times it), and
            // MoltenVK's surface extent is the same product. But Core
            // Animation truncates a drawable size and MoltenVK rounds it half
            // to even: a size .5 or more past a whole pixel (an iPhone 15 Pro
            // Max's 430 points at 2.25 are 967.5) was a pixel more to
            // MoltenVK than its drawables, so a VK_SUBOPTIMAL_KHR and a new
            // swapchain after every layout pass. The scale steps down (by at
            // most 0.02) to one that leaves both sizes under .4 past a pixel,
            // for these bounds: snapped again when they or the fraction
            // change (Stage Manager, Split View; Screen Resolution), from the
            // native scale the first call found. The sizes stay fractional,
            // so SDL's -updateDrawableSize (an exact compare with the
            // truncated drawableSize) sets the same drawables again on each
            // layout pass and posts a METAL_VIEW_RESIZED the presenter
            // ignores. SDL's window pixels stay the screen's (its
            // UIKit_GetWindowSizeInPixels uses the screen's native scale),
            // so a change here sends no pixel-size event.
            static CGFloat s_native = 0;
            static CGSize s_points{};
            static float s_fraction = 0;
            const CGSize points = view.bounds.size;
            if (s_native && CGSizeEqualToSize(points, s_points) && fraction == s_fraction)
                return true;
            if (!s_native)
                s_native = view.layer.contentsScale;
            s_points = points;
            s_fraction = fraction;
            const CGFloat native = s_native, wanted = native * fraction;
            auto agree = [](CGFloat pixels) { return pixels - std::floor(pixels) < 0.4; };
            CGFloat scale = wanted;
            for (int i = 0; i <= 2000; i++)
                if (CGFloat s = wanted - i * 1e-5; agree(points.width * s) && agree(points.height * s))
                {
                    scale = s;
                    break;
                }
            view.contentScaleFactor = scale;
            [view setNeedsLayout];
            [view layoutIfNeeded];
            CGSize size = ((CAMetalLayer*)view.layer).drawableSize;
            fprintf(stderr, "[ui] present scale %.3f: Metal view scale %.4f of %.3f, drawables %.0fx%.0f for %.0fx%.0f points\n",
                fraction, double(view.layer.contentsScale), double(native), size.width, size.height, points.width, points.height);
            return true;
        }
    }

    std::string GpuName()
    {
        @autoreleasepool
        {
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();  // +1: released below (no ARC)
            const char* name = device ? device.name.UTF8String : nullptr;
            std::string text = name ? name : "";
            [device release];
            return text;
        }
    }

    bool IsPhone()
    {
        return UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPhone;
    }

    bool LandscapeOnly()
    {
        static const bool only = [] {
            @autoreleasepool
            {
                if (!IsPhone())
                    return false;
                // NSBundle hands over the device's own variant of the key.
                id list = [NSBundle.mainBundle objectForInfoDictionaryKey:@"UISupportedInterfaceOrientations"];
                if (![list isKindOfClass:[NSArray class]] || [(NSArray*)list count] == 0)
                    return false;
                for (id o in (NSArray*)list)
                    if (![o isKindOfClass:[NSString class]] || ![(NSString*)o hasPrefix:@"UIInterfaceOrientationLandscape"])
                        return false;
                return true;
            }
        }();
        return only;
    }

    void PrepareOrientation()
    {
        @autoreleasepool
        {
            if (LandscapeOnly())
            {
                if (const char* set = SDL_GetHint(SDL_HINT_ORIENTATIONS))
                    fprintf(stderr, "[video] orientation: SDL_HINT_ORIENTATIONS is \"%s\" already: left as it is\n", set);
                else
                {
                    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
                    fprintf(stderr, "[video] orientation: landscape only (iPhone): SDL_HINT_ORIENTATIONS = \"LandscapeLeft LandscapeRight\", "
                                    "so SDL never turns its view portrait\n");
                }
            }
        }
    }

    void WatchKeyboards(SDL_Window* window)
    {
        @autoreleasepool
        {
            s_window = window;
            static bool watching = false;
            if (std::exchange(watching, true))
                return;
            for (NSString* name in @[ UIKeyboardWillShowNotification, UIKeyboardWillHideNotification ])
                [NSNotificationCenter.defaultCenter addObserverForName:name object:nil queue:nil usingBlock:^(NSNotification* note) {
                    if (s_keyboardNotes >= kKeyboardNotesLogged)
                        return;
                    s_keyboardNotes++;
                    NSNumber* local = note.userInfo[UIKeyboardIsLocalUserInfoKey];
                    fprintf(stderr, "[ui] a keyboard will %s (%s)%s: %s\n", [note.name isEqualToString:UIKeyboardWillShowNotification] ? "show" : "hide",
                        local == nil ? "whose not said" : local.boolValue ? "this app's" : "another app's",
                        s_keyboardNotes == kKeyboardNotesLogged ? " (no more of these logged)" : "", DescribeGeometry(s_window).c_str());
                }];
        }
    }

    video::Orientation InterfaceOrientation(SDL_Window* window)
    {
        @autoreleasepool
        {
            UIWindow* uiWindow = UiWindow(window);
            if (!uiWindow)
                return video::Orientation::Unknown;
            UIInterfaceOrientation o = uiWindow.windowScene ? uiWindow.windowScene.effectiveGeometry.interfaceOrientation
                                                            : UIInterfaceOrientationUnknown;
            if (UIInterfaceOrientationIsLandscape(o))
                return video::Orientation::Landscape;
            if (UIInterfaceOrientationIsPortrait(o))
                return video::Orientation::Portrait;
            const CGSize size = uiWindow.bounds.size;
            return size.width <= 0 || size.height <= 0 ? video::Orientation::Unknown
                : size.width >= size.height             ? video::Orientation::Landscape
                                                        : video::Orientation::Portrait;
        }
    }

    bool ViewFitsWindow(SDL_Window* window)
    {
        @autoreleasepool
        {
            UIWindow* uiWindow = UiWindow(window);
            UIView* view = uiWindow.rootViewController.viewIfLoaded;
            return !view || SameSize(view.bounds.size, uiWindow.bounds.size);
        }
    }

    bool FitViewToWindow(SDL_Window* window)
    {
        @autoreleasepool
        {
            UIWindow* uiWindow = UiWindow(window);
            UIView* view = uiWindow.rootViewController.viewIfLoaded;
            if (!view)
                return false;
            const CGRect before = view.frame;
            if (SameSize(view.bounds.size, uiWindow.bounds.size))
            {
                fprintf(stderr, "[video] the view already fills its window: %s\n", DescribeGeometry(window).c_str());
                return false;
            }
            view.frame = uiWindow.bounds;
            [view setNeedsLayout];
            [view layoutIfNeeded];
            fprintf(stderr, "[video] the view was %.0fx%.0f points in a %.0fx%.0f window: fitted back to it (%s)\n", before.size.width,
                before.size.height, uiWindow.bounds.size.width, uiWindow.bounds.size.height, DescribeGeometry(window).c_str());
            return true;
        }
    }

    const char* SceneOrientationName(SDL_Window* window)
    {
        @autoreleasepool
        {
            UIWindow* uiWindow = UiWindow(window);
            return InterfaceName(uiWindow.windowScene ? uiWindow.windowScene.effectiveGeometry.interfaceOrientation
                                                      : UIInterfaceOrientationUnknown);
        }
    }

    std::string DescribeGeometry(SDL_Window* window)
    {
        @autoreleasepool
        {
            UIWindow* uiWindow = UiWindow(window);
            UIView* view = uiWindow.rootViewController.viewIfLoaded;
            if (!view)
                return "no view";
            UIInterfaceOrientation scene = uiWindow.windowScene ? uiWindow.windowScene.effectiveGeometry.interfaceOrientation
                                                                : UIInterfaceOrientationUnknown;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            UIInterfaceOrientation bar = UIApplication.sharedApplication.statusBarOrientation;
#pragma clang diagnostic pop
            const char* state = "active";
            switch (UIApplication.sharedApplication.applicationState)
            {
            case UIApplicationStateInactive:
                state = "inactive";
                break;
            case UIApplicationStateBackground:
                state = "in the background";
                break;
            default:
                break;
            }
            CGSize drawables = [view.layer isKindOfClass:[CAMetalLayer class]] ? ((CAMetalLayer*)view.layer).drawableSize : CGSizeZero;
            char text[320];
            snprintf(text, sizeof text,
                "view %.0fx%.0f at %.0f,%.0f in a %.0fx%.0f window; drawables %.0fx%.0f at scale %.3f; scene %s, status bar %s; app %s",
                view.frame.size.width, view.frame.size.height, view.frame.origin.x, view.frame.origin.y, uiWindow.bounds.size.width,
                uiWindow.bounds.size.height, drawables.width, drawables.height, double(view.layer.contentsScale), InterfaceName(scene),
                InterfaceName(bar), state);
            return text;
        }
    }

    void PollBattery()
    {
        static double next = 0.0;
        static int lastPercent = -1;
        static const char* lastState = nullptr;  // nullptr: nothing logged yet
        const double now = report::SessionSeconds();
        if (now < next)
            return;
        next = now + 5.0;
        @autoreleasepool
        {
            UIDevice* device = UIDevice.currentDevice;
            if (!device.batteryMonitoringEnabled)
                device.batteryMonitoringEnabled = YES;  // left on: SDL's power API isn't used
            const float level = device.batteryLevel;   // -1 while unknown
            const int percent = level < 0.0f ? -1 : int(std::lround(double(level) * 100.0));
            const char* state = "unknown";
            switch (device.batteryState)
            {
            case UIDeviceBatteryStateUnplugged: state = "on battery"; break;
            case UIDeviceBatteryStateCharging: state = "charging"; break;
            case UIDeviceBatteryStateFull: state = "full, on a charger"; break;
            default: break;
            }
            if (percent < 0)
                return;  // monitoring just turned on (or no battery): next time
            if (!lastState)
                fprintf(stderr, "[power] battery %d%%, %s, at %.1f s\n", percent, state, now);
            else if (percent != lastPercent || std::strcmp(state, lastState) != 0)
            {
                if (percent != lastPercent)
                    fprintf(stderr, "[power] battery %d%% -> %d%%, %s, at %.1f s\n", lastPercent, percent, state, now);
                else
                    fprintf(stderr, "[power] battery %d%%, %s -> %s, at %.1f s\n", percent, lastState, state, now);
            }
            lastPercent = percent;
            lastState = state;
        }
    }
}
