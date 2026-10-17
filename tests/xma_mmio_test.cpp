// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include <apu/xma_mmio.h>
#include <cassert>
int main()
{
    using namespace apu;
    assert(DecodeXmaCommand(0x7FEA1940).command == XmaCommand::Kick);
    assert(DecodeXmaCommand(0x7FEA1940).firstContext == 0);
    assert(DecodeXmaCommand(0x7FEA1964).firstContext == 288); // includes hardware ID 319
    assert(DecodeXmaCommand(0x7FEA1968).command == XmaCommand::None);
    assert(DecodeXmaCommand(0x7FEA1A40).command == XmaCommand::Lock);
    assert(DecodeXmaCommand(0x7FEA1A80).command == XmaCommand::Clear);
    assert(DecodeXmaCommand(0x7FEA1AA4).firstContext == 288);
    assert(DecodeXmaCommand(0x7FEA1941).command == XmaCommand::None);
    assert(DecodeXmaCommand(0x7FC81940).command == XmaCommand::None);
}
