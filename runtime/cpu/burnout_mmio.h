// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>

namespace apu { void StoreXmaMmio(uint32_t address, uint32_t value); }
inline void BurnoutMmioStore32(uint8_t* base, uint32_t address, uint32_t value)
{
    // Preserve the normal guest store, including GPU register RAM. XMA
    // registers are little-endian (the guest uses stwbrx to write masks).
    *(volatile uint32_t*)(base + address) = __builtin_bswap32(value);
    if ((address & 0xFFFF0000u) == 0x7FEA0000u)
        apu::StoreXmaMmio(address, __builtin_bswap32(value));
}
#define PPC_MM_STORE_U32(address, value) BurnoutMmioStore32(base, uint32_t(address), uint32_t(value))
