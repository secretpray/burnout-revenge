// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See ../macos_window.h.
//
// macOS only (AppKit); built for iOS too, as every platform/apple/*.mm is,
// where it is empty. Manual reference counting like the rest of the runtime.
#include <TargetConditionals.h>

#if TARGET_OS_OSX
#include <platform/macos_window.h>

#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>

namespace platform::macos
{
    namespace
    {
        NSWindow* s_window = nil;      // the game's (SDL's, not retained here)
        bool s_inTransition = false;   // between a fullscreen transition's "will" and its "did" (or "did fail")
        NSMutableArray* s_observers = nil;

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

        void Observe(NSWindow* window, NSString* name, void (^block)(NSNotification*))
        {
            id token = [NSNotificationCenter.defaultCenter addObserverForName:name object:window queue:nil usingBlock:block];
            [s_observers addObject:token];
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

    void ForgetWindow(SDL_Window*)
    {
        for (id token in s_observers)
            [NSNotificationCenter.defaultCenter removeObserver:token];
        [s_observers release];
        s_observers = nil;
        s_window = nil;
        s_inTransition = false;
    }
}
#endif
