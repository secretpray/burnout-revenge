// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <array>
#include <bit>
#include <cstdint>

namespace hid::rewind
{
    constexpr bool ValidChord(uint16_t mask)
    {
        return (mask&~0xF3FFu)==0 && std::popcount(mask)>=2 && std::popcount(mask)<=3 && (mask&0x30)!=0x30;
    }
    inline unsigned KeySeconds(uint16_t key,const std::array<uint16_t,6>& keys)
    {
        unsigned result=0;
        if (!key) return 0;
        for (unsigned i=0;i<keys.size();++i) if (key==keys[i])
        {
            const unsigned seconds=10*(i/2+1);
            if (result && result!=seconds) return 0; // conflicting bindings
            result=seconds;
        }
        return result;
    }
    struct ChordSet
    {
        std::array<uint16_t,3> configured{};
        bool initialized{},armed{},latched{};
        int candidate=-1;
        double since{};
        struct Result { uint16_t consume{}; unsigned seconds{}; };
        Result Update(const std::array<uint16_t,3>& masks,uint16_t held,bool blocked,double now)
        {
            uint16_t all=0;
            for(auto mask:masks) if(ValidChord(mask)) all|=mask;
            if(!initialized || masks!=configured || blocked)
            {
                *this={};initialized=true;configured=masks;
                armed=!blocked && !(held&all);return {};
            }
            if(!(held&all)) {armed=true;latched=false;candidate=-1;return {};}
            if(!armed) return {};
            if(latched) return {uint16_t(held&all),0};
            unsigned matches=0;int match=-1;
            for(unsigned i=0;i<masks.size();++i)
                if(ValidChord(masks[i]) && (held&masks[i])==masks[i]) {++matches;match=int(i);}
            if(matches>1)
            {
                // Shared buttons or duplicate bindings must never choose an
                // arbitrary duration. A full release starts a new gesture.
                latched=true;candidate=-1;return {uint16_t(held&all),0};
            }
            if(!matches) {candidate=-1;return {};}
            if(candidate!=match) {candidate=match;since=now;}
            if(now-since<.3) return {masks[match],0};
            latched=true;return {masks[match],unsigned(10*(match+1))};
        }
    };

}
