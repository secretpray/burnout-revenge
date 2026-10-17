// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// apu::MixVoice (apu/voice_mix.h), Burnout Revenge's XAudio voice mix,
// against the recompiled guest function (sub_8256D640) written out lane by
// lane: the same bits for random sends (both modes, channels out of range,
// sends without entries, denormal and huge samples, flushed or not).
//
// With a file argument (or BR_VOICE_MIX_RECORDING) it also replays calls
// the game made, recorded with BR_VOICE_MIX_RECORD=<file>: each call's
// inputs through MixVoice, and its buffers and volumes after against what
// the recompiled function left.
#include <apu/voice_mix.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace
{
    // PPCFPSCRRegister's flush-mode calls on the host's FPCR / MXCSR.
    struct TestFpscr
    {
        bool on = false;
        static void Set(bool flush)
        {
#if defined(__aarch64__)
            uint64_t r;
            __asm__ __volatile__("mrs %0, fpcr" : "=r"(r));
            constexpr uint64_t mask = (1u << 19) | (1u << 24);
            r = flush ? r | mask : r & ~mask;
            __asm__ __volatile__("msr fpcr, %0" : : "r"(r) : "memory");
#elif defined(__x86_64__)
            uint32_t r;
            __asm__ __volatile__("stmxcsr %0" : "=m"(r));
            r = flush ? r | 0x8040u : r & ~0x8040u;
            __asm__ __volatile__("ldmxcsr %0" : : "m"(r) : "memory");
#endif
        }
        void enableFlushMode() { if (!on) Set(on = true); }
        void disableFlushMode() { if (on) Set(on = false); }
        void enableFlushModeUnconditional() { Set(on = true); }
        void disableFlushModeUnconditional() { Set(on = false); }
    };

    // Keeps v's computation on its side of a flush-mode switch.
    void Pin(float& v)
    {
#if defined(__aarch64__)
        __asm__ __volatile__("" : "+w"(v) : : "memory");
#else
        __asm__ __volatile__("" : "+m"(v) : : "memory");
#endif
    }

    float Get(const uint8_t* p)
    {
        uint32_t u = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
        float f;
        memcpy(&f, &u, 4);
        return f;
    }

    void Put(uint8_t* p, float f)
    {
        uint32_t u;
        memcpy(&u, &f, 4);
        p[0] = uint8_t(u >> 24), p[1] = uint8_t(u >> 16), p[2] = uint8_t(u >> 8), p[3] = uint8_t(u);
    }

    // The recompiled function's mix, one AltiVec vector (4 lanes) at a time
    // as its loops go, each operation its own statement (built without
    // contraction).
    void Reference(const apu::VoiceMixCall& c, const apu::VoiceMixConstants& k, TestFpscr& fpscr)
    {
        constexpr uint32_t kVectors = 64;
        if (c.entryCount == 0)
        {
            for (uint32_t ch = 0; ch < c.dstChannels; ch++)
            {
                uint8_t* dst = c.dst + ch * 1024;
                const uint8_t* src = c.src + ch * 1024;
                if (ch >= c.srcChannels)
                {
                    if (c.mode == 0)
                        break;
                    for (uint32_t i = 0; i < 1024; i++)
                        dst[i] = 0;
                    continue;
                }
                if (c.mode != 0)
                {
                    for (uint32_t i = 0; i < 1024; i++)
                        dst[i] = src[i];
                    continue;
                }
                for (uint32_t v = 0; v < kVectors; v++)
                    for (uint32_t l = 0; l < 4; l++)
                    {
                        fpscr.enableFlushMode();
                        float sum = Get(src + v * 16 + l * 4) + Get(dst + v * 16 + l * 4);
                        Put(dst + v * 16 + l * 4, sum);
                    }
            }
            return;
        }
        for (uint32_t e = 0; e < c.entryCount; e++)
        {
            uint8_t* entry = c.entries + e * 12;
            fpscr.disableFlushMode();
            float target = Get(entry + 8);
            if (entry[1] >= c.dstChannels)
                continue;
            uint8_t* dst = c.dst + entry[1] * 1024;
            if (entry[0] >= c.srcChannels)
            {
                if (c.mode != 0)
                    for (uint32_t i = 0; i < 1024; i++)
                        dst[i] = 0;
                continue;
            }
            const uint8_t* src = c.src + entry[0] * 1024;
            float current = Get(entry + 4);
            float step = target - current;
            step = step * k.rampScale;
            float step4 = step * k.stepScale;
            Pin(current), Pin(step), Pin(step4);
            fpscr.enableFlushModeUnconditional();
            Pin(current), Pin(step), Pin(step4);
            float vol[4];
            for (uint32_t l = 0; l < 4; l++)
            {
                float t = step * k.lane[l];
                vol[l] = t + current;
            }
            for (uint32_t v = 0; v < kVectors; v++)
            {
                fpscr.enableFlushMode();
                for (uint32_t l = 0; l < 4; l++)
                {
                    float product = Get(src + v * 16 + l * 4) * vol[l];
                    if (c.mode == 0)
                    {
                        float sum = product + Get(dst + v * 16 + l * 4);
                        product = sum;
                    }
                    Put(dst + v * 16 + l * 4, product);
                    float next = vol[l] + step4;
                    vol[l] = next;
                }
            }
            fpscr.disableFlushModeUnconditional();
            Put(entry + 4, target);
        }
    }

    // A float worth mixing: mostly samples and volumes, sometimes zero,
    // denormal, huge or negative zero.
    float RandomValue(std::mt19937& rng, float scale)
    {
        std::uniform_real_distribution<float> uni(-scale, scale);
        switch (rng() % 16)
        {
        case 0: return 0.0f;
        case 1: return -0.0f;
        case 2: { uint32_t u = (rng() & 0x007FFFFFu) | (rng() & 0x80000000u); float f; memcpy(&f, &u, 4); return f; }
        case 3: return uni(rng) * 1e30f;
        case 4: return uni(rng) * 1e-37f;
        default: return uni(rng);
        }
    }

    struct Case
    {
        std::vector<uint8_t> entries, src, dst;
        uint32_t entryCount, srcChannels, dstChannels;
        uint8_t mode;

        apu::VoiceMixCall Call()
        {
            return { entries.data(), entryCount, src.data(), srcChannels, dst.data(), dstChannels, mode };
        }
    };

    uint32_t CompareAll(const Case& a, const Case& b)
    {
        return (a.entries != b.entries) + (a.dst != b.dst) + (a.src != b.src);
    }

    void RandomCases(const apu::VoiceMixConstants& k)
    {
        std::mt19937 rng(20261016);
        for (int n = 0; n < 4000; n++)
        {
            Case c;
            c.srcChannels = rng() % 7;
            c.dstChannels = rng() % 7;
            c.entryCount = rng() % 4 == 0 ? 0 : rng() % 13;
            c.mode = uint8_t(rng() % 4 == 0 ? 0 : rng() % 3);
            if (rng() % 2)
                c.mode = 0;
            c.entries.resize(c.entryCount * 12);
            for (uint32_t e = 0; e < c.entryCount; e++)
            {
                uint8_t* p = c.entries.data() + e * 12;
                p[0] = uint8_t(rng() % 8);  // out of range now and then
                p[1] = uint8_t(rng() % 8);
                p[2] = uint8_t(rng()), p[3] = uint8_t(rng());
                Put(p + 4, RandomValue(rng, 2.0f));
                Put(p + 8, rng() % 4 == 0 ? Get(p + 4) : RandomValue(rng, 2.0f));
            }
            c.src.resize(std::max(c.srcChannels, 1u) * 1024);
            c.dst.resize(std::max(c.dstChannels, 1u) * 1024);
            for (size_t i = 0; i < c.src.size(); i += 4)
                Put(c.src.data() + i, RandomValue(rng, 1.0f));
            for (size_t i = 0; i < c.dst.size(); i += 4)
                Put(c.dst.data() + i, RandomValue(rng, 4.0f));
            for (bool flushAtStart : { false, true })
            {
                Case want = c, got = c;
                TestFpscr f1, f2;
                if (flushAtStart)
                    f1.enableFlushModeUnconditional();
                Reference(want.Call(), k, f1);
                bool endRef = f1.on;
                f1.disableFlushModeUnconditional();
                if (flushAtStart)
                    f2.enableFlushModeUnconditional();
                apu::MixVoice(got.Call(), k, f2);
                bool endGot = f2.on;
                f2.disableFlushModeUnconditional();
                assert(CompareAll(want, got) == 0);
                assert(endRef == endGot);
            }
        }
        printf("voice_mix_test: 8000 random calls match\n");
    }

    uint32_t ReadU32(const uint8_t*& p)
    {
        uint32_t v;
        memcpy(&v, p, 4);
        p += 4;
        return v;
    }

    // A recording from the game (game/burnout.cpp): 8-byte magic, the
    // constants, then calls: size, mode, source and destination channels,
    // same-voice flag, entry count, the function's result, entries, source
    // and destination before; entries and destination after.
    int Replay(const char* path)
    {
        FILE* f = fopen(path, "rb");
        if (!f)
        {
            fprintf(stderr, "voice_mix_test: can't open %s\n", path);
            return 1;
        }
        char magic[8];
        apu::VoiceMixConstants k;
        if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "SBVMIX1\n", 8) != 0 || fread(&k, sizeof(k), 1, f) != 1)
        {
            fprintf(stderr, "voice_mix_test: %s isn't a recording\n", path);
            return 1;
        }
        printf("voice_mix_test: constants %g %g { %g %g %g %g }\n", k.rampScale, k.stepScale, k.lane[0], k.lane[1], k.lane[2], k.lane[3]);
        uint32_t calls = 0, entries = 0, sums = 0, overwrites = 0, failed = 0, mismatched = 0;
        uint32_t size;
        std::vector<uint8_t> rec;
        while (fread(&size, 4, 1, f) == 1)
        {
            rec.resize(size);
            if (fread(rec.data(), 1, size, f) != size)
                break;
            const uint8_t* p = rec.data();
            uint8_t mode = p[0], srcCh = p[1], dstCh = p[2], same = p[3];
            p += 4;
            uint32_t count = ReadU32(p);
            int32_t result = int32_t(ReadU32(p));
            Case c;
            c.entryCount = count, c.srcChannels = srcCh, c.dstChannels = dstCh, c.mode = mode;
            c.entries.assign(p, p + count * 12), p += count * 12;
            c.src.assign(p, p + srcCh * 1024), p += srcCh * 1024;
            c.dst.assign(p, p + dstCh * 1024), p += dstCh * 1024;
            std::vector<uint8_t> wantEntries(p, p + count * 12);
            p += count * 12;
            std::vector<uint8_t> wantDst(p, p + dstCh * 1024);
            calls++;
            if (result < 0)
            {
                // The format change refused it: nothing mixed.
                failed++;
                mismatched += wantEntries != c.entries || wantDst != c.dst;
                continue;
            }
            if (count == 0 && same)
            {
                mismatched += wantDst != c.dst;
                continue;
            }
            TestFpscr fpscr;
            apu::MixVoice(c.Call(), k, fpscr);
            fpscr.disableFlushModeUnconditional();
            entries += count;
            sums += count == 0;
            overwrites += mode != 0;
            if (wantEntries != c.entries || wantDst != c.dst)
            {
                if (mismatched < 5)
                    fprintf(stderr, "voice_mix_test: call %u differs (mode %u, %u entries, %u -> %u channels)\n", calls, mode, count, srcCh, dstCh);
                mismatched++;
            }
        }
        fclose(f);
        printf("voice_mix_test: %u recorded calls (%u entries, %u without entries, %u overwriting, %u refused), %u differ\n",
               calls, entries, sums, overwrites, failed, mismatched);
        return calls == 0 || mismatched != 0;
    }
}

int main(int argc, char** argv)
{
    // The values read from Burnout's image (recorded alongside the calls).
    apu::VoiceMixConstants k{ 1.0f / 256.0f, 4.0f, { 0.0f, 1.0f, 2.0f, 3.0f } };
    RandomCases(k);
    // Other constants: the code reads them, it doesn't assume them.
    RandomCases({ 0.00390625f * 1.5f, 3.0f, { 0.5f, -1.0f, 7.0f, 1e-3f } });
    const char* path = argc > 1 ? argv[1] : std::getenv("BR_VOICE_MIX_RECORDING");
    return path && *path ? Replay(path) : 0;
}
