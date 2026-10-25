// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#include <stdafx.h>
#include "camera.h"
#include "rewind_probe.h"
#include <user/settings.h>
#include <report/report.h>
#include <cmath>
#include <numbers>

namespace game::camera { bool Driving(uint8_t* base); }

namespace
{
    bool ResearchCamera()
    {
        static const bool enabled = [] {
            const char* v = std::getenv("BURNOUT_CAMERA_RESEARCH");
            return v && std::string_view(v) == "1";
        }();
        return enabled;
    }
}

// The USA ordinary chase controller (slot 9). Its per-car Follow/Camera
// Offset record is read by sub_82318848 before the native collision pass
// sub_8230EBF8. Scope the longitudinal offset to this call, leaving the
// shared tuning record, checkpoint and other camera modes unchanged.
extern "C" PPC_FUNC(__imp__sub_82318848);
PPC_FUNC(sub_82318848)
{
    using namespace game::camera;
    const int percent = settings::GetInt(settings::Id::CameraDistance);
    const uint32_t self = ctx.r3.u32;
    if ((percent == 100 && !ResearchCamera()) || self != kChase || ctx.r4.u32 != kFrame || !Driving(base))
    {
        __imp__sub_82318848(ctx, base);
        return;
    }
    const uint32_t params = PPC_LOAD_U32(self + 20);
    const uint32_t records = PPC_LOAD_U32(0x82AB1E40);
    // Six 432-byte records, Follow/Camera Offset at +96. Refuse unknown
    // pointers rather than changing an unverified camera parameter.
    bool known = false;
    for (unsigned slot = 0; records && slot < 6; ++slot)
        known |= uint64_t(records) + slot * 432 + 96 == params;
    if (!known || PPC_LOAD_U32(self + 4) != kManager || PPC_LOAD_U32(kManager + 11904) != 9 ||
        PPC_LOAD_U32(kManager + 11892) != 0)
    {
        __imp__sub_82318848(ctx, base);
        return;
    }
    const uint32_t address = params + 8, saved = PPC_LOAD_U32(address);
    const float native = std::bit_cast<float>(saved);
    const bool valid = std::isfinite(native) && native <= -2.f && native >= -15.f;
    struct Restore
    {
        uint8_t* base; uint32_t address, saved; bool active;
        ~Restore() { if (active) PPC_STORE_U32(address, saved); }
    } restore{base, address, saved, valid && percent != 100};
    const float adjusted = valid ? native * float(percent) / 100.f : native;
    if (restore.active) PPC_STORE_U32(address, std::bit_cast<uint32_t>(adjusted));
    if (ResearchCamera())
    {
        static thread_local double next{};
        const double now = report::SessionSeconds();
        if (now >= next)
        {
            next = now + 1.;
            fprintf(stderr, "[camera-distance] percent=%d native=%.6g adjusted=%.6g valid=%u params=%08X\n",
                percent, native, adjusted, valid, params);
        }
    }
    __imp__sub_82318848(ctx, base);
}

extern "C" PPC_FUNC(__imp__sub_8230EBF8);
PPC_FUNC(sub_8230EBF8)
{
    if (!ResearchCamera() || ctx.r3.u32 != game::camera::kChase || ctx.r4.u32 != game::camera::kFrame)
    {
        __imp__sub_8230EBF8(ctx, base);
        return;
    }
    std::array<float, 3> before{}, after{};
    for (unsigned i = 0; i < 3; ++i)
        before[i] = std::bit_cast<float>(PPC_LOAD_U32(game::camera::kFrame + 48 + i * 4));
    __imp__sub_8230EBF8(ctx, base);
    for (unsigned i = 0; i < 3; ++i)
        after[i] = std::bit_cast<float>(PPC_LOAD_U32(game::camera::kFrame + 48 + i * 4));
    static thread_local double next{};
    const double now = report::SessionSeconds();
    if (now >= next)
    {
        next = now + 1.;
        fprintf(stderr, "[camera-distance] native_collision position=%.6g,%.6g,%.6g->%.6g,%.6g,%.6g\n",
            before[0], before[1], before[2], after[0], after[1], after[2]);
    }
}

namespace
{
    using Pose = std::array<uint32_t, 7>;
    Pose ReadPose(uint8_t* base, uint32_t frame)
    {
        Pose result{};
        for (unsigned i = 0; i < result.size(); ++i) result[i] = PPC_LOAD_U32(frame + 32 + i * 4);
        return result;
    }

    void ReduceShake(uint8_t* base, uint32_t frame, const Pose& before, const Pose& after, int percent)
    {
        if (before == after) return;
        std::array<double, 7> a{}, b{}, result{};
        double normA = 0., normB = 0.;
        for (unsigned i = 0; i < a.size(); ++i)
        {
            a[i] = std::bit_cast<float>(before[i]); b[i] = std::bit_cast<float>(after[i]);
            if (!std::isfinite(a[i]) || !std::isfinite(b[i])) return;
            if (i < 4) { normA += a[i] * a[i]; normB += b[i] * b[i]; }
        }
        // Reject malformed frames rather than manufacturing a valid pose.
        if (normA < .9 || normA > 1.1 || normB < .9 || normB > 1.1) return;
        if (percent == 0)
        {
            for (unsigned i = 0; i < before.size(); ++i) PPC_STORE_U32(frame + 32 + i * 4, before[i]);
            return;
        }
        const double amount = double(percent) / 100.;
        double dot = 0.;
        for (unsigned i = 0; i < 4; ++i)
        {
            a[i] /= std::sqrt(normA); b[i] /= std::sqrt(normB); dot += a[i] * b[i];
        }
        // q and -q represent the same orientation. Take the shorter arc.
        if (dot < 0.) { dot = -dot; for (unsigned i = 0; i < 4; ++i) b[i] = -b[i]; }
        dot = std::clamp(dot, 0., 1.);
        double wa = 1. - amount, wb = amount;
        if (dot < .9995)
        {
            const double angle = std::acos(dot), denominator = std::sin(angle);
            wa = std::sin((1. - amount) * angle) / denominator;
            wb = std::sin(amount * angle) / denominator;
        }
        double norm = 0.;
        for (unsigned i = 0; i < 4; ++i) { result[i] = wa * a[i] + wb * b[i]; norm += result[i] * result[i]; }
        for (unsigned i = 0; i < 4; ++i) result[i] /= std::sqrt(norm);
        for (unsigned i = 4; i < 7; ++i) result[i] = a[i] + (b[i] - a[i]) * amount;
        for (unsigned i = 0; i < result.size(); ++i)
            PPC_STORE_U32(frame + 32 + i * 4, std::bit_cast<uint32_t>(float(result[i])));
    }
}

// Slots 32..34 add native camera effects after the follow/collision pass.
// Run the complete effect first (including timers, curves, rumble and FOV),
// then attenuate only its position/rotation contribution to this frame.
extern "C" PPC_FUNC(__imp__sub_822F5128);
PPC_FUNC(sub_822F5128)
{
    using namespace game::camera;
    const int percent = settings::GetInt(settings::Id::CameraShake);
    const bool research = ResearchCamera();
    const uint32_t self = ctx.r3.u32, frame = ctx.r4.u32;
    if ((percent == 100 && !research) || frame != kFrame ||
        (self != kManager + 9020 && self != kManager + 9072 && self != kManager + 9124))
    { __imp__sub_822F5128(ctx, base); return; }
    const uint32_t slot = PPC_LOAD_U32(self + 44);
    const bool active = slot >= 32 && slot <= 34 && self == kManager + 9020 + (slot - 32) * 52 &&
        PPC_LOAD_U32(self + 4) == kManager && PPC_LOAD_U32(kManager + 2896 + slot * 4) == self &&
        PPC_LOAD_U32(kManager + 11904) == 9 && PPC_LOAD_U32(kManager + 11892) == 0 && Driving(base);
    if (!active && !research) { __imp__sub_822F5128(ctx, base); return; }
    const Pose before = ReadPose(base, frame);
    const float amplitude = std::bit_cast<float>(PPC_LOAD_U32(self + 28));
    const uint32_t type = PPC_LOAD_U32(self + 36);
    __imp__sub_822F5128(ctx, base);
    const Pose native = ReadPose(base, frame);
    if (active && percent != 100) ReduceShake(base, frame, before, native, percent);
    if (research)
    {
        static thread_local std::array<double, 3> next{};
        const unsigned index = (self - kManager - 9020) / 52;
        const double now = report::SessionSeconds();
        if (now >= next[index])
        {
            next[index] = now + .25;
            double nativeDelta = 0., adjustedDelta = 0.; const Pose adjusted = ReadPose(base, frame);
            for (unsigned i = 0; i < before.size(); ++i)
            {
                nativeDelta += std::abs(double(std::bit_cast<float>(native[i])) - std::bit_cast<float>(before[i]));
                adjustedDelta += std::abs(double(std::bit_cast<float>(adjusted[i])) - std::bit_cast<float>(before[i]));
            }
            fprintf(stderr, "[camera-effect] t=%.3f slot=%u type=%u amp=%.6g active=%u percent=%d native_delta=%.8g adjusted_delta=%.8g\n",
                now, slot, type, amplitude, active, percent, nativeDelta, adjustedDelta);
        }
    }
}

namespace game::camera
{
    bool Driving(uint8_t* base)
    {
        // Same verified ordinary-driving boundary used by Rewind. The
        // camera can share its address with Aftertouch, results and Replay.
        const uint32_t session = PPC_LOAD_U32(codes::kSession);
        const int id = int8_t(PPC_LOAD_U8(kParticipant + 15480));
        const uint32_t wrapper = id >= 0 && id < 54
            ? PPC_LOAD_U32(0x82AB6F80 + 0x47760 + (7 * 65536 + 9776 + id) * 4) : 0;
        return PPC_LOAD_U32(rewind::kPhase) == 5 &&
            // Xbox Live sign-in is also available while playing World Tour.
            // The verified solo wrapper, not the account's sign-in setting,
            // separates this camera from online and local multiplayer.
            codes::Allowed({true, false, session,
                session == codes::kSoloSession ? PPC_LOAD_U32(session + 8) : 0,
                PPC_LOAD_U32(codes::kLocalPlayers), PPC_LOAD_U8(codes::kSpecialMode),
                PPC_LOAD_U32(codes::kCrashVariant), 0, 0}) &&
            PPC_LOAD_U32(rewind::kClockFlags) == 0x01000000 &&
            !PPC_LOAD_U8(rewind::kWorldPlayback) &&
            PPC_LOAD_U32(kPrimary + 104) == kFrame && wrapper &&
            !PPC_LOAD_U8(wrapper + 599) && PPC_LOAD_U32(kParticipant + 13520) == 2;

    }

    float ViewWindowScale(uint8_t* base, uint32_t camera, uint32_t caller, double nativeFov)
    {
        const int offset = settings::GetInt(settings::Id::CameraFov);
        static const bool log = [] {
            const char* v = std::getenv("BURNOUT_LOG_CAMERA");
            return v && std::string_view(v) == "1";
        }();
        if ((!offset && !log) || camera != kPrimary || caller != 0x8235A230)
            return 1.f;

        if (ResearchCamera())
        {
            static thread_local double next{};
            const double now = report::SessionSeconds();
            if (now >= next && PPC_LOAD_U32(camera + 104) == kFrame)
            {
                next = now + 1.;
                constexpr uint32_t manager = kFrame - 2736;
                const uint32_t index = PPC_LOAD_U32(manager + 11904);
                const uint32_t controller = index < 38 ? PPC_LOAD_U32(manager + 2896 + index * 4) : 0;
                const uint32_t vtable = controller ? PPC_LOAD_U32(controller) : 0;
                fprintf(stderr, "[camera-research] active manager=%08X index=%u controller=%08X update=%08X flags=%016llX\n",
                    manager, index, controller, vtable ? PPC_LOAD_U32(vtable + 16) : 0,
                    (unsigned long long)PPC_LOAD_U64(manager + 11856));
            }
        }

        const bool driving = Driving(base);

        float scale = 1.f;
        double adjusted = nativeFov;
        if (driving && offset && std::isfinite(nativeFov) && nativeFov >= 30. && nativeFov <= 120.)
        {
            adjusted = std::clamp(nativeFov + offset, 30., 120.);
            constexpr double radians = std::numbers::pi / 360.;
            scale = float(std::tan(adjusted * radians) / std::tan(nativeFov * radians));
        }
        if (log)
        {
            static thread_local double next{};
            static thread_local bool lastDriving{};
            const double now = report::SessionSeconds();
            if (now >= next || driving != lastDriving)
            {
                next = now + 1.; lastDriving = driving;
                fprintf(stderr, "[camera] driving=%u native=%.4f adjusted=%.4f offset=%d scale=%.6f mode=%u frame=%08X\n",
                    driving, nativeFov, adjusted, offset, scale, PPC_LOAD_U32(kParticipant + 13520),
                    PPC_LOAD_U32(camera + 104));
            }
        }
        return scale;
    }
}
