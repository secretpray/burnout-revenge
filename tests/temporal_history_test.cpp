// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include <gpu/temporal_history.h>
#include <cassert>
#include <limits>

int main()
{
    gpu::temporal::Coverage c;
    c.Reset(1280,720);
    assert(c.Add({0,0,1280,240})); assert(c.Add({0,480,1280,240}));
    assert(!c.Complete()); // Bounding box is full, but middle is missing.
    assert(c.Add({0,240,1280,240})); assert(c.Complete());
    assert(!c.Add({0,240,1280,240})); assert(!c.Complete());
    c.Reset(1920,1080);
    for (unsigned y=0;y<1080;y+=360) assert(c.Add({0,y,1920,360}));
    assert(c.Complete());
    c.Reset(1280,720); assert(!c.Add({0,600,1280,240})); assert(!c.Complete());
    c.Reset(1280,720); assert(!c.Add({~0u,0,2,720})); assert(!c.Complete());
    c.Reset(1280,720); assert(!c.Add({0,0,0,720}));
    std::array<float,16> a{1,0,0,0, 0,1,0,0, 0,0,1,-.5f, 0,0,1,0};
    auto b=a; assert(!gpu::temporal::CameraCut(a,b));
    b[15]=-1; assert(!gpu::temporal::CameraCut(a,b)); // normal movement
    b[15]=-30; assert(gpu::temporal::CameraCut(a,b));
    b=a; b[12]=1; b[14]=0; assert(gpu::temporal::CameraCut(a,b));
    b=a; b[0]*=1.2f; assert(gpu::temporal::CameraCut(a,b));
    b=a; b[2]=std::numeric_limits<float>::quiet_NaN(); assert(gpu::temporal::CameraCut(a,b));
    b={}; assert(gpu::temporal::CameraCut(a,b));
}
