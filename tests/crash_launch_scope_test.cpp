// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Execute the production session guard on protected guest pages. Foreign
// wrappers are intentionally unmapped, so following one fails the test.
#include <stdafx.h>
#include <game/crash_launch_scope.h>
#include <unistd.h>

Memory::Memory() {}
Memory g_memory;
static void Map(uint32_t address, size_t size)
{
    const size_t page = size_t(sysconf(_SC_PAGESIZE));
    const size_t start = address & ~(page - 1);
    assert(mprotect(g_memory.base + start, (address + size - start + page - 1) & ~(page - 1), PROT_READ | PROT_WRITE) == 0);
}
int main()
{
    using namespace game;
    g_memory.base = static_cast<uint8_t*>(mmap(nullptr, PPC_MEMORY_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
    assert(g_memory.base != MAP_FAILED);
    Map(codes::kSession, 4); Map(codes::kSoloSession, 16); Map(codes::kLocalPlayers, 4);
    auto* base = g_memory.base;
    PPC_STORE_U32(codes::kSoloSession + 4, crash_launch::kSoloEvent);
    PPC_STORE_U32(codes::kLocalPlayers, 1);
    for (uint32_t session : {0u, codes::kFrontendSession, codes::kSplitSession, 0x8287C718u, 0xDEADBEEFu})
    {
        PPC_STORE_U32(codes::kSession, session);
        assert(!crash_launch::SoloEvent(base));
    }
    PPC_STORE_U32(codes::kSession, codes::kSoloSession);
    assert(crash_launch::SoloEvent(base));
    for (uint32_t players : {0u, 2u, 3u, 4u, UINT32_MAX})
    {
        PPC_STORE_U32(codes::kLocalPlayers, players);
        assert(!crash_launch::SoloEvent(base));
    }
    PPC_STORE_U32(codes::kLocalPlayers, 1);
    for (uint32_t event : {0u, crash_launch::kSoloEvent + 4, 0x8287D304u, UINT32_MAX})
    {
        PPC_STORE_U32(codes::kSoloSession + 4, event);
        assert(!crash_launch::SoloEvent(base));
    }
    PPC_STORE_U32(codes::kSoloSession + 4, crash_launch::kSoloEvent);
    assert(crash_launch::SoloEvent(base));
    PPC_STORE_U32(crash_launch::kNetworkSession+4,0x82889780);
    for (uint32_t session : {0u,codes::kFrontendSession,codes::kSoloSession,codes::kSplitSession,0xDEADBEEFu})
    {
        PPC_STORE_U32(codes::kSession,session);
        assert(!crash_launch::NetworkEvent(base,0x82889780));
    }
    PPC_STORE_U32(codes::kSession,crash_launch::kNetworkSession);
    for (uint32_t event : {0x82888190u,0x82889780u,0x8288AC70u})
    {
        PPC_STORE_U32(crash_launch::kNetworkSession+4,event);
        assert(crash_launch::NetworkEvent(base,event));
        assert(!crash_launch::NetworkEvent(base,event+4));
        PPC_STORE_U32(codes::kLocalPlayers,2);
        assert(!crash_launch::NetworkEvent(base,event));
        PPC_STORE_U32(codes::kLocalPlayers,1);
    }
    munmap(base, PPC_MEMORY_SIZE); g_memory.base = nullptr;
}
