// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Burnout Revenge's XAudio voice mix (guest sub_8256D640) without its lock
// and format check, header only so tests/voice_mix_test.cpp builds it alone.
// game/burnout.cpp runs it in place of the recompiled function.
//
// Every 256-sample frame the game sends each voice into its output voice:
// for each entry of the send's matrix, one source channel times a volume
// that ramps linearly from the entry's current to its target over the
// frame, added into (or, in the send's overwrite mode, written over) one
// destination channel. A send without entries adds channel to channel.
// Buffers are 256 big-endian floats per channel, channel after channel.
//
// The recompiled loop was 42% of the audio thread in a race (M1 Pro,
// 2026-10-16): each AltiVec load and store goes through a byte shuffle,
// a guest-stack store and a flush-mode check. The arithmetic here is the
// recompiled code's, operation for operation: a multiply and a separate add
// (never fused), the same operand order, the ramp's scalars with denormals
// kept and its vector work with them flushed, so the result is the same
// bits.
#pragma once
#include <cstdint>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace apu
{
    constexpr uint32_t kVoiceMixFrames = 256;
    constexpr uint32_t kVoiceMixChannelBytes = kVoiceMixFrames * 4;
    constexpr uint32_t kVoiceMixEntryBytes = 12;

    // The function's float constants, read from the game's image: the
    // ramp's step per sample is (target - current) * rampScale, per vector
    // that times stepScale, and lane i of the first vector starts lane[i]
    // steps in (1/256, 4 and 0..3 in practice).
    struct VoiceMixConstants
    {
        float rampScale;
        float stepScale;
        float lane[4];
    };

    // One call. entries: entryCount matrix entries as the guest keeps them,
    // 12 bytes each: source channel (u8), destination channel (u8), two
    // bytes unused, current volume and target volume (big-endian floats);
    // the current volume becomes the target after its ramp. mode is the
    // send's byte 26: 0 adds into the destination, anything else writes it.
    struct VoiceMixCall
    {
        uint8_t* entries;
        uint32_t entryCount;
        const uint8_t* src;
        uint32_t srcChannels;
        uint8_t* dst;
        uint32_t dstChannels;
        uint8_t mode;
    };

    namespace voice_mix_detail
    {
        inline float LoadBE(const uint8_t* p)
        {
            uint32_t u;
            memcpy(&u, p, 4);
            u = __builtin_bswap32(u);
            float f;
            memcpy(&f, &u, 4);
            return f;
        }

        inline void StoreBE(uint8_t* p, float f)
        {
            uint32_t u;
            memcpy(&u, &f, 4);
            u = __builtin_bswap32(u);
            memcpy(p, &u, 4);
        }

        // Flush mode is a register write the compiler doesn't order float
        // math against: at -O2 it moved the ramp's first volumes across it.
        // Fence keeps memory accesses on their side of a switch, Pin the
        // values in registers (computed before, used after).
        inline void Fence() { __asm__ __volatile__("" ::: "memory"); }

        inline void Pin(float& v)
        {
#if defined(__aarch64__)
            __asm__ __volatile__("" : "+w"(v) : : "memory");
#elif defined(__x86_64__)
            __asm__ __volatile__("" : "+x"(v) : : "memory");
#else
            __asm__ __volatile__("" : "+m"(v) : : "memory");
#endif
        }

#if defined(__aarch64__)
        inline float32x4_t Load4(const uint8_t* p) { return vreinterpretq_f32_u8(vrev32q_u8(vld1q_u8(p))); }
        inline void Store4(uint8_t* p, float32x4_t v) { vst1q_u8(p, vrev32q_u8(vreinterpretq_u8_f32(v))); }
#endif

        // dst[i] = src[i] * vol (+ dst[i]), vol += step4 every 4 samples.
        inline void Ramp(uint8_t* dst, const uint8_t* src, const float vol0[4], float step4, bool overwrite)
        {
#if defined(__aarch64__)
            float32x4_t vol = vld1q_f32(vol0);
            const float32x4_t inc = vdupq_n_f32(step4);
            if (overwrite)
            {
                for (uint32_t i = 0; i < kVoiceMixChannelBytes; i += 16)
                {
                    Store4(dst + i, vmulq_f32(Load4(src + i), vol));
                    vol = vaddq_f32(vol, inc);
                }
            }
            else
            {
                for (uint32_t i = 0; i < kVoiceMixChannelBytes; i += 16)
                {
                    float32x4_t product = vmulq_f32(Load4(src + i), vol);
                    Store4(dst + i, vaddq_f32(product, Load4(dst + i)));
                    vol = vaddq_f32(vol, inc);
                }
            }
#else
            float vol[4] = { vol0[0], vol0[1], vol0[2], vol0[3] };
            for (uint32_t i = 0; i < kVoiceMixChannelBytes; i += 16)
            {
                for (uint32_t l = 0; l < 4; l++)
                {
                    float product = LoadBE(src + i + l * 4) * vol[l];
                    if (!overwrite)
                    {
                        float sum = product + LoadBE(dst + i + l * 4);
                        product = sum;
                    }
                    StoreBE(dst + i + l * 4, product);
                    float next = vol[l] + step4;
                    vol[l] = next;
                }
            }
#endif
        }

        // dst[i] = src[i] + dst[i].
        inline void Add(uint8_t* dst, const uint8_t* src)
        {
#if defined(__aarch64__)
            for (uint32_t i = 0; i < kVoiceMixChannelBytes; i += 16)
                Store4(dst + i, vaddq_f32(Load4(src + i), Load4(dst + i)));
#else
            for (uint32_t i = 0; i < kVoiceMixChannelBytes; i += 4)
                StoreBE(dst + i, LoadBE(src + i) + LoadBE(dst + i));
#endif
        }
    }

    // Fpscr is the guest's PPCFPSCRRegister, or anything with its four
    // flush-mode calls (the test's), switched where the recompiled code
    // switches them. The caller skips a send without entries from a voice
    // into itself, as the guest does.
    template <typename Fpscr>
    void MixVoice(const VoiceMixCall& c, const VoiceMixConstants& k, Fpscr& fpscr)
    {
        using namespace voice_mix_detail;
        if (c.entryCount == 0)
        {
            for (uint32_t ch = 0; ch < c.dstChannels; ch++)
            {
                uint8_t* dst = c.dst + ch * kVoiceMixChannelBytes;
                if (ch < c.srcChannels)
                {
                    const uint8_t* src = c.src + ch * kVoiceMixChannelBytes;
                    if (c.mode == 0)
                    {
                        fpscr.enableFlushMode();
                        Fence();
                        Add(dst, src);
                        Fence();
                    }
                    else
                        memmove(dst, src, kVoiceMixChannelBytes);
                }
                else if (c.mode != 0)
                    memset(dst, 0, kVoiceMixChannelBytes);
                else
                    break;
            }
            return;
        }
        for (uint32_t e = 0; e < c.entryCount; e++)
        {
            uint8_t* entry = c.entries + e * kVoiceMixEntryBytes;
            uint32_t srcChannel = entry[0], dstChannel = entry[1];
            fpscr.disableFlushMode();
            Fence();
            float target = LoadBE(entry + 8);
            if (dstChannel >= c.dstChannels)
                continue;
            uint8_t* dst = c.dst + dstChannel * kVoiceMixChannelBytes;
            if (srcChannel >= c.srcChannels)
            {
                if (c.mode != 0)
                    memset(dst, 0, kVoiceMixChannelBytes);
                continue;
            }
            float current = LoadBE(entry + 4);
            float diff = target - current;
            float step = diff * k.rampScale;
            float step4 = step * k.stepScale;
            Pin(current), Pin(step), Pin(step4);
            fpscr.enableFlushModeUnconditional();
            Pin(current), Pin(step), Pin(step4);
            float vol0[4];
            for (uint32_t l = 0; l < 4; l++)
            {
                float offset = step * k.lane[l];
                vol0[l] = offset + current;
            }
            fpscr.enableFlushMode();
            Fence();
            Ramp(dst, c.src + srcChannel * kVoiceMixChannelBytes, vol0, step4, c.mode != 0);
            Fence();
            fpscr.disableFlushModeUnconditional();
            StoreBE(entry + 4, target);
        }
    }
}
