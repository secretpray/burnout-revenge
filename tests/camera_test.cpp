// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Exercise the production camera guard and projection multiplier against
// protected guest pages, including transitions out of ordinary driving.
#include <stdafx.h>
#include <game/camera.h>
#include <game/rewind_probe.h>
#include <user/settings.h>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <unistd.h>

Memory::Memory() {}
Memory g_memory;
namespace report { double SessionSeconds() { return 0.; } }
PPC_EXTERN_FUNC(sub_82318848);
extern "C" PPC_FUNC(__imp__sub_8230EBF8) {}
static bool throwOriginal{};
static unsigned chaseCalls{};
static constexpr uint32_t kRecords = 0x60000;
static float observedZ{};
extern "C" PPC_FUNC(__imp__sub_82318848)
{
    ++chaseCalls;
    const uint32_t record = PPC_LOAD_U32(ctx.r3.u32 + 20);
    observedZ = record ? std::bit_cast<float>(PPC_LOAD_U32(record + 8)) : 0;
    assert(ctx.r5.u64 == 0xDEADBEEF12345678);
    if (throwOriginal) throw std::runtime_error("native camera test");
    ctx.r3.u64 = 0x12345678;
}
PPC_EXTERN_FUNC(sub_822F5128);
static unsigned effectCalls{};
static bool effectWrite = true;
static uint32_t effectFrame{};
static float nativeQuaternionSign = 1.f;
static bool invalidEffect{};
static bool unchangedEffect{};
static double effectAngle = std::numbers::pi / 12.;
extern "C" PPC_FUNC(__imp__sub_822F5128)
{
    ++effectCalls;
    assert(ctx.r5.u64 == 0x1234567812345678);
    if (effectWrite)
    {
        const uint32_t self = ctx.r3.u32;
        const float elapsed = std::bit_cast<float>(PPC_LOAD_U32(self + 20));
        PPC_STORE_U32(self + 20, std::bit_cast<uint32_t>(elapsed + .125f));
        const std::array<float, 7> pose = {0.f, 0.f,
            nativeQuaternionSign * float(std::sin(effectAngle)),
            nativeQuaternionSign * float(std::cos(effectAngle)), 12.f, 24.f, 36.f};
        if (!unchangedEffect)
            for (unsigned i = 0; i < pose.size(); ++i)
                PPC_STORE_U32(effectFrame + 32 + i * 4, std::bit_cast<uint32_t>(pose[i]));
        if (invalidEffect) PPC_STORE_U32(effectFrame + 32, std::bit_cast<uint32_t>(NAN));
        PPC_STORE_U32(effectFrame + 16, std::bit_cast<uint32_t>(81.f));
    }
    ctx.r3.u64 = 0x87654321;
}
using namespace game;
static constexpr uint32_t kWrapper = 0x50000;
static constexpr uint32_t kWrapperTable = 0x82AB6F80 + 0x47760 + (7 * 65536 + 9776) * 4;
static void Map(uint32_t address, size_t size)
{
    const size_t page = size_t(sysconf(_SC_PAGESIZE));
    const size_t start = address & ~(page - 1);
    assert(mprotect(g_memory.base + start, (address + size - start + page - 1) & ~(page - 1), PROT_READ | PROT_WRITE) == 0);
}
static void Word(uint32_t address, uint32_t value)
{
    auto* base = g_memory.base; PPC_STORE_U32(address, value);
}
static float Scale(double fov = 70., uint32_t object = camera::kPrimary, uint32_t caller = 0x8235A230)
{
    return camera::ViewWindowScale(g_memory.base, object, caller, fov);
}
static void Reset()
{
    settings::Set(settings::Id::CameraFov, 10);
    settings::Set(settings::Id::OnlineEnabled, 0);
    Word(codes::kSession, codes::kSoloSession); Word(codes::kSoloSession + 8, 1);
    Word(codes::kLocalPlayers, 1); g_memory.base[codes::kSpecialMode] = 0;
    Word(codes::kCrashVariant, 0); Word(rewind::kPhase, 5);
    Word(rewind::kClockFlags, 0x01000000); g_memory.base[rewind::kWorldPlayback] = 0;
    Word(camera::kPrimary + 104, camera::kFrame);
    g_memory.base[camera::kParticipant + 15480] = 0;
    Word(camera::kParticipant + 13520, 2);
    Word(kWrapperTable, kWrapper); Word(kWrapperTable + 4, 0);
    g_memory.base[kWrapper + 599] = 0;
}
int main()
{
    unsetenv("BURNOUT_CAMERA_FOV"); unsetenv("BURNOUT_LOG_CAMERA");
    unsetenv("BURNOUT_CAMERA_DISTANCE"); unsetenv("BURNOUT_CAMERA_SHAKE");
    unsetenv("BURNOUT_CAMERA_RESEARCH");
    char temp[] = "/tmp/br-camera-test-XXXXXX"; assert(mkdtemp(temp));
    const auto file = std::filesystem::path(temp) / "settings.toml";
    assert(settings::Load(file));
    g_memory.base = static_cast<uint8_t*>(mmap(nullptr, PPC_MEMORY_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
    assert(g_memory.base != MAP_FAILED);
    // A default run must return before touching any guest page.
    assert(Scale() == 1.f);
    // Default shake adds no guest reads, even with the known native addresses.
    effectWrite = false;
    PPCContext originalOnly{}; originalOnly.r3.u32 = camera::kManager + 9020;
    originalOnly.r4.u32 = camera::kFrame; originalOnly.r5.u64 = 0x1234567812345678;
    sub_822F5128(originalOnly, g_memory.base);
    assert(effectCalls == 1 && originalOnly.r3.u64 == 0x87654321); effectWrite = true;
    Map(codes::kSession, 64); Map(codes::kSoloSession, 32); Map(codes::kLocalPlayers, 4);
    Map(codes::kCrashVariant, 4); Map(rewind::kPhase, 4); Map(rewind::kClockFlags, 4);
    Map(rewind::kWorldPlayback, 4); Map(camera::kPrimary, 304);
    Map(camera::kParticipant + 13520, 4); Map(camera::kParticipant + 15480, 1);
    Map(kWrapperTable, 54 * 4); Map(kWrapper, 640);
    Map(camera::kManager, 12000); Map(0x82AB1E40, 4); Map(kRecords, 6 * 432);
    Reset();
    constexpr double radians = std::numbers::pi / 360.;
    assert(std::abs(Scale() - std::tan(80. * radians) / std::tan(70. * radians)) < 1e-6);
    // Repeated calls never compound the adjustment or change saved game data.
    std::array<uint8_t, 304> before{}; memcpy(before.data(), g_memory.base + camera::kPrimary, before.size());
    const float first = Scale(); for (int i = 0; i < 100; ++i) assert(Scale() == first);
    assert(memcmp(before.data(), g_memory.base + camera::kPrimary, before.size()) == 0);
    settings::Set(settings::Id::CameraFov, -10);
    assert(std::abs(Scale() - std::tan(60. * radians) / std::tan(70. * radians)) < 1e-6);
    assert(Scale(30.) == 1.f); settings::Set(settings::Id::CameraFov, 20); assert(Scale(120.) == 1.f);
    for (double fov : {0., 29., 121., double(INFINITY), double(NAN)}) assert(Scale(fov) == 1.f);
    assert(Scale(70., camera::kPrimary + 304) == 1.f && Scale(70., camera::kPrimary, 0x8215C2D4) == 1.f);
    Reset(); settings::Set(settings::Id::OnlineEnabled, 1); assert(Scale() > 1.f);
    // Sign-in is independent of active play. Only the exact solo wrapper is
    // eligible; online, split, frontend and unknown wrappers never are.
    for (uint32_t session : {0u, codes::kFrontendSession, codes::kSplitSession, 0x8287C718u, 0xDEADBEEFu})
    { Reset(); settings::Set(settings::Id::OnlineEnabled, 1); Word(codes::kSession, session); assert(Scale() == 1.f); }
    for (uint32_t count : {0u, 2u, 4u}) { Reset(); Word(codes::kLocalPlayers, count); assert(Scale() == 1.f); }
    for (uint32_t phase : {0u, 4u, 6u}) { Reset(); Word(rewind::kPhase, phase); assert(Scale() == 1.f); }
    for (uint32_t mode : {0u, 1u, 3u}) { Reset(); Word(camera::kParticipant + 13520, mode); assert(Scale() == 1.f); }
    for (uint32_t controller : {0u, codes::kCrashController, codes::kPartyController})
    { Reset(); Word(codes::kSoloSession + 8, controller); assert(Scale() == 1.f); }
    for (uint8_t id : {uint8_t(54), uint8_t(255)})
    { Reset(); g_memory.base[camera::kParticipant + 15480] = id; assert(Scale() == 1.f); }
    Reset(); Word(codes::kSession, 0); assert(Scale() == 1.f);
    Reset(); g_memory.base[codes::kSpecialMode] = 1; assert(Scale() == 1.f);
    Reset(); Word(rewind::kClockFlags, 0x01010000); assert(Scale() == 1.f);
    Reset(); g_memory.base[rewind::kWorldPlayback] = 1; assert(Scale() == 1.f);
    Reset(); Word(camera::kPrimary + 104, 0); assert(Scale() == 1.f);
    Reset(); g_memory.base[kWrapper + 599] = 1; assert(Scale() == 1.f);
    Reset(); Word(kWrapperTable, 0); assert(Scale() == 1.f);
    // Respawning can change the physical actor slot. Follow its current ID.
    Reset(); g_memory.base[camera::kParticipant + 15480] = 1;
    assert(Scale() == 1.f); Word(kWrapperTable + 4, kWrapper); assert(Scale() > 1.f);
    assert(settings::Save(file)); settings::Set(settings::Id::CameraFov, 0);
    assert(settings::Load(file) && settings::GetInt(settings::Id::CameraFov) == 10 && Scale() > 1.f);
    assert(settings::Reset(settings::Id::CameraFov) == settings::SetResult::Applied && Scale() == 1.f);
    // Execute the production distance hook. Its original routine sees the
    // scoped parameter while all registers and the original return survive.
    auto chase = [&] {
        PPCContext c{}; c.r3.u32 = camera::kChase; c.r4.u32 = camera::kFrame;
        c.r5.u64 = 0xDEADBEEF12345678;
        const unsigned previous = chaseCalls;
        sub_82318848(c, g_memory.base);
        assert(chaseCalls == previous + 1 && c.r3.u64 == 0x12345678);
    };
    auto z = [&] { auto* base = g_memory.base; return PPC_LOAD_U32(kRecords + 104); };
    auto setup = [&] {
        Reset(); throwOriginal = false;
        Word(0x82AB1E40, kRecords); Word(camera::kChase + 20, kRecords + 96);
        Word(camera::kChase + 4, camera::kManager); Word(camera::kManager + 11904, 9);
        Word(camera::kManager + 11892, 0);
        Word(kRecords + 104, std::bit_cast<uint32_t>(-6.8f));
        settings::Set(settings::Id::CameraDistance, 150);
    };
    setup(); const uint32_t stockZ = z(); chase();
    assert(std::abs(observedZ + 10.2f) < 1e-5 && z() == stockZ);
    for (int i = 0; i < 100; ++i) { chase(); assert(z() == stockZ); }
    settings::Set(settings::Id::CameraDistance, 75); chase();
    assert(std::abs(observedZ + 5.1f) < 1e-5 && z() == stockZ);
    settings::Reset(settings::Id::CameraDistance); chase(); assert(observedZ == -6.8f);
    setup(); settings::Set(settings::Id::OnlineEnabled, 1); chase(); assert(std::abs(observedZ + 10.2f) < 1e-5);
    setup(); settings::Set(settings::Id::OnlineEnabled, 1); Word(codes::kSession, 0x8287C718); chase(); assert(observedZ == -6.8f);
    setup(); g_memory.base[kWrapper + 599] = 1; chase(); assert(observedZ == -6.8f);
    setup(); Word(rewind::kClockFlags, 0x01010000); chase(); assert(observedZ == -6.8f);
    setup(); Word(camera::kManager + 11904, 10); chase(); assert(observedZ == -6.8f);
    setup(); Word(camera::kManager + 11892, 1); chase(); assert(observedZ == -6.8f);
    setup(); Word(camera::kChase + 4, camera::kManager + 12000); chase(); assert(observedZ == -6.8f);
    setup(); Word(camera::kChase + 20, kRecords + 112); chase(); assert(observedZ == 0.f && z() == stockZ);
    for (float value : {0.f, 2.f, -1.f, -16.f, INFINITY, NAN})
    {
        setup(); const uint32_t bits = std::bit_cast<uint32_t>(value);
        Word(kRecords + 104, bits); chase(); assert(z() == bits);
        assert(std::bit_cast<uint32_t>(observedZ) == bits);
    }
    setup(); throwOriginal = true;
    try { chase(); assert(false); } catch (const std::runtime_error&) {}
    assert(z() == stockZ); throwOriginal = false;
    setup(); assert(settings::Save(file)); settings::Reset(settings::Id::CameraDistance);
    assert(settings::Load(file) && settings::GetInt(settings::Id::CameraDistance) == 150);
    chase(); assert(std::abs(observedZ + 10.2f) < 1e-5 && z() == stockZ);
    // The production additive-effect hook must execute the original in full,
    // keep its timer/FOV/return value, and attenuate only the current frame pose.
    constexpr uint32_t effect = camera::kManager + 9020;
    auto value = [&](uint32_t a) { auto* base = g_memory.base; return std::bit_cast<float>(PPC_LOAD_U32(a)); };
    const std::array<float, 7> initial = {0.f, -0.f, 0.f, 1.f, 10.f, 20.f, 30.f};
    auto prepareEffect = [&](int percent = 0, unsigned slot = 32) {
        setup(); settings::Set(settings::Id::CameraShake, percent);
        nativeQuaternionSign = 1.f; invalidEffect = false; unchangedEffect = false;
        effectAngle = std::numbers::pi / 12.; effectFrame = camera::kFrame;
        const uint32_t self = effect + (slot - 32) * 52;
        Word(self + 4, camera::kManager); Word(self + 44, slot); Word(self + 36, 3);
        Word(self + 20, std::bit_cast<uint32_t>(.5f)); Word(self + 28, std::bit_cast<uint32_t>(.6f));
        Word(camera::kManager + 2896 + slot * 4, self);
        for (unsigned i = 0; i < initial.size(); ++i)
            Word(effectFrame + 32 + i * 4, std::bit_cast<uint32_t>(initial[i]));
        return self;
    };
    auto shake = [&](uint32_t self = effect, uint32_t frame = camera::kFrame) {
        PPCContext c{}; c.r3.u32 = self; c.r4.u32 = frame; c.r5.u64 = 0x1234567812345678;
        const unsigned previous = effectCalls; sub_822F5128(c, g_memory.base);
        assert(effectCalls == previous + 1 && c.r3.u64 == 0x87654321);
        assert(value(effectFrame + 16) == 81.f); // Native FOV effect survives.
        assert(value(self + 20) == .625f && value(self + 28) == .6f);
    };
    for (unsigned slot : {32u, 33u, 34u})
    {
        const uint32_t self = prepareEffect(0, slot); shake(self);
        for (unsigned i = 0; i < initial.size(); ++i)
        { auto* base = g_memory.base; assert(PPC_LOAD_U32(effectFrame + 32 + i * 4) == std::bit_cast<uint32_t>(initial[i])); }
    }
    prepareEffect(50); shake();
    assert(std::abs(value(effectFrame + 40) - std::sin(std::numbers::pi / 24.)) < 1e-6);
    assert(std::abs(value(effectFrame + 44) - std::cos(std::numbers::pi / 24.)) < 1e-6);
    assert(value(effectFrame + 48) == 11.f && value(effectFrame + 52) == 22.f && value(effectFrame + 56) == 33.f);
    prepareEffect(50); nativeQuaternionSign = -1.f; shake();
    assert(std::abs(value(effectFrame + 40) - std::sin(std::numbers::pi / 24.)) < 1e-6);
    for (int i = 0; i < 100; ++i) { prepareEffect(50); shake(); assert(value(effectFrame + 48) == 11.f); }
    auto nativePose = [&] { assert(value(effectFrame + 48) == 12.f && value(effectFrame + 52) == 24.f); };
    prepareEffect(50); unchangedEffect = true; shake();
    for (unsigned i = 0; i < initial.size(); ++i)
    { auto* base = g_memory.base; assert(PPC_LOAD_U32(effectFrame + 32 + i * 4) == std::bit_cast<uint32_t>(initial[i])); }
    prepareEffect(50); effectAngle = .0001; shake();
    assert(std::abs(value(effectFrame + 40) - std::sin(.00005)) < 1e-7);
    prepareEffect(100); shake(); nativePose();
    prepareEffect(); settings::Set(settings::Id::OnlineEnabled, 1); shake(); assert(value(effectFrame + 48) == 10.f);
    prepareEffect(); settings::Set(settings::Id::OnlineEnabled, 1); Word(codes::kSession, 0x8287C718); shake(); nativePose();
    prepareEffect(); Word(codes::kLocalPlayers, 2); shake(); nativePose();
    prepareEffect(); g_memory.base[rewind::kWorldPlayback] = 1; shake(); nativePose();
    prepareEffect(); g_memory.base[kWrapper + 599] = 1; shake(); nativePose();
    prepareEffect(); Word(camera::kManager + 11904, 5); shake(); nativePose();
    prepareEffect(); Word(camera::kManager + 11892, 1); shake(); nativePose();
    prepareEffect(); Word(effect + 4, 0); shake(); nativePose();
    prepareEffect(); Word(effect + 44, 35); shake(); nativePose();
    prepareEffect(); Word(camera::kManager + 2896 + 32 * 4, 0); shake(); nativePose();
    prepareEffect(); shake(effect, camera::kFrame + 80); nativePose();
    prepareEffect(); Word(effectFrame + 44, 0); shake(); nativePose(); // Invalid input quaternion.
    prepareEffect(); invalidEffect = true; shake(); nativePose(); assert(std::isnan(value(effectFrame + 32)));
    prepareEffect(40); assert(settings::Save(file)); settings::Reset(settings::Id::CameraShake);
    assert(settings::GetInt(settings::Id::CameraShake) == 100);
    assert(settings::Load(file) && settings::GetInt(settings::Id::CameraShake) == 40);
    munmap(g_memory.base, PPC_MEMORY_SIZE); std::filesystem::remove_all(temp);
    puts("Camera: live FOV, original defaults, persistence, respawn, cinematic/Replay/multiplayer guards PASS");
}
