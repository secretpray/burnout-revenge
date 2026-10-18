// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See menu_sounds.h.
//
// sound/fe.xwb: an XACT 2 wave bank (version 34), big-endian. Its header is
// "DNBW", the version, then five segments (offset, length): bank data, entry
// metadata, seek tables, names, wave data. The bank data holds the entry
// count (at 4) and the size of one entry (at 72); an entry starts with its
// flags and duration (samples << 4), its format (sample rate in bits 5-22),
// and where its data lies in the wave data. The front end's clicks are one
// 2048-byte XMA packet each, mono, decoded here as the game decodes its XMA
// (apu/xma.cpp: XMA1).
#include <stdafx.h>
#include "menu_sounds.h"
#include <install/disc_source.h>
#include <user/paths.h>
#include <user/settings.h>

#include <SDL3/SDL.h>

#include <array>
#include <fstream>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace ui::menu_sounds
{
    namespace
    {
        constexpr int kRate = 48000;
        constexpr float kLevel = 0.6f;   // under the game's own mix
        constexpr uint32_t kPacket = 2048;
        // Which of the bank's entries each sound is. All are one-packet mono
        // clicks (entries 5, 9, 10, 14, 19, 28, 29, 38, 42 and 43 are).
        constexpr std::array<int, 4> kEntry = { 9, 14, 28, 42 };  // Move, Change, Select, Back

        std::array<std::vector<float>, 4> s_pcm;  // 48 kHz stereo
        bool s_loaded = false, s_tried = false;
        SDL_AudioStream* s_stream = nullptr;

        uint32_t Be32(const uint8_t* p)
        {
            return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
        }

        // One XMA1 packet, mono, at `rate`: `samples` of it, as 48 kHz stereo.
        std::vector<float> Decode(const uint8_t* packet, int rate, uint32_t samples)
        {
            std::vector<float> out;
            const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_XMA1);
            AVCodecContext* ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
            if (!ctx)
                return out;
            ctx->sample_rate = rate;
            av_channel_layout_default(&ctx->ch_layout, 1);
            ctx->block_align = kPacket;
            // XMAWAVEFORMAT: one stream, one channel (as apu/xma.cpp).
            ctx->extradata = static_cast<uint8_t*>(av_mallocz(28 + AV_INPUT_BUFFER_PADDING_SIZE));
            ctx->extradata_size = 28;
            ctx->extradata[4] = 1;
            ctx->extradata[8 + 17] = 1;
            AVPacket* pkt = av_packet_alloc();
            AVFrame* frame = av_frame_alloc();
            SwrContext* swr = nullptr;
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            if (avcodec_open2(ctx, codec, nullptr) == 0 && pkt && frame && av_new_packet(pkt, kPacket) == 0 &&
                swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, kRate, &ctx->ch_layout, ctx->sample_fmt, rate, 0, nullptr) == 0 &&
                swr_init(swr) == 0)
            {
                std::memcpy(pkt->data, packet, kPacket);
                uint32_t left = samples;
                auto convert = [&](const uint8_t** data, int count) {
                    int room = swr_get_out_samples(swr, count);
                    if (room <= 0)
                        return;
                    size_t at = out.size();
                    out.resize(at + size_t(room) * 2);
                    uint8_t* dst = reinterpret_cast<uint8_t*>(out.data() + at);
                    int made = swr_convert(swr, &dst, room, data, count);
                    out.resize(at + size_t(std::max(made, 0)) * 2);
                };
                if (avcodec_send_packet(ctx, pkt) >= 0)
                {
                    avcodec_send_packet(ctx, nullptr);
                    while (avcodec_receive_frame(ctx, frame) == 0)
                    {
                        int count = int(std::min<uint32_t>(left, uint32_t(frame->nb_samples)));
                        left -= uint32_t(count);
                        if (count > 0)
                            convert(const_cast<const uint8_t**>(frame->extended_data), count);
                        av_frame_unref(frame);
                    }
                }
                convert(nullptr, 0);
            }
            swr_free(&swr);
            av_frame_free(&frame);
            av_packet_free(&pkt);
            avcodec_free_context(&ctx);
            return out;
        }

        // The bank, whole (5 MB): its four clicks into s_pcm.
        bool Load(const std::vector<uint8_t>& d, const char* from)
        {
            if (d.size() < 64 || std::memcmp(d.data(), "DNBW", 4) != 0 || Be32(&d[4]) != 34)
            {
                fprintf(stderr, "[menu-sounds] %s isn't the expected wave bank (XACT 2, version 34)\n", from);
                return false;
            }
            const uint32_t bank = Be32(&d[8]), meta = Be32(&d[16]), wave = Be32(&d[40]);
            if (bank + 76 > d.size())
                return false;
            const uint32_t count = Be32(&d[bank + 4]), size = Be32(&d[bank + 72]);
            for (size_t s = 0; s < kEntry.size(); s++)
            {
                const uint32_t i = uint32_t(kEntry[s]);
                const size_t e = size_t(meta) + size_t(i) * size;
                if (i >= count || size < 16 || e + 16 > d.size())
                    return false;
                const uint32_t samples = Be32(&d[e]) >> 4, format = Be32(&d[e + 4]);
                const uint32_t offset = Be32(&d[e + 8]), length = Be32(&d[e + 12]);
                const int rate = int((format >> 5) & 0x3FFFF);
                if (length != kPacket || size_t(wave) + offset + kPacket > d.size() || rate < 8000 || rate > 48000)
                {
                    fprintf(stderr, "[menu-sounds] entry %u isn't a one-packet click (%u bytes, %d Hz)\n", i, length, rate);
                    return false;
                }
                s_pcm[s] = Decode(&d[size_t(wave) + offset], rate, samples);
                if (s_pcm[s].empty())
                {
                    fprintf(stderr, "[menu-sounds] entry %u didn't decode\n", i);
                    return false;
                }
            }
            fprintf(stderr, "[menu-sounds] the game's menu clicks from %s (entries %d %d %d %d)\n", from, kEntry[0], kEntry[1],
                kEntry[2], kEntry[3]);
            return s_loaded = true;
        }

        float Gain()
        {
            if (settings::GetBool(settings::Id::Mute))
                return 0;
            float volume = float(settings::GetInt(settings::Id::MasterVolume)) / 100.0f;
            return volume * volume * kLevel;  // squared, as apu/audio.cpp
        }
    }

    void LoadFromGame(const std::filesystem::path& gameDir)
    {
        if (s_loaded)
            return;
        s_tried = true;
        std::filesystem::path file = gameDir / "sound" / "fe.xwb";
        std::ifstream in(file, std::ios::binary);
        if (!in)
            return;  // (not installed yet, or another layout: the menus stay silent)
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Load(d, "the installed game");
    }

    void LoadFromDisc(const install::DiscSource& source)
    {
        if (s_loaded)
            return;
        const install::DiscFile* file = source.Find("sound/fe.xwb");
        std::string error;
        std::unique_ptr<install::FileReader> reader = file ? source.Open(*file, error) : nullptr;
        if (!reader || file->size > (64u << 20))
            return;
        std::vector<uint8_t> d(size_t(file->size));
        if (reader->Read(0, d.data(), d.size(), error))
            Load(d, "the disc image");
    }

    void Play(Sound sound)
    {
        if (!s_loaded && !s_tried && !GetGamePath().empty())
            LoadFromGame(GetGamePath());
        const std::vector<float>& pcm = s_pcm[size_t(sound)];
        const float gain = Gain();
        if (!s_loaded || pcm.empty() || gain <= 0 || !SDL_WasInit(SDL_INIT_AUDIO))
            return;
        if (!s_stream)
        {
            const SDL_AudioSpec spec{ SDL_AUDIO_F32, 2, kRate };
            if (!(s_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr)))
            {
                fprintf(stderr, "[menu-sounds] no output: %s\n", SDL_GetError());
                s_loaded = false;  // no more tries
                return;
            }
            SDL_ResumeAudioStreamDevice(s_stream);
        }
        // The newest click wins: a run of D-pad presses doesn't queue up.
        SDL_ClearAudioStream(s_stream);
        SDL_SetAudioStreamGain(s_stream, gain);
        SDL_PutAudioStreamData(s_stream, pcm.data(), int(pcm.size() * sizeof(float)));
    }

    void Shutdown()
    {
        if (s_stream)
            SDL_DestroyAudioStream(s_stream);
        s_stream = nullptr;
    }
}
