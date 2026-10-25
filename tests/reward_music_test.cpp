// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Execute the production PPC hook against protected guest pages. No game data
// is required: spies model the retail getter and check native resume's ABI.
#include <stdafx.h>
#include <unistd.h>

PPC_EXTERN_FUNC(sub_821E6BA8);
Memory::Memory() {}
Memory g_memory;
static constexpr uint32_t kService = 0x20000, kText = 0x30000, kMusic = 0x8268748C;
static int getterCalls = 0, resumes = 0;
static PPCContext returned;

extern "C" PPC_FUNC(__imp__sub_821E6BA8)
{
    assert(ctx.r3.u32 == kService);
    ++getterCalls;
    ctx.r3.u64 = PPC_LOAD_U32(kService + 20);
    PPC_STORE_U32(kService + 20, 0);
    ctx.r4.u64 = 0xFACEBEEF12345678;
    ctx.r12.u64 = 0x123456789ABCDEF0;
    ctx.lr = 0x821AB5E0;
    ctx.f1.f64 = 12.5;
    returned = ctx;
}
PPC_FUNC(sub_822ADE70)
{
    assert(ctx.r3.u64 == kMusic && PPC_LOAD_U32(kMusic + 780));
    ++resumes;
    // Cleanup belongs to this native routine, not to the menu hook.
    PPC_STORE_U32(kMusic + 780, 0);
    ctx = {}; // Clobber volatile and nonvolatile context to test restoration.
}
static void Map(uint32_t address, size_t size)
{
    const size_t page = size_t(sysconf(_SC_PAGESIZE));
    const size_t start = address & ~(page - 1);
    assert(mprotect(g_memory.base + start, (address + size - start + page - 1) & ~(page - 1), PROT_READ | PROT_WRITE) == 0);
}
static void Word(uint32_t address, uint32_t value)
{
    auto* base = g_memory.base;
    PPC_STORE_U32(address, value);
}
static void Request(uint32_t address, int expected)
{
    Word(kService + 20, address);
    PPCContext ctx{};
    ctx.r3.u64 = kService;
    ctx.r31.u64 = 0xDEADBEEFDEADBEEF;
    const int beforeGets = getterCalls, beforeResumes = resumes;
    sub_821E6BA8(ctx, g_memory.base);
    assert(getterCalls == beforeGets + 1 && resumes == beforeResumes + expected);
    assert(std::memcmp(&ctx, &returned, sizeof(ctx)) == 0);
    auto* base = g_memory.base;
    assert(PPC_LOAD_U32(kService + 20) == 0);
}
static void Page(const char* page, int expected = 0)
{
    std::strcpy(reinterpret_cast<char*>(g_memory.base + kText), page);
    Request(kText, expected);
}
int main()
{
    g_memory.base = static_cast<uint8_t*>(mmap(nullptr, PPC_MEMORY_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
    assert(g_memory.base != MAP_FAILED);
    Map(kService, 32); Map(kText, 512); Map(kMusic, 800);
    constexpr const char* menu = "../Burnout4/Menu/Main/MainMenu";
    constexpr const char* rewards[] = {
        "../Burnout4/Menu/Rewards/ChallengeCompleteVideo",
        "../Burnout4/Menu/Rewards/RankUpVideo",
        "../Burnout4/Menu/Rewards/SigTakedownVideo"
    };
    for (const char* reward : rewards)
        for (uint32_t mode : {1u, 2u, 3u})
            for (uint32_t muted : {0u, 1u})
            {
                Page(menu);
                Word(kMusic + 776, mode);
                g_memory.base[kMusic + 25] = uint8_t(muted);
                Word(kMusic + 780, 1);
                Page(reward); // Entering must never resume.
                Request(0, 0); // Null polls do not forget the reward screen.
                Page(reward); // Same page again must not resume either.
                Page(menu, 1); // START exit, even if the hidden movie is running.
                Page(menu); // No duplicate resume.
                auto* base = g_memory.base;
                assert(PPC_LOAD_U32(kMusic + 776) == mode && PPC_LOAD_U8(kMusic + 25) == muted);
                Page(reward);
                Word(kMusic + 780, 0); // Full watch: native movie-end already resumed.
                Page(menu);
            }
    // Consecutive rewards keep the hold until the last reward's exit.
    Word(kMusic + 780, 1);
    for (const char* reward : rewards) Page(reward);
    Page(menu, 1);
    // A suffix match alone would catch unrelated videos. These are excluded.
    for (const char* other : {"../Burnout4/Menu/Main/Attract", "../Burnout4/Menu/Main/VideoMovie",
                              "../Burnout4/Menu/Rewards/OtherVideo", "../Burnout4/Menu/Rewards/RankUpVideoExtra"})
    {
        Word(kMusic + 780, 1);
        Page(other); Page(menu);
    }
    // Invalid/overlong requests must not trigger a resume or lose the real page.
    Page(rewards[0]);
    Request(1, 0); Request(UINT32_MAX - 1, 0);
    Page("unrelated"); Page("");
    std::memset(g_memory.base + kText, 'X', 256);
    Request(kText, 0);
    Page(menu, 1);
    assert(resumes == 20);
    munmap(g_memory.base, PPC_MEMORY_SIZE);
    g_memory.base = nullptr;
}
