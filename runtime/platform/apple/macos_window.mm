// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See ../macos_window.h.
//
// macOS only (AppKit); built for iOS too, as every platform/apple/*.mm is,
// where it is empty. Manual reference counting like the rest of the runtime.
#include <TargetConditionals.h>

#if TARGET_OS_OSX
#include <platform/macos_window.h>

#import <AppKit/AppKit.h>
#import <GameController/GameController.h>
#include <cmath>
#include <SDL3/SDL.h>
#include <ui/ui.h>
#include <cstdio>
#include <utility>

// The app menu's Hide (Cmd+H), redirected here (InstallHide).
@interface BRHideHandler : NSObject
- (void)hideGame:(id)sender;
@end

// SDL creates the app menu's Settings item without an action. Adapted from
// SpeedBreaker PR #47 (SOHAM4884), including v0.2.0's key-repeat guard.
@interface BRSettingsHandler : NSObject
- (void)openSettings:(id)sender;
@end

@implementation BRSettingsHandler
- (void)openSettings:(id)sender
{
    NSEvent* event = NSApp.currentEvent;
    if (event.type == NSEventTypeKeyDown && event.isARepeat)
        return;
    ui::ToggleSettings();
}

- (BOOL)validateMenuItem:(NSMenuItem*)item
{
    return ui::Ready() && !ui::HasModal();
}
@end

namespace platform::macos
{
    namespace
    {
        SDL_Window* s_sdlWindow = nullptr;
        bool s_hideAfterLeave = false;  // Hide asked while fullscreen: leaving it first
        bool s_returnFullscreen = false;  // hidden from fullscreen: back to it on return
        BRHideHandler* s_hideHandler = nil;
    }
}

@implementation BRHideHandler
- (void)hideGame:(id)sender
{
    using namespace platform::macos;
    NSWindow* w = s_sdlWindow ? (__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(s_sdlWindow),
        SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr) : nil;
    if (w && (w.styleMask & NSWindowStyleMaskFullScreen))
    {
        fprintf(stderr, "[video] Cmd+H while fullscreen: leaving fullscreen, then hiding\n");
        s_hideAfterLeave = true;
        SDL_SetWindowFullscreen(s_sdlWindow, false);
        return;
    }
    [NSApp hide:sender];
}
@end

namespace platform::macos
{
    namespace
    {
        NSWindow* s_window = nil;      // the game's (SDL's, not retained here)
        bool s_inTransition = false;   // between a fullscreen transition's "will" and its "did" (or "did fail")
        NSMutableArray* s_observers = nil;
        BRSettingsHandler* s_settingsHandler = nil;

        NSWindow* CocoaWindow(SDL_Window* window)
        {
            if (!window)
                return nil;
            return (__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
                nullptr);
        }

        // No title, a transparent title bar, the picture under it.
        void ApplyLook(NSWindow* window)
        {
            window.titleVisibility = NSWindowTitleHidden;
            window.titlebarAppearsTransparent = YES;
            window.styleMask |= NSWindowStyleMaskFullSizeContentView;
        }

        // Points every Hide item of the menus (the app menu's, Cmd+H) at
        // BRHideHandler; Hide Others and the rest are left alone.
        void InstallHide(NSMenu* menu)
        {
            for (NSMenuItem* item in menu.itemArray)
            {
                if (item.action == @selector(hide:))
                {
                    item.target = s_hideHandler;
                    item.action = @selector(hideGame:);
                }
                if (item.submenu)
                    InstallHide(item.submenu);
            }
        }

        void Observe(NSWindow* window, NSString* name, void (^block)(NSNotification*))
        {
            id token = [NSNotificationCenter.defaultCenter addObserverForName:name object:window queue:nil usingBlock:block];
            [s_observers addObject:token];
        }

        void WireSettingsMenu()
        {
            NSMenu* appMenu = NSApp.mainMenu.numberOfItems ? [NSApp.mainMenu itemAtIndex:0].submenu : nil;
            for (NSMenuItem* item in appMenu.itemArray)
                if ([item.keyEquivalent isEqualToString:@","] &&
                    (item.keyEquivalentModifierMask & NSEventModifierFlagCommand))
                {
                    if (!s_settingsHandler)
                        s_settingsHandler = [[BRSettingsHandler alloc] init];
                    item.target = s_settingsHandler;
                    item.action = @selector(openSettings:);
                    return;
                }
        }
    }

    void ConfigureWindow(SDL_Window* window, bool windowed)
    {
        NSWindow* w = CocoaWindow(window);
        if (!w)
            return;
        if (s_window != w)
        {
            ForgetWindow(nullptr);
            s_window = w;
            s_observers = [[NSMutableArray alloc] init];
            // Whether a fullscreen transition is under way (AppKit says
            // nothing else): the look is never changed in the middle of one.
            for (NSString* name in @[ NSWindowWillEnterFullScreenNotification, NSWindowWillExitFullScreenNotification ])
                Observe(w, name, ^(NSNotification*) { s_inTransition = true; });
            for (NSString* name in @[ NSWindowDidEnterFullScreenNotification, NSWindowDidExitFullScreenNotification,
                     @"NSWindowDidFailToEnterFullScreenNotification", @"NSWindowDidFailToExitFullScreenNotification" ])
                Observe(w, name, ^(NSNotification*) { s_inTransition = false; });
            // SDL takes the full-size content view off as a fullscreen
            // transition starts (its windowWillEnterFullScreen), but its
            // timing against AppKit's own steps varies, and the bit changing
            // after AppKit has set its fullscreen frame crashed AppKit. So it
            // goes now, as AppKit announces the transition. (In testing SDL
            // had always taken it off already; this is the safety net.)
            Observe(w, NSWindowWillEnterFullScreenNotification, ^(NSNotification* n) {
                NSWindow* nw = n.object;
                if (nw.styleMask & NSWindowStyleMaskFullSizeContentView)
                    nw.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
            });
        }
        if (windowed)
            ApplyLook(w);
        s_sdlWindow = window;
        if (!s_hideHandler)
            s_hideHandler = [[BRHideHandler alloc] init];
        InstallHide(NSApp.mainMenu);
        WireSettingsMenu();
    }

    bool HideAfterLeavingFullscreen()
    {
        if (!s_hideAfterLeave)
            return false;
        s_hideAfterLeave = false;
        s_returnFullscreen = true;
        [NSApp hide:nil];
        return true;
    }

    bool ReturnToFullscreen()
    {
        return std::exchange(s_returnFullscreen, false);
    }

    void HideGame()
    {
        if (!s_hideHandler)
            s_hideHandler = [[BRHideHandler alloc] init];
        [s_hideHandler hideGame:nil];
    }

    void UnhideGame()
    {
        [NSApp unhide:nil];
        [NSApp activate];
    }

    void SetControllerHomePriority(bool focused)
    {
        @autoreleasepool
        {
            unsigned exposed = 0;
            for (GCController* controller in [GCController controllers])
            {
                GCControllerButtonInput* home = controller.physicalInputProfile.buttons[GCInputButtonHome];
                if (!home) continue;
                ++exposed;
                // This is a preference of this process, not a change to the
                // user's Game Center/Games shortcut. macOS can override it.
                home.preferredSystemGestureState = focused ? GCSystemGestureStateDisabled : GCSystemGestureStateEnabled;
            }
            fprintf(stderr, "[input] macOS Home priority: %s (%u exposed buttons; system policy may override)\n",
                focused ? "game quit confirmation" : "system gestures", exposed);
        }
    }

    bool RestoreAfterFullscreen(SDL_Window* window)
    {
        NSWindow* w = CocoaWindow(window);
        if (!w || s_inTransition || (w.styleMask & NSWindowStyleMaskFullScreen))
            return false;
        // AppKit has put back the frame the window had before fullscreen;
        // -setStyleMask: would keep the content (one title bar shorter
        // without the bit) and shrink the frame to it, so the frame is set
        // back: the content is the whole frame again, as before fullscreen.
        const NSRect frame = w.frame;
        ApplyLook(w);
        [w setFrame:frame display:YES];
        return true;
    }

    void ShowWindowButtons(SDL_Window* window, bool shown)
    {
        NSWindow* w = CocoaWindow(window);
        if (!w)
            return;
        const NSWindowButton buttons[] = { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton };
        for (NSWindowButton b : buttons)
        {
            NSButton* button = [w standardWindowButton:b];
            if (button && button.hidden == shown)
                button.hidden = !shown;
        }
    }

    float TitleStripHeight(SDL_Window* window)
    {
        NSWindow* w = CocoaWindow(window);
        if (!w || (w.styleMask & NSWindowStyleMaskFullScreen) || !(w.styleMask & NSWindowStyleMaskFullSizeContentView))
            return 0.0f;
        // The layout rect is the part of the window no title bar covers.
        const CGFloat strip = NSHeight(w.frame) - NSMaxY(w.contentLayoutRect);
        return strip > 0 ? float(strip) : 0.0f;
    }

    bool DisplayRefreshFixed(SDL_Window* window, double* minHz, double* maxHz)
    {
        NSWindow* w = CocoaWindow(window);
        NSScreen* screen = w ? w.screen : nil;
        if (!screen)
            return false;
        const NSTimeInterval shortest = screen.minimumRefreshInterval, longest = screen.maximumRefreshInterval;
        if (minHz)
            *minHz = longest > 0 ? 1.0 / longest : 0.0;
        if (maxHz)
            *maxHz = shortest > 0 ? 1.0 / shortest : 0.0;
        NSNumber* number = screen.deviceDescription[@"NSScreenNumber"];
        if (number && CGDisplayIsBuiltin(CGDirectDisplayID(number.unsignedIntValue)))
            return false;
        return shortest > 0 && longest > 0 && std::abs(longest - shortest) <= 0.01 * shortest;
    }

    void ForgetWindow(SDL_Window*)
    {
        for (id token in s_observers)
            [NSNotificationCenter.defaultCenter removeObserver:token];
        [s_observers release];
        s_observers = nil;
        s_window = nil;
        s_sdlWindow = nullptr;
        s_inTransition = false;
        s_hideAfterLeave = s_returnFullscreen = false;
    }

    bool ReduceMotion()
    {
        return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
    }
}
#endif
