// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#include <stdafx.h>
#include "madden_unlock.h"
#include "codes.h"
#include "rewind_probe.h"
#include <atomic>

namespace
{
    constexpr uint32_t kCareer = 0x8281FA78;
    std::atomic<game::madden::Status> status{game::madden::Status::Unavailable};
    std::atomic<bool> requested{};
}
PPC_EXTERN_FUNC(sub_821015A8);
game::madden::Status game::madden::GetStatus() { return status.load(std::memory_order_acquire); }
void game::madden::Request()
{
    Status ready = Status::Ready;
    if (status.compare_exchange_strong(ready, Status::Pending, std::memory_order_acq_rel))
        requested.store(true, std::memory_order_release);
}
void game::madden::AtBoundary(PPCContext& ctx, uint8_t* base)
{
    const bool apply = requested.exchange(false, std::memory_order_acq_rel);
    // Restrict the action to a loaded career in the frontend. Live sign-in
    // is independent of this menu and does not prevent the stock trailer unlock.
    if (PPC_LOAD_U32(rewind::kPhase) != 5 ||
        PPC_LOAD_U32(codes::kSession) != codes::kFrontendSession ||
        PPC_LOAD_U32(kCareer + 1248) == 0)
    {
        status.store(Status::Unavailable, std::memory_order_release);
        return;
    }
    if (apply && PPC_LOAD_U8(kCareer + 1242) == 0)
    {
        // Same routine as the Madden NFL 06 Trailer menu (821F4C18).
        // It sets only the Madden bonus and rebuilds the native car catalogue.
        const PPCContext saved = ctx;
        ctx.r3.u64 = kCareer;
        sub_821015A8(ctx, base);
        ctx = saved;
        fprintf(stderr, "[codes] Madden Van: native trailer unlock applied\n");
    }
    status.store(PPC_LOAD_U8(kCareer + 1242) ? Status::Unlocked : Status::Ready, std::memory_order_release);
}
