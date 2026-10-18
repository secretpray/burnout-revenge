// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Settings > Graphics > Motion Blur at 0%. The patch is boma's for Xenia (its
// Burnout Revenge game patches), as shipa-2's Xerenge applies it: the motion
// and radial blur stop when sub_821436D0's "frames left" counter path that
// returns 0 returns 1 instead. This code is ours. (Bloom is not switched off
// here: emptying its kernel, the other half of that patch set, greys the
// sky. Bloom below 100% keeps the game's bloom in place instead:
// gpu/burnout_bloom.h. Motion Blur between 0 and 100% shortens the streaks
// in the composite: gpu/burnout_blur.h.)
#include <stdafx.h>
#include <user/settings.h>

// sub_821436D0(counter*): a counter at or below 0 returns 0; otherwise the
// counter is stepped down and 1 returned. Read each call: the setting is live.
extern "C" PPC_FUNC(__imp__sub_821436D0);
PPC_FUNC(sub_821436D0)
{
    if (settings::GetInt(settings::Id::MotionBlur) == 0 && int16_t(PPC_LOAD_U16(ctx.r3.u32)) <= 0)
    {
        ctx.r3.u64 = 1;
        return;
    }
    __imp__sub_821436D0(ctx, base);
}
