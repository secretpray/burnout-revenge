// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace gpu::burnout
{
    struct MotionRegisters { uint32_t current, previous; };
    // Verified Burnout USA pixel programs: these interpolators carry current
    // and previous homogeneous (U,V,W). Read them at entry, before the game's
    // motion-blur extrapolation/sign/strength/UNORM encoding changes them.
    inline std::optional<MotionRegisters> VelocityRegisters(uint64_t ps)
    {
        switch (ps)
        {
        case 0xc075677bb65a3238: return MotionRegisters{0,1};
        case 0xafabfe4eeb699bf6: case 0x82d5b6c16d45b93a: case 0xc4c233949e95921b:
        case 0x1340f83c2b242e01: case 0x609919477cdcab2a: case 0x8488234fefc61ea1: return MotionRegisters{1,2};
        case 0x2b2279d8a2da44ca: case 0x156ac0f7b9547152: return MotionRegisters{2,3};
        case 0x5bc7cdb9030fa35e: case 0xdbfa8f2a7f5efc08: case 0x930d501c6a5a458c: return MotionRegisters{3,4};
        case 0xfe0bf33dea4a067e: case 0xbd126d321e59e586: case 0x8afe2a81ba9bf3a1: return MotionRegisters{4,5};
        case 0x03dc874ef84efcc9: return MotionRegisters{5,6};
        case 0xadf425bfcc3c7eb0: return MotionRegisters{6,7};
        default: return std::nullopt;
        }
    }
    inline constexpr const char* kInputMarker="#ifdef IN15\n    r[15] = i_i15;\n#endif\n";
    inline bool CanInstrument(uint64_t ps, const std::string& glsl)
    {
        return VelocityRegisters(ps).has_value() && glsl.find(kInputMarker)!=std::string::npos;
    }
    inline std::string WithTrueMotion(uint64_t ps, const std::string& original)
    {
        if (!CanInstrument(ps,original)) return original;
        const auto regs=*VelocityRegisters(ps);
        const std::string current="r["+std::to_string(regs.current)+"]";
        const std::string previous="r["+std::to_string(regs.previous)+"]";
        const std::string code="\n#ifdef OUT2\n    {\n"
            "        vec3 now="+current+".xyz, before="+previous+".xyz;\n"
            "        bool valid=now.z>0.000001 && before.z>0.000001;\n"
            "        vec2 velocity=valid ? before.xy/before.z-now.xy/now.z : vec2(0);\n"
            "        valid=valid && !any(isnan(velocity)) && !any(isinf(velocity));\n"
            "        o_color[2]=valid ? vec4(velocity,1,min(before.z/now.z,65504.0)) : vec4(0);\n"
            "    }\n#endif\n";
        std::string glsl=original;
        glsl.insert(glsl.find(kInputMarker)+std::char_traits<char>::length(kInputMarker),code);
        return glsl;
    }
}
