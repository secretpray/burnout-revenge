// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#include <stdafx.h>
#include "crash_pacing.h"
#include "crash_launch_scope.h"
#include "rewind_probe.h"
#include "camera.h"
#include <report/report.h>

namespace game::crash_pacing
{
    void Sample(const char* kind, uint8_t* base, uint32_t object, uint32_t caller)
    {
        static FILE* file = [] {
            const char* path = std::getenv("BURNOUT_CRASH_PACING_LOG");
            FILE* f = path && *path ? fopen(path, "w") : nullptr;
            if (f)
                fprintf(f, "kind,steady_ns,session_s,object,caller,phase,event_state,world_s,dt_s,clock_tick,clock_flags,playback,deadline_s,traffic_enabled,rng0,rng1,car_x,car_y,car_z,car_dx,car_dy,car_dz,camera_frame,c0,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,camera_mode,half_requested,half_active,traffic_hash,m0,m1,m2,m3,m4,m5,m6,m7,m8,m9,m10,m11,m12,m13,m14,m15\n");
            return f;
        }();
        if (!file) return;
        constexpr uint32_t world = 0x82AB6F80, event = crash_launch::kSoloEvent, participant = 0x82CE0030;
        // Only the known solo Crash event. Never dereference a foreign session.
        if (PPC_LOAD_U32(codes::kSession) != codes::kSoloSession ||
            PPC_LOAD_U32(codes::kSoloSession + 4) != event || PPC_LOAD_U32(rewind::kPhase) != 5) return;
        if (std::strcmp(kind, "controls") == 0 && object != PPC_LOAD_U32(participant + 18240)) return;
        static std::mutex mutex;
        std::lock_guard lock(mutex);
        static uint32_t rows = 0;
        if (rows++ >= 120000) return; // bound the diagnostic, including a forgotten long run
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        auto f = [base](uint32_t a) { return std::bit_cast<float>(PPC_LOAD_U32(a)); };
        fprintf(file, "%s,%lld,%.9f,%u,%u,%u,%u,%.9g,%.9g,%u,%u,%u,%.9g,%u,%u,%u",
            kind, (long long)ns, report::SessionSeconds(), object, caller, PPC_LOAD_U32(rewind::kPhase),
            PPC_LOAD_U32(event + 2384), f(world + 36), f(world + 32), PPC_LOAD_U32(world + 16),
            PPC_LOAD_U32(rewind::kClockFlags), PPC_LOAD_U8(rewind::kWorldPlayback), f(event + 2772),
            PPC_LOAD_U8(world + 0x4708D), PPC_LOAD_U32(rewind::kGame + 0x33088), PPC_LOAD_U32(rewind::kGame + 0x3308C));
        for (uint32_t offset : {64u,68u,72u,80u,84u,88u}) fprintf(file, ",%.9g", f(participant + offset));
        const uint32_t frame = PPC_LOAD_U32(camera::kPrimary + 104);
        // The primary camera's known frame; unknown pointers are logged, not followed.
        fprintf(file, ",%u", frame);
        for (uint32_t offset = 16; offset < 64; offset += 4)
            fprintf(file, ",%.9g", frame == camera::kFrame ? f(frame + offset) : 0.f);
        // Immutable evidence for interpolation resets and simulation audits.
        // The fixed traffic records contain no streamed body pointers.
        uint64_t trafficHash = 14695981039346656037ULL;
        constexpr uint32_t traffic = world + 20464;
        for (uint32_t i = 0; i < 254; ++i)
            for (uint32_t offset : {160u,164u,168u})
                trafficHash = (trafficHash ^ PPC_LOAD_U32(traffic + 0x131D0 + i * 512 + offset)) * 1099511628211ULL;
        fprintf(file, ",%u,%u,%u,%llu", PPC_LOAD_U32(camera::kManager + 11904),
            PPC_LOAD_U8(rewind::kGame + 0x73F77), PPC_LOAD_U8(rewind::kGame + 0x73F78),
            (unsigned long long)trafficHash);
        // sub_820E35C8 hands participant+16 to sub_8217CF10, whose first
        // eight 64-bit loads copy this complete matrix for the vehicle draw.
        for (uint32_t offset = 16; offset < 80; offset += 4) fprintf(file, ",%.9g", f(participant + offset));
        fprintf(file, "\n");
        // _Exit does not flush stdio. Bound the diagnostic tail without
        // putting a flush on every simulation dispatch or render consumer.
        if (rows % 120 == 0) fflush(file);
    }
}

// Inner simulation-loop dispatch, distinct from a main-loop iteration and
// from a GPU front-buffer submission. The trace also records the native clock
// tick and dt; a dispatch does not imply that every physics body stepped.
extern "C" PPC_FUNC(__imp__sub_82101E18);
PPC_FUNC(sub_82101E18)
{
    const uint32_t self = ctx.r3.u32, caller = uint32_t(ctx.lr);
    game::crash_pacing::Sample("dispatch", base, self, caller);
    __imp__sub_82101E18(ctx, base);
    game::crash_pacing::Sample("dispatch_end", base, self, caller);
}

// The primary camera's conversion of its native quaternion/position into
// render matrices. Observe before conversion; never replace the frame or run
// this routine a second time. This is separate from a simulation boundary.
extern "C" PPC_FUNC(__imp__sub_8235A080);
PPC_FUNC(sub_8235A080)
{
    if (ctx.r3.u32 == game::camera::kPrimary)
        game::crash_pacing::Sample("render_camera", base, ctx.r3.u32, uint32_t(ctx.lr));
    __imp__sub_8235A080(ctx, base);
}

// Native game step called at 0x82120708, after dispatch and the active
// session's update. Capture only after the original returns. Dispatch alone
// precedes the world/event/camera consumers and is not pose truth.
extern "C" PPC_FUNC(__imp__sub_8210ECF0);
PPC_FUNC(sub_8210ECF0)
{
    const uint32_t self = ctx.r3.u32, caller = uint32_t(ctx.lr);
    __imp__sub_8210ECF0(ctx, base);
    game::crash_pacing::Sample("native_update_end", base, self, caller);
}
