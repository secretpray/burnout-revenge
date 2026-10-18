// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Burnout Revenge's own hooks: its addresses only (no Most Wanted hooks or
// writes to its global data belong in this build). Burnout keeps its
// original timing; screens of another shape than 16:9 get Hor+ / Vert+ as in
// game/ultrawide.h, through the hooks below.
#include <stdafx.h>
#include "ultrawide.h"
#include "hud_anchor.h"
#include <user/settings.h>
#include <apu/xma_mmio.h>
#include <apu/voice_mix.h>
#include "frame_time.h"

#include <unordered_set>

namespace
{
    struct Scales
    {
        float x, y;
    };
    std::atomic<Scales> s_scales{ Scales{ 1.0f, 1.0f } };
    std::atomic<bool> s_hooksSeen{ false };
    const auto s_processStart = std::chrono::steady_clock::now();  // BURNOUT_LOG_2D's clock
}

namespace game
{
    void SetAspectScale(float scaleX, float scaleY)
    {
        const Scales next{ std::clamp(scaleX, 0.1f, 1.0f), std::clamp(scaleY, 0.5f, 1.0f) };
        const Scales last = s_scales.exchange(next, std::memory_order_relaxed);
        if (last.x != next.x || last.y != next.y)
            fprintf(stderr, "[uw] view scale x %.4f y %.4f\n", next.x, next.y);
    }
    float AspectScaleX() { return s_scales.load(std::memory_order_relaxed).x; }
    float AspectScaleY() { return s_scales.load(std::memory_order_relaxed).y; }
    bool HooksActive() { return s_hooksSeen.load(std::memory_order_relaxed); }
    bool TallHooksActive() { return s_hooksSeen.load(std::memory_order_relaxed); }
    float FrontEndScaleX() { return 0.0f; }
    float FrontEndScaleY() { return 0.0f; }
    FrameTimeStats GetFrameTimeStats() { return {}; }
    double FrameClockMs() { return 0.0; }
}

// Hor+ / Vert+ (game/ultrawide.h). sub_82353BA8 is the camera's FOV setter,
// called by each camera's per-frame update (sub_8235A080) and at setup
// (sub_8215C250): from the FOV f1 (degrees) it stores the view window,
// x = m * tan(f1 / 2) at +72 (m at +112: 1.2 in widescreen, the game's own
// partial Hor+ from 4:3) and y = x / aspect at +76 (aspect at +116: 16/9 or
// 4/3 by the screen manager's widescreen flag), 1/x and 1/y at +80 and +84,
// and hands +72 to the render camera at +88 (sub_8234ABC0: its view window
// at +88/+92, the reciprocals at +96/+100, from which the projection and the
// frustum are built). Widening x by 1/scaleX (Hor+) or y by 1/scaleY (Vert+)
// here widens the projection and the culling together.
extern "C" PPC_FUNC(__imp__sub_82353BA8);
PPC_EXTERN_FUNC(sub_8234ABC0);
PPC_FUNC(sub_82353BA8)
{
    const uint32_t camera = ctx.r3.u32;
    const double fov = ctx.f1.f64;
    const uint32_t caller = uint32_t(ctx.lr);
    __imp__sub_82353BA8(ctx, base);
    if (!s_hooksSeen.load(std::memory_order_relaxed))
        s_hooksSeen.store(true, std::memory_order_relaxed);

    static const bool log = std::getenv("BR_LOG_CAMERAS") != nullptr;
    if (log)
    {
        static std::mutex mutex;
        static std::unordered_set<uint32_t> seen;
        std::lock_guard lock(mutex);
        if (seen.size() < 64 && seen.insert(camera).second)
        {
            auto f = [base](uint32_t a) { uint32_t u = PPC_LOAD_U32(a); float v; memcpy(&v, &u, 4); return v; };
            fprintf(stderr, "[uw] camera %08X lr %08X fov %.2f m %.3f aspect %.4f window %.4f x %.4f render %08X\n",
                camera, caller, fov, f(camera + 112), f(camera + 116), f(camera + 72), f(camera + 76),
                PPC_LOAD_U32(camera + 88));
        }
    }

    const Scales scale = s_scales.load(std::memory_order_relaxed);
    if (scale.x == 1.0f && scale.y == 1.0f)
        return;
    auto loadF = [base](uint32_t a) { uint32_t u = PPC_LOAD_U32(a); float v; memcpy(&v, &u, 4); return v; };
    auto storeF = [base](uint32_t a, float v) { uint32_t u; memcpy(&u, &v, 4); PPC_STORE_U32(a, u); };
    const float x = loadF(camera + 72) / scale.x, y = loadF(camera + 76) / scale.y;
    storeF(camera + 72, x);
    storeF(camera + 76, y);
    storeF(camera + 80, 1.0f / x);
    storeF(camera + 84, 1.0f / y);
    ctx.r3.u32 = PPC_LOAD_U32(camera + 88);
    ctx.r4.u32 = camera + 72;
    sub_8234ABC0(ctx, base);
}

// The HUD and the menus (2D, in 1280x720 pixels): sub_82355FD0 flushes a
// batch of them, as a DrawPrimitiveUP (sub_82380780 copies the vertices into
// the command buffer; the batch is empty again after it). The batch object
// r3 holds up to 16 KB of vertices from its start, the write pointer at
// +16384 and the vertex format's index at +16388; the format's block at
// r3 + index * 32 has the primitive type at +16468 (D3D: 4 triangle list,
// 8 rectangle list, 13 quad list) and the stride at +16476. Every vertex
// begins with its x and y in pixels: the vertex shaders compute clip xy =
// pixel xy * c1.xy + c0.xy, c0 = (-1, 1, z, 1) from 0x82697910 (z at +8,
// sub_823567F0) and c1 = (2/w, -2/h, 0, 0) from 0x82697920 (sub_82356350,
// from the back buffer's size), which this function copies into the
// device's constants (device + 1920 and + 1936).
// Under Hor+ the presenter stretches the frame by 1/scaleX in x, so the
// flush gets c0.x and c1.x times scaleX (and y alike for Vert+): the HUD is
// narrowed about the frame's centre and keeps its 16:9 shape and size in
// the middle. The constants stay in the device after the flush, and some 2D
// drawn by other paths uses them (a button's icon beside its label), so it
// is narrowed with the rest. A primitive whose every vertex is at or past
// the frame's left or right edge, within 1% (the fades, the dims, the
// cinematic bars; the pause dim ends at x = 1278.4), is widened here by the
// inverse so it still spans the screen; one that also reaches the top and
// bottom, in y too. Strips and fans are one primitive here (the whole
// batch). gpu/renderer.cpp does the rest per draw: full-screen layers from
// other paths, 2D in the world (depth-tested: the markers over the cars),
// and clipping the HUD to the 16:9 band (the menus park elements past the
// frame's edges).
namespace
{
    // A flush of the race HUD (and the crash camera's, the countdown's, the
    // event messages'): its callers' code, two frames up the guest's back
    // chain, is the HUD's (0x82180000-0x821A0000), or it is drawn by the HUD
    // scene's loop (0x82353340) or the rating widget (0x8235DD34). The menus,
    // the pause menu and the results come from the front end's scene
    // (0x8235C..., 0x8235F..., 0x8246BD88); the markers over the cars from
    // 0x820BE07C (BURNOUT_LOG_2D-style survey, 18.10).
    bool IsRaceHud(PPCContext& ctx, uint8_t* base)
    {
        const uint32_t frame1 = PPC_LOAD_U32(ctx.r1.u32), lr2 = PPC_LOAD_U32(frame1 - 8);
        const uint32_t frame2 = PPC_LOAD_U32(frame1), lr3 = PPC_LOAD_U32(frame2 - 8);
        auto hudCode = [](uint32_t a) { return a >= 0x82180000 && a < 0x821A0000; };
        return hudCode(lr2) || hudCode(lr3) || lr3 == 0x82353340 || lr3 == 0x8235DD34;
    }
}

extern "C" PPC_FUNC(__imp__sub_82355FD0);
PPC_FUNC(sub_82355FD0)
{
    constexpr uint32_t kOrigin = 0x82697910, kScale = 0x82697920;
    const Scales scale = s_scales.load(std::memory_order_relaxed);
    const uint32_t batch = ctx.r3.u32;
    const uint32_t end = PPC_LOAD_U32(batch + 16384);
    if ((scale.x == 1.0f && scale.y == 1.0f) || end <= batch || end - batch > 16384)
        return __imp__sub_82355FD0(ctx, base);

    auto loadF = [base](uint32_t a) { uint32_t u = PPC_LOAD_U32(a); float v; memcpy(&v, &u, 4); return v; };
    auto storeF = [base](uint32_t a, float v) { uint32_t u; memcpy(&u, &v, 4); PPC_STORE_U32(a, u); };
    const uint32_t format = batch + PPC_LOAD_U32(batch + 16388) * 32;
    const uint32_t type = PPC_LOAD_U32(format + 16468), stride = PPC_LOAD_U32(format + 16476);
    const float c1x = loadF(kScale), c1y = loadF(kScale + 4);
    if (c1x == 0.0f || c1y == 0.0f)
        return __imp__sub_82355FD0(ctx, base);
    const float w = std::fabs(2.0f / c1x), h = std::fabs(2.0f / c1y);
    const uint32_t count = stride >= 8 && stride <= 256 ? (end - batch) / stride : 0;
    const uint32_t per = type == 4 || type == 8 ? 3 : type == 13 ? 4 : count;
    // BURNOUT_LOG_2D=<seconds>: from then on, each flush (up to 4000): its
    // callers up the guest's back chain, primitive type, vertex count and
    // box in frame pixels (how the HUD's and the menus' pieces were told
    // apart).
    if (static const char* from = std::getenv("BURNOUT_LOG_2D"); from && count)
    {
        static std::atomic<int> lines{ 0 };
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_processStart).count();
        if (t >= std::atof(from) && lines.fetch_add(1) < 4000)
        {
            float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
            for (uint32_t i = 0; i < count; i++)
            {
                const float x = loadF(batch + i * stride), y = loadF(batch + i * stride + 4);
                x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
            }
            const uint32_t f1 = PPC_LOAD_U32(ctx.r1.u32), f2 = PPC_LOAD_U32(f1);
            fprintf(stderr, "[2d] t %.3f lr %08X %08X %08X type %u n %u stride %u box %.0f,%.0f-%.0f,%.0f\n", t, uint32_t(ctx.lr),
                PPC_LOAD_U32(f1 - 8), PPC_LOAD_U32(f2 - 8), type, count, stride, x0, y0, x1, y1);
        }
    }
    // The race HUD at the screen's edges (game/hud_anchor.h): a piece of it
    // moves with its element, by a shift of c0.x below; a row across the frame
    // gets its 16:9 width back from c1.x (and so, as any 2D not narrowed, is
    // left whole by the renderer).
    float shift = 0.0f, widen = 1.0f;
    game::hud::Box box{ 1e30f, 1e30f, -1e30f, -1e30f };
    for (uint32_t i = 0; i < count; i++)
    {
        const float x = loadF(batch + i * stride), y = loadF(batch + i * stride + 4);
        box = { std::min(box.x0, x), std::min(box.y0, y), std::max(box.x1, x), std::max(box.y1, y) };
    }
    // The menus' grimy frame, stretched to an ultrawide's edges: its columns
    // moved, the rows between them widened (game/hud_anchor.h).
    if (scale.x < 1.0f && count && w == 1280.0f && game::hud::IsMenuFrameTile(box))
    {
        if (box.x1 <= 128.0f || box.x0 >= 1151.0f)
            shift = box.x1 <= 128.0f ? -game::hud::EdgeShift(scale.x) : game::hud::EdgeShift(scale.x);
        else
            widen = game::hud::MenuFrameStretch(scale.x);
    }
    else if (scale.x < 1.0f && count && settings::GetBool(settings::Id::HudAtEdges) && IsRaceHud(ctx, base))
    {
        const bool spans = box.x0 <= 0.01f * w && box.x1 >= 0.99f * w;
        if (!spans && w == 1280.0f)
        {
            static std::mutex mutex;
            static game::hud::Anchor anchor;
            const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            std::lock_guard lock(mutex);
            const game::hud::Place place = anchor.Decide(box, now);
            widen = place == game::hud::Place::Row ? 1.0f / scale.x : 1.0f;
            shift = place == game::hud::Place::Left ? -game::hud::EdgeShift(scale.x) : place == game::hud::Place::Right ? game::hud::EdgeShift(scale.x) : 0.0f;
        }
    }
    for (uint32_t first = 0; first + per <= count && per; first += per)
    {
        // Every vertex at or past an edge, and both edges reached (a sprite
        // sliding in from past the right edge is not a layer).
        bool fullWidth = true, fullHeight = true;
        float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
        for (uint32_t i = 0; i < per; i++)
        {
            const uint32_t v = batch + (first + i) * stride;
            const float x = loadF(v), y = loadF(v + 4);
            fullWidth &= x <= 0.01f * w || x >= 0.99f * w;
            fullHeight &= y <= 0.01f * h || y >= 0.99f * h;
            minX = std::min(minX, x), maxX = std::max(maxX, x);
            minY = std::min(minY, y), maxY = std::max(maxY, y);
        }
        fullWidth &= minX <= 0.01f * w && maxX >= 0.99f * w;
        fullHeight &= minY <= 0.01f * h && maxY >= 0.99f * h;
        // Narrowed by the constants below: widened back here.
        const float wx = fullWidth ? 1.0f / scale.x : 1.0f, wy = fullWidth && fullHeight ? 1.0f / scale.y : 1.0f;
        if (wx == 1.0f && wy == 1.0f)
            continue;
        for (uint32_t i = 0; i < per; i++)
        {
            const uint32_t v = batch + (first + i) * stride;
            storeF(v, 0.5f * w + (loadF(v) - 0.5f * w) * wx);
            storeF(v + 4, 0.5f * h + (loadF(v + 4) - 0.5f * h) * wy);
        }
    }
    const float originX = loadF(kOrigin), originY = loadF(kOrigin + 4);
    // The shift in clip space: a pixel's x * c1.x * s (+-(1 - s)), which
    // gpu/renderer.cpp tells by c0.x and leaves unclipped; widen scales x
    // about the frame's middle.
    storeF(kOrigin, originX * scale.x * widen + shift * c1x * scale.x);
    storeF(kOrigin + 4, originY * scale.y);
    storeF(kScale, c1x * scale.x * widen);
    storeF(kScale + 4, c1y * scale.y);
    __imp__sub_82355FD0(ctx, base);
    storeF(kOrigin, originX);
    storeF(kOrigin + 4, originY);
    storeF(kScale, c1x);
    storeF(kScale + 4, c1y);
}

// This leaf function reads the hardware context-array base into the game's
// XAudio global. Seed the register before running its original instructions.
extern "C" PPC_FUNC(__imp__sub_82566870);
PPC_FUNC(sub_82566870)
{
    apu::InitializeXmaMmio();
    __imp__sub_82566870(ctx, base);
}

// XAudio's voice mix (apu/voice_mix.h), native. It keeps the guest's
// steps around the mix: the send's voices, the overwrite send's format
// change (sub_8256E7C0) and the recursive lock at 0x82D3DCF0.
// BR_GUEST_VOICE_MIX=1 runs the recompiled function instead;
// BR_VOICE_MIX_RECORD=<file> does too and writes every
// BR_VOICE_MIX_RECORD_STRIDE-th call's inputs and results (default 50, up
// to 48 MB) for tests/voice_mix_test.cpp to replay.
extern "C" PPC_FUNC(__imp__sub_8256D640);
PPC_EXTERN_FUNC(sub_8256E7C0);
PPC_EXTERN_FUNC(__imp__KeRaiseIrqlToDpcLevel);
PPC_EXTERN_FUNC(__imp__KeAcquireSpinLockAtRaisedIrql);
PPC_EXTERN_FUNC(__imp__KeReleaseSpinLockFromRaisedIrql);
PPC_EXTERN_FUNC(__imp__KfLowerIrql);

namespace
{
    constexpr uint32_t kMixLock = 0x82D3DCF0;

    apu::VoiceMixConstants ReadMixConstants(uint8_t* base)
    {
        auto f = [base](uint32_t addr) {
            uint32_t u = PPC_LOAD_U32(addr);
            float v;
            memcpy(&v, &u, 4);
            return v;
        };
        return { f(0x8200FD8C), f(0x8200F9A4), { f(0x82000B30), f(0x82000B34), f(0x82009594), f(0x8202C4D4) } };
    }

    // What sub_8256E650 reads of a voice: channel count and buffer.
    struct MixVoiceInfo
    {
        uint32_t channels;
        uint32_t buffer;
    };

    MixVoiceInfo ReadMixVoice(uint8_t* base, uint32_t voice)
    {
        uint32_t header = voice ? voice - 8 : 0;
        return { PPC_LOAD_U8(header + 21), PPC_LOAD_U32(header + 28) };
    }

    class VoiceMixRecorder
    {
    public:
        static VoiceMixRecorder* Get()
        {
            static VoiceMixRecorder* recorder = [] () -> VoiceMixRecorder* {
                const char* path = std::getenv("BR_VOICE_MIX_RECORD");
                return path && *path ? new VoiceMixRecorder(path) : nullptr;
            }();
            return recorder;
        }

        void Run(PPCContext& ctx, uint8_t* base)
        {
            std::lock_guard lock(m_mutex);
            if (!m_file || m_calls++ % m_stride != 0 || m_bytes > (48u << 20))
            {
                __imp__sub_8256D640(ctx, base);
                return;
            }
            uint32_t send = ctx.r3.u32;
            MixVoiceInfo src = ReadMixVoice(base, ctx.r4.u32), dst = ReadMixVoice(base, ctx.r5.u32);
            uint8_t mode = PPC_LOAD_U8(send + 26);
            if (mode == 1)
                dst.channels = PPC_LOAD_U8(send + 25);
            uint32_t entryCount = PPC_LOAD_U8(send + 16);
            uint32_t entries = PPC_LOAD_U32(send + 20);
            uint8_t head[4] = { mode, uint8_t(src.channels), uint8_t(dst.channels), uint8_t(ctx.r4.u32 == ctx.r5.u32) };
            std::vector<uint8_t> before;
            Append(before, head, 4);
            AppendU32(before, entryCount);
            uint32_t entryBytes = entryCount * apu::kVoiceMixEntryBytes;
            uint32_t srcBytes = src.channels * apu::kVoiceMixChannelBytes;
            uint32_t dstBytes = dst.channels * apu::kVoiceMixChannelBytes;
            std::vector<uint8_t> state;
            Append(state, base + entries, entryBytes);
            Append(state, base + src.buffer, srcBytes);
            Append(state, base + dst.buffer, dstBytes);
            __imp__sub_8256D640(ctx, base);
            AppendU32(before, ctx.r3.u32);
            Append(before, state.data(), state.size());
            Append(before, base + entries, entryBytes);
            Append(before, base + dst.buffer, dstBytes);
            uint32_t size = uint32_t(before.size());
            fwrite(&size, 4, 1, m_file);
            fwrite(before.data(), 1, size, m_file);
            fflush(m_file);
            m_bytes += size + 4;
        }

    private:
        explicit VoiceMixRecorder(const char* path) : m_file(fopen(path, "wb"))
        {
            if (const char* v = std::getenv("BR_VOICE_MIX_RECORD_STRIDE"))
                m_stride = std::max(1, atoi(v));
            if (!m_file)
                return;
            apu::VoiceMixConstants k = ReadMixConstants(g_memory.base);
            fwrite("SBVMIX1\n", 1, 8, m_file);
            fwrite(&k, sizeof(k), 1, m_file);
        }

        static void Append(std::vector<uint8_t>& out, const void* p, size_t n)
        {
            out.insert(out.end(), (const uint8_t*)p, (const uint8_t*)p + n);
        }

        static void AppendU32(std::vector<uint8_t>& out, uint32_t v) { Append(out, &v, 4); }

        std::mutex m_mutex;
        FILE* m_file;
        uint32_t m_stride = 50;
        uint64_t m_calls = 0;
        uint64_t m_bytes = 0;
    };
}

PPC_FUNC(sub_8256D640)
{
    static const bool guest = [] { const char* v = std::getenv("BR_GUEST_VOICE_MIX"); return v && v[0] == '1'; }();
    if (VoiceMixRecorder* recorder = VoiceMixRecorder::Get()) [[unlikely]]
        return recorder->Run(ctx, base);
    if (guest) [[unlikely]]
        return __imp__sub_8256D640(ctx, base);

    uint32_t send = ctx.r3.u32, srcVoice = ctx.r4.u32, dstVoice = ctx.r5.u32;
    MixVoiceInfo src = ReadMixVoice(base, srcVoice), dst = ReadMixVoice(base, dstVoice);
    if (PPC_LOAD_U8(send + 26) == 1)
    {
        // The overwrite send sets its output's format first, from a copy of
        // what sub_8256E650 read, in a frame of the guest's stack.
        uint32_t sp = ctx.r1.u32, frame = sp - 272;
        PPC_STORE_U32(frame, sp);
        uint32_t format = frame + 96;
        uint32_t header = dstVoice ? dstVoice - 8 : 0;
        PPC_STORE_U32(format + 0, PPC_LOAD_U32(header + 20));
        PPC_STORE_U32(format + 4, PPC_LOAD_U32(header + 24));
        PPC_STORE_U32(format + 8, PPC_LOAD_U32(header + 28));
        PPC_STORE_U8(format + 1, PPC_LOAD_U8(send + 25));
        ctx.r1.u32 = frame;
        ctx.r3.u32 = header;
        ctx.r4.u32 = format;
        ctx.r5.u64 = 1;
        sub_8256E7C0(ctx, base);
        ctx.r1.u32 = sp;
        if (ctx.r3.s32 < 0)
            return;
        dst.channels = PPC_LOAD_U8(format + 1);
        dst.buffer = PPC_LOAD_U32(format + 8);
    }

    // The lock, as the guest takes it: recursive per thread (r13).
    __imp__KeRaiseIrqlToDpcLevel(ctx, base);
    uint32_t irql = ctx.r3.u32;
    uint32_t thread = ctx.r13.u32;
    uint32_t depth = PPC_LOAD_U32(kMixLock + 4);
    if (depth == 0 || PPC_LOAD_U32(kMixLock + 8) != thread)
    {
        ctx.r3.u32 = kMixLock;
        __imp__KeAcquireSpinLockAtRaisedIrql(ctx, base);
        PPC_STORE_U32(kMixLock + 8, thread);
        PPC_STORE_U8(kMixLock + 12, uint8_t(irql));
        depth = PPC_LOAD_U32(kMixLock + 4);
    }
    PPC_STORE_U32(kMixLock + 4, depth + 1);

    apu::VoiceMixCall call{};
    call.entryCount = PPC_LOAD_U8(send + 16);
    call.entries = base + PPC_LOAD_U32(send + 20);
    call.src = base + src.buffer;
    call.srcChannels = src.channels;
    call.dst = base + dst.buffer;
    call.dstChannels = dst.channels;
    call.mode = PPC_LOAD_U8(send + 26);
    if (call.entryCount != 0 || srcVoice != dstVoice)
    {
        static const apu::VoiceMixConstants constants = ReadMixConstants(base);
        apu::MixVoice(call, constants, ctx.fpscr);
    }

    depth = PPC_LOAD_U32(kMixLock + 4);
    if (depth != 0 && PPC_LOAD_U32(kMixLock + 8) == ctx.r13.u32)
    {
        PPC_STORE_U32(kMixLock + 4, depth - 1);
        if (depth - 1 == 0)
        {
            uint32_t saved = PPC_LOAD_U8(kMixLock + 12);
            PPC_STORE_U8(kMixLock + 12, 0);
            PPC_STORE_U32(kMixLock + 8, 0);
            ctx.r3.u32 = kMixLock;
            __imp__KeReleaseSpinLockFromRaisedIrql(ctx, base);
            ctx.r3.u64 = saved;
            __imp__KfLowerIrql(ctx, base);
        }
    }
    ctx.r3.u64 = 0;
}

// The game's mix snapshots. sub_822AC748 (called by sub_822BE008) blends the
// snapshots whose bits are set in the mixer's mask at +16 (16 of them) into
// the channel faders at 0x82D13140 (20 bytes each, level at +4). Channel 20
// is the music: sub_822ADC90 hands its level times the music volume to
// XMPSetVolume. Seen in runs 149/150: 0001 racing and in Replay, 0003 a
// crash (music silent for its 4-5 s, in Replay too), 8000 ducks the music to
// 0.7, 0800 loading. Each change of the mask is logged with the music fader,
// so a log tells which snapshot silenced the music.
extern "C" PPC_FUNC(__imp__sub_822AC748);
PPC_FUNC(sub_822AC748)
{
    constexpr uint32_t kFaders = 0x82D13140;
    constexpr uint32_t kMusic = 20;
    auto loadF = [base](uint32_t a) { uint32_t u = PPC_LOAD_U32(a); float v; memcpy(&v, &u, 4); return v; };
    const uint32_t mask = PPC_LOAD_U32(ctx.r3.u32 + 16) & 0xFFFF;
    static std::atomic<uint32_t> s_lastMask{ 0xFFFFFFFF };
    if (s_lastMask.exchange(mask, std::memory_order_relaxed) != mask)
    {
        fprintf(stderr, "[mix] snapshots %04X, music fader %.3f\n", mask, loadF(kFaders + kMusic * 20 + 4));
    }
    __imp__sub_822AC748(ctx, base);
}
