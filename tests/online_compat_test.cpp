// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Actual social/session imports and object lifetime, through PPC wrappers.
// Host heap failures are injected; only a few guest pages are committed.
#include <stdafx.h>
#include <kernel/function.h>
#include <kernel/overlapped.h>
#include <kernel/xdm.h>
#include <unordered_set>

PPC_EXTERN_FUNC(__imp__XamShowFriendsUI);
PPC_EXTERN_FUNC(__imp__XamShowGamerCardUIForXUID);
PPC_EXTERN_FUNC(__imp__XamShowPlayerReviewUI);
PPC_EXTERN_FUNC(__imp__XamShowMessageComposeUI);
PPC_EXTERN_FUNC(__imp__XamUserAreUsersFriends);
PPC_EXTERN_FUNC(__imp__XamParseGamerTileKey);
PPC_EXTERN_FUNC(__imp__XamReadTileToTexture);
PPC_EXTERN_FUNC(__imp__XamSessionCreateHandle);
PPC_EXTERN_FUNC(__imp__XamSessionRefObjByHandle);

Memory::Memory() {}
Memory g_memory;
Heap g_userHeap;
static uint32_t userNext = 0x40000, physicalNext = 0x80040000;
static bool failUser = false, failPhysical = false;
static std::unordered_set<void*> allocations;
void* Heap::Alloc(size_t size)
{
    if (failUser) return nullptr;
    assert(userNext + size < 0x80000);
    void* p = g_memory.base + userNext;
    userNext += (size + 15) & ~size_t(15);
    memset(p, 0xA5, size);
    allocations.insert(p);
    return p;
}
void* Heap::AllocPhysical(size_t size, size_t)
{
    if (failPhysical) return nullptr;
    assert(physicalNext + size < 0x80080000);
    void* p = g_memory.base + physicalNext;
    physicalNext += (size + 31) & ~size_t(31);
    memset(p, 0xA5, size);
    allocations.insert(p);
    return p;
}
void Heap::Free(void* p) { assert(allocations.erase(p) == 1); }
bool ImportTraceEnabled() { return false; }
void ImportTraceCall(const char*, const PPCContext&) {}
void ImportTraceReturn(const char*, const PPCContext&) {}
static uint32_t completions = 0;
void CompleteOverlapped(XXOVERLAPPED* o, uint32_t result, uint32_t error, uint32_t length)
{
    ++completions;
    o->Error = result; o->dwExtendedError = error; o->Length = length;
}
template<class T> T* At(uint32_t address) { return reinterpret_cast<T*>(g_memory.base + address); }
static uint32_t Call(PPCFunc* fn, std::initializer_list<uint64_t> args)
{
    PPCContext ctx{};
    PPCRegister* regs[] = {&ctx.r3, &ctx.r4, &ctx.r5, &ctx.r6, &ctx.r7, &ctx.r8, &ctx.r9, &ctx.r10};
    assert(args.size() <= 8);
    size_t i = 0;
    for (auto a : args) regs[i++]->u64 = a;
    fn(ctx, g_memory.base);
    return ctx.r3.u32;
}
static void TestSocial()
{
    // Closing an unavailable system screen must keep the title alive.
    for (int i = 0; i < 4; ++i)
    {
        assert(Call(__imp__XamShowFriendsUI, {0}) == 0);
        assert(Call(__imp__XamShowGamerCardUIForXUID, {0, 0xB13EBABE12345678, 0}) == 0);
        assert(Call(__imp__XamShowPlayerReviewUI, {0, 0xB13EBABE12345678}) == 0);
        assert(Call(__imp__XamShowMessageComposeUI, {0, 1, 0x14000, 0}) == 0);
    }
    auto* result = At<be<uint32_t>>(0x11000);
    *result = 0xDEADBEEF;
    assert(Call(__imp__XamUserAreUsersFriends, {0, 0x14000, 1, 0x11000, 0}) == 0);
    assert(*result == 0);
    for (uint32_t user : {1u, 4u, 0xFFFFFFFFu})
    {
        *result = 0xDEADBEEF;
        assert(Call(__imp__XamUserAreUsersFriends, {user, 0x14000, 1, 0x11000, 0}) == (user < 4 ? 1317 : 87));
        assert(*result == 0);
    }
    assert(Call(__imp__XamUserAreUsersFriends, {0, 0x14000, 1, 0, 0}) == 87);
    auto* o = At<XXOVERLAPPED>(0x12000);
    memset(o, 0x7F, sizeof(*o));
    auto before = completions;
    assert(Call(__imp__XamUserAreUsersFriends, {0, 0x14000, 1, 0, 0x12000}) == 997);
    assert(completions == before + 1 && o->Error == 0 && o->Length == 0 && o->dwExtendedError == 0);
    assert(Call(__imp__XamUserAreUsersFriends, {4, 0x14000, 1, 0, 0x12000}) == 997);
    assert(o->Error == 1627 && o->Length == 0 && o->dwExtendedError == 0x80070057);

    std::fill_n(At<be<uint32_t>>(0x11100), 5, be<uint32_t>(0xDEADBEEF));
    assert(Call(__imp__XamParseGamerTileKey, {0x14000, 0x11104, 0x11108, 0x1110C}) == 0);
    assert(*At<be<uint32_t>>(0x11100) == 0xDEADBEEF && *At<be<uint32_t>>(0x11110) == 0xDEADBEEF);
    for (int i = 1; i <= 3; ++i) assert(*At<be<uint32_t>>(0x11100 + i * 4) == 0);
    assert(Call(__imp__XamParseGamerTileKey, {0, 0, 0, 0}) == 0);

    // Match sub_825AEBE0 exactly: type, title, 64-bit tile id, user,
    // pixels, stride, height, completion. r9=32 must NEVER become a pointer.
    // The low guest pages are protected, so the old 7-argument hook faults.
    memset(At<uint8_t>(0x13000), 0xA5, 512);
    before = completions;
    assert(Call(__imp__XamReadTileToTexture, {9, 0, 0, 254, 0x13000, 128, 32, 0}) == 2);
    assert(completions == before);
    memset(o, 0x7F, sizeof(*o));
    assert(Call(__imp__XamReadTileToTexture, {9, 0, 0, 254, 0x13000, 128, 32, 0x12000}) == 997);
    assert(completions == before + 1 && o->Error == 2 && o->dwExtendedError == 0x80070002 && o->Length == 0);
    for (int i = 0; i < 512; ++i) assert(*At<uint8_t>(0x13000 + i) == 0xA5);
}
static void TestSessions()
{
    auto* h = At<be<uint32_t>>(0x15000);
    auto* object = At<be<uint32_t>>(0x15004);
    assert(Call(__imp__XamSessionCreateHandle, {0}) == 0xC000000D);
    for (bool physical : {false, true})
    {
        failUser = !physical; failPhysical = physical;
        *h = 0xDEADBEEF;
        assert(Call(__imp__XamSessionCreateHandle, {0x15000}) == 0xC0000017);
        assert(*h == 0 && allocations.empty());
    }
    failUser = failPhysical = false;
    for (int i = 0; i < 32; ++i)
    {
        assert(Call(__imp__XamSessionCreateHandle, {0x15000}) == 0);
        assert(IsKernelObject(*h) && allocations.size() == 2);
        assert(Call(__imp__XamSessionRefObjByHandle, {*h, 0x15004}) == 0);
        assert(*object != 0);
        for (int j = 0; j < 0x100; ++j) assert(*At<uint8_t>(*object + j) == 0);
        ReleaseKernelObject(GetKernelObject(*h));
        assert(allocations.empty());
    }
    for (uint32_t invalid : {0u, 1u, 0xFFFFFFFFu})
    {
        *object = 0xDEADBEEF;
        assert(Call(__imp__XamSessionRefObjByHandle, {invalid, 0x15004}) == 0xC0000008);
        assert(*object == 0);
    }
    auto* wrong = CreateKernelObject<KernelObject>();
    assert(Call(__imp__XamSessionRefObjByHandle, {GetKernelHandle(wrong), 0x15004}) == 0xC0000008);
    ReleaseKernelObject(wrong);
    assert(Call(__imp__XamSessionRefObjByHandle, {0, 0}) == 0xC000000D);
    assert(allocations.empty());
}
int main()
{
    g_memory.base = static_cast<uint8_t*>(mmap(nullptr, PPC_MEMORY_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
    assert(g_memory.base != MAP_FAILED);
    assert(mprotect(g_memory.base + 0x10000, 0x70000, PROT_READ | PROT_WRITE) == 0);
    assert(mprotect(g_memory.base + 0x80040000, 0x40000, PROT_READ | PROT_WRITE) == 0);
    TestSocial(); TestSessions();
    munmap(g_memory.base, PPC_MEMORY_SIZE);
    puts("Online compatibility: social PPC ABI, r10 tile completion, failures and 32 session lifetimes PASS");
}
