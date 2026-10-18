// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See menu_sounds.h.
//
// sound/fe.xwb: an XACT 2 wave bank (version 34), big-endian. Its header is
// "DNBW", the version, then five segments (offset, length): bank data, entry
// metadata, entry names, seek tables, wave data. The bank data holds its
// flags (0x10000: entry names, 64 bytes each), the entry count (at 4) and
// the size of one entry (at 72). An entry starts with its flags and duration
// (samples << 4), then its format: bit 0 XMA, bits 1-3 the channels, bits
// 4-21 the sample rate (44100, 48000, 32768 Hz in this bank); then where its
// data lies in the wave data. The data is XMA in 2048-byte packets as the
// 360 plays them, decoded here packet by packet with FFmpeg's XMA2 decoder.
// Read this way every entry decodes to its named sound, at its length
// (garagamb_L11, the garage music, 111 s). The clicks are named: keymove,
// adjust_l and _r (one sound's left and right), keyselect, keyback.
#include <stdafx.h>
#include "menu_sounds.h"
#include <install/disc_source.h>
#include <user/paths.h>
#include <user/settings.h>

#include <SDL3/SDL.h>

#include <array>
#include <cstring>
#include <format>
#include <fstream>
#include <string>
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
        // Each sound's entries, by name: one plays on both sides, two are
        // its left and right.
        constexpr std::array<std::array<const char*, 2>, 4> kNames = { {
            { "keymove", nullptr },        // Move
            { "adjust_l", "adjust_r" },    // Change
            { "keyselect", nullptr },      // Select
            { "keyback", nullptr },        // Back
        } };

        std::array<std::vector<float>, 4> s_pcm;  // 48 kHz stereo
        bool s_loaded = false, s_tried = false;
        SDL_AudioStream* s_stream = nullptr;

        uint32_t Be32(const uint8_t* p)
        {
            return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
        }

        // One entry's XMA as 48 kHz stereo float, `samples` of it kept: its
        // packets, whole, through FFmpeg's XMA2 decoder (one stream per two
        // channels, as an XMA2WAVEFORMATEX says).
        std::vector<float> Decode(const uint8_t* data, uint32_t size, int rate, int channels, uint32_t samples)
        {
            std::vector<float> out;
            const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_XMA2);
            AVCodecContext* ctx = codec && size >= kPacket ? avcodec_alloc_context3(codec) : nullptr;
            if (!ctx)
                return out;
            ctx->sample_rate = rate;
            av_channel_layout_default(&ctx->ch_layout, channels);
            ctx->block_align = kPacket;
            ctx->extradata = static_cast<uint8_t*>(av_mallocz(34 + AV_INPUT_BUFFER_PADDING_SIZE));
            ctx->extradata_size = 34;
            ctx->extradata[0] = uint8_t((channels + 1) / 2);  // NumStreams
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
                auto receive = [&] {
                    while (avcodec_receive_frame(ctx, frame) == 0)
                    {
                        int count = int(std::min<uint32_t>(left, uint32_t(frame->nb_samples)));
                        left -= uint32_t(count);
                        if (count > 0)
                            convert(const_cast<const uint8_t**>(frame->extended_data), count);
                        av_frame_unref(frame);
                    }
                };
                for (uint32_t at = 0; at + kPacket <= size && left > 0; at += kPacket)
                {
                    std::memcpy(packet, data + at, kPacket);
                    av_packet_unref(pkt);
                    pkt->data = packet;
                    pkt->size = kPacket;
                    if (avcodec_send_packet(ctx, pkt) == 0)
                        receive();
                }
                avcodec_send_packet(ctx, nullptr);
                receive();
                convert(nullptr, 0);
            }
            swr_free(&swr);
            av_frame_free(&frame);
            av_packet_free(&pkt);
            avcodec_free_context(&ctx);
            return out;
        }

        struct Entry
        {
            std::string name;
            uint32_t samples = 0, offset = 0, length = 0;
            int rate = 0, channels = 0;
            bool xma = false;
        };

        // The bank's entries, or none if it isn't the expected bank.
        std::vector<Entry> Entries(const std::vector<uint8_t>& d, const char* from, uint32_t& wave)
        {
            std::vector<Entry> entries;
            if (d.size() < 64 || std::memcmp(d.data(), "DNBW", 4) != 0 || Be32(&d[4]) != 34)
            {
                fprintf(stderr, "[menu-sounds] %s isn't the expected wave bank (XACT 2, version 34)\n", from);
                return entries;
            }
            const uint32_t bank = Be32(&d[8]), meta = Be32(&d[16]), names = Be32(&d[24]);
            wave = Be32(&d[40]);
            if (size_t(bank) + 76 > d.size())
                return entries;
            const uint32_t flags = Be32(&d[bank]), count = Be32(&d[bank + 4]), size = Be32(&d[bank + 72]);
            if (!(flags & 0x10000) || size < 16 || count > 4096 || size_t(meta) + size_t(count) * size > d.size() ||
                size_t(names) + size_t(count) * 64 > d.size())
            {
                fprintf(stderr, "[menu-sounds] %s: no entry names, or entries past its end\n", from);
                return entries;
            }
            for (uint32_t i = 0; i < count; i++)
            {
                const uint8_t* e = &d[size_t(meta) + size_t(i) * size];
                const uint32_t format = Be32(e + 4);
                const char* name = reinterpret_cast<const char*>(&d[size_t(names) + size_t(i) * 64]);
                entries.push_back({ .name = std::string(name, strnlen(name, 64)),
                    .samples = Be32(e) >> 4,
                    .offset = Be32(e + 8),
                    .length = Be32(e + 12),
                    .rate = int((format >> 4) & 0x3FFFF),
                    .channels = int((format >> 1) & 7),
                    .xma = (format & 1) != 0 });
            }
            return entries;
        }

        std::vector<float> DecodeEntry(const std::vector<uint8_t>& d, uint32_t wave, const Entry& e)
        {
            if (!e.xma || e.channels < 1 || e.channels > 2 || e.rate < 8000 || e.rate > 48000 ||
                size_t(wave) + e.offset + e.length > d.size() || e.length > (8u << 20))
                return {};
            return Decode(&d[size_t(wave) + e.offset], e.length, e.rate, e.channels, e.samples);
        }

        // BURNOUT_MENU_SOUNDS_DUMP=<folder>: every entry of the bank as a
        // 48 kHz stereo float WAV there (NN_<name>.wav), to hear them.
        void DumpBank(const std::vector<uint8_t>& d, uint32_t wave, const std::vector<Entry>& entries, const char* folder)
        {
            for (size_t i = 0; i < entries.size(); i++)
            {
                std::vector<float> pcm = DecodeEntry(d, wave, entries[i]);
                std::string name = std::format("{}/{:02}_{}.wav", folder, i, entries[i].name);
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
            fprintf(stderr, "[menu-sounds] BURNOUT_MENU_SOUNDS_DUMP: %zu entries written to %s\n", entries.size(), folder);
        }

        // The bank, whole (5 MB): its clicks into s_pcm.
        bool Load(const std::vector<uint8_t>& d, const char* from)
        {
            uint32_t wave = 0;
            const std::vector<Entry> entries = Entries(d, from, wave);
            if (entries.empty())
                return false;
            if (const char* folder = std::getenv("BURNOUT_MENU_SOUNDS_DUMP"); folder && *folder)
                DumpBank(d, wave, entries, folder);
            auto find = [&](const char* name) -> const Entry* {
                for (const Entry& e : entries)
                    if (e.name == name)
                        return &e;
                return nullptr;
            };
            std::array<std::vector<float>, 4> pcm;
            for (size_t s = 0; s < kNames.size(); s++)
            {
                const Entry* left = find(kNames[s][0]);
                const Entry* right = kNames[s][1] ? find(kNames[s][1]) : nullptr;
                if (!left || (kNames[s][1] && !right))
                {
                    fprintf(stderr, "[menu-sounds] %s has no \"%s\": no menu sounds\n", from, kNames[s][right || !kNames[s][1] ? 0 : 1]);
                    return false;
                }
                pcm[s] = DecodeEntry(d, wave, *left);
                if (right)
                {
                    // The left entry's left channel, the right entry's right.
                    const std::vector<float> other = DecodeEntry(d, wave, *right);
                    pcm[s].resize(std::max(pcm[s].size(), other.size()));
                    for (size_t i = 1; i < pcm[s].size(); i += 2)
                        pcm[s][i] = i < other.size() ? other[i] : 0.0f;
                }
                float peak = 0;
                for (float v : pcm[s])
                    peak = std::max(peak, std::abs(v));
                // Silence or garbage (a misread stream reads as huge values):
                // never played, the menus stay quiet.
                if (pcm[s].empty() || peak < 1e-4f || peak > 1.5f)
                {
                    fprintf(stderr, "[menu-sounds] \"%s\" didn't decode to a usable sound (peak %.3g): no menu sounds\n",
                        kNames[s][0], double(peak));
                    return false;
                }
            }
            s_pcm = std::move(pcm);
            fprintf(stderr, "[menu-sounds] the game's menu clicks from %s (keymove, adjust, keyselect, keyback: %.0f, %.0f, %.0f, %.0f ms)\n",
                from, s_pcm[0].size() / 2 * 1000.0 / kRate, s_pcm[1].size() / 2 * 1000.0 / kRate, s_pcm[2].size() / 2 * 1000.0 / kRate,
                s_pcm[3].size() / 2 * 1000.0 / kRate);
            return s_loaded = true;
        }

        // BURNOUT_MENU_SOUNDS=0: our menus silent (an A/B switch).
        bool Enabled()
        {
            static const bool enabled = [] { const char* v = std::getenv("BURNOUT_MENU_SOUNDS"); return !(v && *v == '0'); }();
            return enabled;
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
        if (s_loaded || !Enabled())
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
        if (!Enabled())
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
