// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include <TargetConditionals.h>
#if TARGET_OS_OSX && defined(BR_GAME_BURNOUT)
#include <platform/app_identity.h>
#include <cstdio>
#import <AppKit/AppKit.h>

namespace platform
{
    void ApplyBurnoutIcon(const char* executableDirectory)
    {
        @autoreleasepool
        {
            if (!executableDirectory) return;
            // Launching the executable directly can reuse another app's Dock
            // identity. Set the process icon explicitly from this game's bundle.
            NSString* base = [NSString stringWithUTF8String:executableDirectory];
            NSString* path = [base stringByAppendingPathComponent:@"../Resources/BurnoutRevenge.icns"];
            NSImage* icon = [[NSImage alloc] initWithContentsOfFile:path];
            if (icon)
            {
                [NSApp setApplicationIconImage:icon];
                [icon release];
                fprintf(stderr, "[app] Burnout Revenge process icon applied\n");
            }
        }
    }
}
#endif
