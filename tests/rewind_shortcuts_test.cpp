// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#include <hid/rewind_shortcuts.h>
#include <cassert>
int main()
{
    using namespace hid::rewind;
    assert(ValidChord(0x300) && ValidChord(0xE000) && ValidChord(0xC000) && ValidChord(0xA000) && ValidChord(0x6000));
    assert(!ValidChord(0) && !ValidChord(0x100) && !ValidChord(0x30) && !ValidChord(0xF000) && !ValidChord(0xC00));
    const std::array<uint16_t,3> masks{0xC000,0xA000,0x6000};
    const auto CheckDurations=[](const std::array<uint16_t,3>& masks)
    {
        for(unsigned i=0;i<3;++i)
        {
            ChordSet c;
            c.Update(masks,0,false,0);
            // An individual first button retains its gameplay action.
            const auto first=uint16_t(masks[i]&uint16_t(-masks[i]));
            assert(!c.Update(masks,first,false,1).consume);
            assert(c.Update(masks,masks[i],false,2).consume==masks[i]);
            assert(!c.Update(masks,masks[i],false,2.29).seconds);
            assert(c.Update(masks,masks[i],false,2.31).seconds==10*(i+1));
            assert(!c.Update(masks,masks[i],false,9).seconds);
            // Adding other shortcut buttons cannot chain another duration.
            assert(!c.Update(masks,uint16_t(masks[0]|masks[1]|masks[2]),false,10).seconds);
            assert(!c.Update(masks,masks[(i+1)%3],false,11).seconds);
            assert(!c.Update(masks,masks[(i+1)%3],false,12).seconds);
            c.Update(masks,0,false,13);
            c.Update(masks,masks[i],false,14);
            assert(c.Update(masks,masks[i],false,14.31).seconds==10*(i+1));
        }
    };
    CheckDurations(masks);
    CheckDurations({0x300,0xC0,0x180});
    assert(ValidChord(0xC0) && ValidChord(0x180));
    ChordSet c;
    c.Update(masks,0,false,0);
    c.Update(masks,0xE000,false,1);
    assert(!c.Update(masks,0xE000,false,2).seconds);
    // An ambiguous three-button press stays blocked even after releasing one.
    assert(!c.Update(masks,0xC000,false,3).seconds);
    assert(!c.Update(masks,0xC000,false,4).seconds);
    c.Update(masks,0,false,5);
    c.Update(masks,0xC000,false,6);
    c.Update(masks,0,false,6.25); // short accidental combination
    assert(!c.Update(masks,0xC000,false,7).seconds);
    assert(c.Update(masks,0xC000,false,7.31).seconds==10);
    c.Update(masks,0xC000,true,8); // focus loss or open menu
    assert(!c.Update(masks,0xC000,false,9).seconds);
    assert(!c.Update(masks,0xC000,false,10).seconds);
    c.Update(masks,0,false,11);
    c.Update(masks,0xA000,false,12);
    assert(c.Update(masks,0xA000,false,12.31).seconds==20);
    const std::array<uint16_t,3> changed{0x300,0,0};
    c.Update(changed,0x300,false,13); // reconfiguration while held
    assert(!c.Update(changed,0x300,false,14).seconds);
    c.Update(changed,0,false,15);c.Update(changed,0x300,false,16);
    assert(c.Update(changed,0x300,false,16.31).seconds==10);
    const std::array<uint16_t,3> duplicate{0x300,0x300,0};
    c.Update(duplicate,0,false,17);c.Update(duplicate,0x300,false,18);
    assert(!c.Update(duplicate,0x300,false,19).seconds);
    c.Update({0,0,0},0,false,20);
    assert(!c.Update({0,0,0},0xE000,false,21).consume);
    const std::array<uint16_t,6> keys{59,0,60,0,61,0};
    assert(KeySeconds(59,keys)==10 && KeySeconds(60,keys)==20 && KeySeconds(61,keys)==30 && !KeySeconds(0,keys));
    assert(!KeySeconds(59,{59,0,59,0,61,0}));
    assert(KeySeconds(59,{59,59,60,0,61,0})==10);
}
