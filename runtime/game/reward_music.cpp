// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Adapted from Xerenge's reward-video resume fix, Copyright 2026 Shipa Snake,
// commit 65c40067fb6d1cfc9a55b46ee7aec1f525b53ed1 (issue #29).
// USA retail: the menu getter returns service+20 and clears it. START can
// leave a reward screen while its movie continues unseen; sub_822ADEE8 then
// keeps the soundtrack paused until the movie ends. Resume through the game's
// own routine when leaving those screens, preserving its hold/mute cleanup.
#include <stdafx.h>

#include <string_view>

namespace
{
    constexpr uint32_t kMusic = 0x8268748C;
    enum class Reward { None, Challenge, Rank, Signature };

    Reward RewardPage(std::string_view page)
    {
        if (page == "../Burnout4/Menu/Rewards/ChallengeCompleteVideo") return Reward::Challenge;
        if (page == "../Burnout4/Menu/Rewards/RankUpVideo") return Reward::Rank;
        if (page == "../Burnout4/Menu/Rewards/SigTakedownVideo") return Reward::Signature;
        return Reward::None;
    }
}

PPC_EXTERN_FUNC(sub_822ADE70);
extern "C" PPC_FUNC(__imp__sub_821E6BA8);
PPC_FUNC(sub_821E6BA8)
{
    __imp__sub_821E6BA8(ctx, base);
    // A null request is a poll, not a page change. The returned pointer is
    // the retail getter's; never reread the now-cleared pending slot.
    const uint32_t address = ctx.r3.u32;
    if (address < 0x10000 || address > UINT32_MAX - 256) return;
    const char* text = reinterpret_cast<const char*>(base + address);
    const size_t length = strnlen(text, 256);
    const std::string_view page(text, length);
    if (length == 256 || !page.starts_with("../Burnout4/Menu/")) return;

    static Reward previous = Reward::None; // guest main thread, like the getter
    const Reward next = RewardPage(page);
    // Keep the hold across consecutive reward clips. The last clip's exit
    // resumes once; a completed movie has already cleared +780 itself.
    if (previous != Reward::None && next == Reward::None && PPC_LOAD_U32(kMusic + 780))
    {
        const PPCContext saved = ctx;
        ctx.r3.u64 = kMusic;
        sub_822ADE70(ctx, base);
        ctx = saved;
        fprintf(stderr, "[music] reward video left: soundtrack resumed\n");
    }
    previous = next;
}
