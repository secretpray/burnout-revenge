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
        // Which of the bank's entries each sound is, and its channels.
        constexpr std::array<int, 4> kEntry = { 9, 14, 28, 42 };  // Move, Change, Select, Back
        constexpr std::array<int, 4> kChannels = { 1, 1, 1, 1 };

        std::array<std::vector<float>, 4> s_pcm;  // 48 kHz stereo
        bool s_loaded = false, s_tried = false;
        SDL_AudioStream* s_stream = nullptr;

        uint32_t Be32(const uint8_t* p)
        {
            return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
        }

        uint32_t Bit(const uint8_t* data, size_t pos) { return (data[pos >> 3] >> (7 - (pos & 7))) & 1u; }

        void PutBit(uint8_t* data, size_t pos, uint32_t v)
        {
            const uint8_t mask = uint8_t(0x80 >> (pos & 7));
            data[pos >> 3] = uint8_t(v ? data[pos >> 3] | mask : data[pos >> 3] & ~mask);
        }

        // The XMA frames of one entry's packets (one stream, as XACT stores
        // it), as apu/xma.cpp walks them: the first frame at the packet
        // header's frame offset, each led by its 15-bit length in bits, a
        // frame running on into the next packet's payload, and the frame's
        // last bit saying whether another follows in this packet.
        std::vector<std::vector<uint8_t>> Frames(const uint8_t* data, uint32_t size, std::vector<uint32_t>& lengths)
        {
            constexpr uint32_t kPayload = (kPacket - 4) * 8;
            const uint32_t packets = size / kPacket;
            auto field = [&](uint32_t k) {
                const uint8_t* p = data + size_t(k) * kPacket;
                return (uint32_t(p[0] & 3) << 13) | (uint32_t(p[1]) << 5) | (p[2] >> 3);
            };
            auto at = [&](size_t pos) { return size_t(pos / kPayload) * kPacket * 8 + 32 + pos % kPayload; };  // payload bit -> data bit
            auto next = [&](uint32_t k) -> int64_t {
                while (k < packets && field(k) >= kPayload)
                    k++;
                return k < packets ? int64_t(k) * kPayload + field(k) : -1;
            };
            std::vector<std::vector<uint8_t>> frames;
            const size_t end = size_t(packets) * kPayload;
            for (int64_t pos = next(0); pos >= 0 && size_t(pos) + 15 <= end;)
            {
                uint32_t length = 0;
                for (int i = 0; i < 15; i++)
                    length = length << 1 | Bit(data, at(size_t(pos) + i));
                const uint32_t start = uint32_t(size_t(pos) / kPayload);
                if (length <= 15 || length == 0x7FFF)
                {
                    pos = next(start + 1);
                    continue;
                }
                if (size_t(pos) + length > end)
                    break;
                std::vector<uint8_t> bits((length + 7) / 8 + 1, 0);
                for (uint32_t i = 0; i < length; i++)
                    PutBit(bits.data(), i, Bit(data, at(size_t(pos) + i)));
                const bool another = Bit(bits.data(), length - 1);
                frames.push_back(std::move(bits));
                lengths.push_back(length);
                if (size_t(pos) % kPayload + length < kPayload && another)
                    pos += length;
                else
                    pos = next(start + 1);
            }
            return frames;
        }

        // One entry's XMA as 48 kHz stereo float: each frame in a packet of
        // its own for FFmpeg's XMA1 decoder (as apu/xma.cpp does), `samples`
        // of the result kept.
        std::vector<float> Decode(const uint8_t* data, uint32_t size, int rate, int channels, uint32_t samples)
        {
            std::vector<float> out;
            std::vector<uint32_t> lengths;
            const std::vector<std::vector<uint8_t>> frames = Frames(data, size, lengths);
            const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_XMA1);
            AVCodecContext* ctx = codec && !frames.empty() ? avcodec_alloc_context3(codec) : nullptr;
            if (!ctx)
                return out;
            ctx->sample_rate = rate;
            av_channel_layout_default(&ctx->ch_layout, channels);
            ctx->block_align = kPacket;
            ctx->flags2 |= AV_CODEC_FLAG2_SKIP_MANUAL;
            // XMAWAVEFORMAT: one stream (as apu/xma.cpp).
            ctx->extradata = static_cast<uint8_t*>(av_mallocz(28 + AV_INPUT_BUFFER_PADDING_SIZE));
            ctx->extradata_size = 28;
            ctx->extradata[4] = 1;
            ctx->extradata[8 + 17] = uint8_t(channels);
            AVPacket* pkt = av_packet_alloc();
            AVFrame* frame = av_frame_alloc();
            SwrContext* swr = nullptr;
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            static uint8_t packet[kPacket + AV_INPUT_BUFFER_PADDING_SIZE];
            if (avcodec_open2(ctx, codec, nullptr) == 0 && pkt && frame &&
                swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, kRate, &ctx->ch_layout, ctx->sample_fmt, rate, 0, nullptr) == 0 &&
                swr_init(swr) == 0)
            {
                uint32_t left = samples;
                auto convert = [&](const uint8_t** in, int count) {
                    int room = swr_get_out_samples(swr, count);
                    if (room <= 0)
                        return;
                    size_t at = out.size();
                    out.resize(at + size_t(room) * 2);
                    uint8_t* dst = reinterpret_cast<uint8_t*>(out.data() + at);
                    int made = swr_convert(swr, &dst, room, in, count);
                    out.resize(at + size_t(std::max(made, 0)) * 2);
                };
                constexpr uint32_t kPayload = (kPacket - 4) * 8;
                for (size_t f = 0; f < frames.size(); f++)
                {
                    const uint32_t length = std::min(lengths[f], kPayload);  // (a click's frames fit one payload)
                    std::memset(packet, 0, kPacket);
                    for (uint32_t i = 0; i < length; i++)
                        PutBit(packet, 32 + i, Bit(frames[f].data(), i));
                    PutBit(packet, 32 + length - 1, 0);  // no frame follows in this packet
                    av_packet_unref(pkt);
                    pkt->data = packet;
                    pkt->size = kPacket;
                    if (avcodec_send_packet(ctx, pkt) != 0)
                        continue;
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

        // BURNOUT_MENU_SOUNDS_DUMP=<folder>: every entry of the bank as a
        // 48 kHz stereo float WAV there (fe_NN_<channels>.wav), mono and
        // stereo readings both, to find which the game's menus play.
        void DumpBank(const std::vector<uint8_t>& d, uint32_t count, uint32_t size, uint32_t meta, uint32_t wave, const char* folder)
        {
            for (uint32_t i = 0; i < count; i++)
            {
                const size_t e = size_t(meta) + size_t(i) * size;
                if (e + 16 > d.size())
                    break;
                const uint32_t samples = Be32(&d[e]) >> 4, format = Be32(&d[e + 4]);
                const uint32_t offset = Be32(&d[e + 8]), length = Be32(&d[e + 12]);
                if (size_t(wave) + offset + length > d.size())
                    continue;
                for (int channels : { 1, 2 })
                {
                    std::vector<float> pcm = Decode(&d[size_t(wave) + offset], length, int((format >> 5) & 0x3FFFF), channels, samples);
                    std::string name = std::format("{}/fe_{:02}_{}.wav", folder, i, channels);
                    FILE* f = fopen(name.c_str(), "wb");
                    if (!f)
                        continue;
                    const uint32_t bytes = uint32_t(pcm.size() * sizeof(float));
                    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
                    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
                    fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f); u32(16);
                    u16(3); u16(2); u32(kRate); u32(kRate * 8); u16(8); u16(32);
                    fwrite("data", 1, 4, f); u32(bytes);
                    fwrite(pcm.data(), 1, bytes, f);
                    fclose(f);
                }
            }
            fprintf(stderr, "[menu-sounds] BURNOUT_MENU_SOUNDS_DUMP: %u entries written to %s\n", count, folder);
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
            if (const char* folder = std::getenv("BURNOUT_MENU_SOUNDS_DUMP"); folder && *folder)
                DumpBank(d, count, size, meta, wave, folder);
            for (size_t s = 0; s < kEntry.size(); s++)
            {
                const uint32_t i = uint32_t(kEntry[s]);
                const size_t e = size_t(meta) + size_t(i) * size;
                if (i >= count || size < 16 || e + 16 > d.size())
                    return false;
                const uint32_t samples = Be32(&d[e]) >> 4, format = Be32(&d[e + 4]);
                const uint32_t offset = Be32(&d[e + 8]), length = Be32(&d[e + 12]);
                const int rate = int((format >> 5) & 0x3FFFF);
                if (size_t(wave) + offset + length > d.size() || length > (1u << 20) || rate < 8000 || rate > 48000)
                {
                    fprintf(stderr, "[menu-sounds] entry %u can't be a click (%u bytes, %d Hz)\n", i, length, rate);
                    return false;
                }
                s_pcm[s] = Decode(&d[size_t(wave) + offset], length, rate, kChannels[s], samples);
                float peak = 0;
                for (float v : s_pcm[s])
                    peak = std::max(peak, std::abs(v));
                // Silence or garbage (a misread stream reads as huge values):
                // never played, the menus stay quiet.
                if (s_pcm[s].empty() || peak < 1e-4f || peak > 1.5f)
                {
                    fprintf(stderr, "[menu-sounds] entry %u didn't decode to a usable sound (peak %.3g): no menu sounds\n", i, double(peak));
                    s_pcm = {};
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
        static const bool enabled = [] { const char* v = std::getenv("BURNOUT_MENU_SOUNDS"); return v && *v == '1'; }();
        if (s_loaded || !enabled)
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
        // Experimental until the clicks the game's own menus play are
        // identified: their bank entries didn't decode to usable sounds
        // (docs/ACTIVE_WORK.md). BURNOUT_MENU_SOUNDS=1 tries them.
        static const bool enabled = [] { const char* v = std::getenv("BURNOUT_MENU_SOUNDS"); return v && *v == '1'; }();
        if (!enabled)
            return;
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
