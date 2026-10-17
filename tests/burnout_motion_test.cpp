// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include <gpu/burnout_motion.h>
#include <cassert>

int main()
{
    const auto car=gpu::burnout::VelocityRegisters(0x156ac0f7b9547152);
    assert(car && car->current==2 && car->previous==3);
    assert(!gpu::burnout::VelocityRegisters(0));
    const std::string source=std::string("void main(){\n")+gpu::burnout::kInputMarker+"    original();\n}\n";
    const auto patched=gpu::burnout::WithTrueMotion(0x156ac0f7b9547152,source);
    assert(patched.find("now=r[2].xyz, before=r[3].xyz")!=std::string::npos);
    assert(patched.find("before.xy/before.z-now.xy/now.z")!=std::string::npos);
    assert(patched.find("o_color[2]")<patched.find("original();"));
    assert(patched.find("#ifdef OUT2")!=std::string::npos);
    assert(gpu::burnout::WithTrueMotion(0,source)==source);
    assert(!gpu::burnout::CanInstrument(0x156ac0f7b9547152,"changed translator output"));
}
