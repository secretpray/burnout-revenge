// Synthetic XDBF, deliberately contains no game text or artwork.
#pragma once
#include <cstring>
#include <kernel/achievement_wire.h>
namespace achievementfixture
{
    inline void U16(std::vector<uint8_t>& b, size_t p, uint16_t n) { b[p] = uint8_t(n >> 8); b[p+1] = uint8_t(n); }
    inline void U32(std::vector<uint8_t>& b, size_t p, uint32_t n) { achievements::Put32(b, p, n); }
    inline std::vector<uint8_t> Database()
    {
        std::vector<uint8_t> strings(14), table(14 + 72), result(24 + 2*18 + 8);
        U32(strings,0,0x58535452); U32(strings,4,1); U16(strings,12,6);
        uint16_t id = 1;
        for (auto text : {"First", "Earned first", "Do first", "Second", "Earned second", "Do second"})
        {
            size_t p = strings.size(), length = std::strlen(text); strings.resize(p+4+length);
            U16(strings,p,id++); U16(strings,p+2,uint16_t(length)); std::memcpy(strings.data()+p+4,text,length);
        }
        U32(table,0,0x58414348); U32(table,4,1); U16(table,12,2);
        for (unsigned i=0;i<2;++i)
        {
            size_t p=14+i*36; U16(table,p,uint16_t(58+i)); U16(table,p+2,uint16_t(1+i*3));
            U16(table,p+4,uint16_t(2+i*3)); U16(table,p+6,uint16_t(3+i*3));
            U32(table,p+8,2+i); U16(table,p+12,5); U32(table,p+16,9);
        }
        U32(result,0,0x58444246); U32(result,4,0x10000); U32(result,8,2); U32(result,12,2); U32(result,16,1);
        U16(result,24,1); U32(result,30,0x58414348); U32(result,34,0); U32(result,38,uint32_t(table.size()));
        U16(result,42,3); U32(result,48,1); U32(result,52,uint32_t(table.size())); U32(result,56,uint32_t(strings.size()));
        result.insert(result.end(),table.begin(),table.end()); result.insert(result.end(),strings.begin(),strings.end()); return result;
    }
}
