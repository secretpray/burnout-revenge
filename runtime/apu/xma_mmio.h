// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>

namespace apu
{
    enum class XmaCommand { None, Kick, Lock, Clear };
    struct XmaRegisterCommand { XmaCommand command; uint32_t firstContext; };
    constexpr XmaRegisterCommand DecodeXmaCommand(uint32_t address)
    {
        if ((address & 0xFFFF0003u) != 0x7FEA0000u) return {XmaCommand::None,0};
        uint32_t index = (address & 0xFFFFu) / 4;
        if (index >= 0x650 && index < 0x65A) return {XmaCommand::Kick,(index-0x650)*32};
        if (index >= 0x690 && index < 0x69A) return {XmaCommand::Lock,(index-0x690)*32};
        if (index >= 0x6A0 && index < 0x6AA) return {XmaCommand::Clear,(index-0x6A0)*32};
        return {XmaCommand::None,0};
    }
    void InitializeXmaMmio();
    void StoreXmaMmio(uint32_t address, uint32_t littleEndianValue);
}
