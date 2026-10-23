// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>

namespace game::rewind::copy
{
    struct Region { uint32_t begin{}; uint64_t end{},calls{},bytes{}; };
    inline thread_local Region region;
    struct Scope
    {
        Region previous;
        Scope(uint32_t begin,uint32_t size,bool enabled) : previous(region)
        { region=enabled ? Region{begin,uint64_t(begin)+size}:Region{}; }
        ~Scope() { region=previous; }
    };
    inline bool CanCopy(uint32_t destination,uint32_t source,uint32_t size)
    {
        if(!region.begin || region.end>0x100000000ull || !size) return false;
        const uint64_t dstEnd=uint64_t(destination)+size,srcEnd=uint64_t(source)+size;
        if(dstEnd>0x100000000ull || srcEnd>0x100000000ull) return false;
        if(uint64_t(destination)<srcEnd && uint64_t(source)<dstEnd) return false;
        const auto inside=[](uint32_t address,uint64_t end) { return address>=region.begin && end<=region.end; };
        const bool dstInside=inside(destination,dstEnd),srcInside=inside(source,srcEnd);
        if((uint64_t(destination)<region.end && dstEnd>region.begin && !dstInside) ||
           (uint64_t(source)<region.end && srcEnd>region.begin && !srcInside)) return false;
        return dstInside || srcInside;
    }
}
