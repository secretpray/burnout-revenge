// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#include <stdafx.h>
#include "rewind_copy.h"

extern "C" PPC_FUNC(__imp__sub_8259D3A0);
PPC_FUNC(sub_8259D3A0)
{
    // The original memcpy loops over translated PPC loads/stores. Only copies
    // to/from our checkpoint staging buffer use the host implementation.
    // Other game calls, overlapping ranges and wrapped addresses stay native.
    using namespace game::rewind::copy;
    if(CanCopy(ctx.r3.u32,ctx.r4.u32,ctx.r5.u32))
    {
        // The assembly implementation saves r3 in its PPC stack red zone.
        // Preserve that memory side effect as well as the destination bytes.
        PPC_STORE_U64(ctx.r1.u32-8,ctx.r3.u64);
        memcpy(base+ctx.r3.u32,base+ctx.r4.u32,ctx.r5.u32);
        ++region.calls;region.bytes+=ctx.r5.u32;
        // Native memcpy returns the full original r3, including its high bits.
        return;
    }
    __imp__sub_8259D3A0(ctx,base);
}
