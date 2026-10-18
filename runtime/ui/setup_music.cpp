// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See setup_music.h.
//
// The decoder thread opens each song through install::FileReader (the disc
// image is never extracted for this), demuxes ASF and decodes WMA v2 with the
// FFmpeg already linked for the game's EA Trax (apu/music.cpp), resamples to
// 48 kHz stereo float and keeps ~0.75 s queued in the stream. The main
// thread only sets the stream's gain.
#include <stdafx.h>
#include "setup_music.h"
#include <install/disc_source.h>
#include <user/settings.h>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <random>
#include <thread>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace ui::setup_music
{
    namespace
    {
        constexpr int kRate = 48000;
        constexpr int kQueueBytes = kRate * 2 * int(sizeof(float)) * 3 / 4;  // 0.75 s
        constexpr float kLevel = 0.7f;  // under the game's own mix: it's a backdrop

        struct Song
        {
            std::string name;  // disc path, eatrax/07.wma
            uint64_t size = 0;
            std::unique_ptr<install::FileReader> reader;
        };

        // AVIO over a FileReader (exact reads, so the tail is clamped here).
        struct Input
        {
            install::FileReader* reader;
            uint64_t size;
            uint64_t position = 0;

            static int Read(void* opaque, uint8_t* buffer, int size)
            {
                auto* in = static_cast<Input*>(opaque);
                uint64_t n = std::min<uint64_t>(uint64_t(size), in->size - std::min(in->position, in->size));
                if (n == 0)
                    return AVERROR_EOF;
                std::string error;
                if (!in->reader->Read(in->position, buffer, size_t(n), error))
                    return AVERROR(EIO);
                in->position += n;
                return int(n);
            }

            static int64_t Seek(void* opaque, int64_t offset, int whence)
            {
                auto* in = static_cast<Input*>(opaque);
                if (whence & AVSEEK_SIZE)
                    return int64_t(in->size);
                whence &= ~AVSEEK_FORCE;
                int64_t base = whence == SEEK_CUR ? int64_t(in->position) : whence == SEEK_END ? int64_t(in->size) : 0;
                if (base + offset < 0)
                    return -1;
                in->position = uint64_t(base + offset);
                return int64_t(in->position);
            }
        };

        SDL_AudioStream* s_stream = nullptr;
        std::thread s_thread;
        std::atomic<bool> s_stop{ false };
        std::atomic<int> s_track{ 0 };  // the song playing: its NN + 1 (00.wma is track 1), 0 for none
        bool s_enabled = true;
        float s_fade = 0;          // 0..1, the main thread's
        float s_fadeTarget = 1;
        float s_fadeSeconds = 2;   // to the target from the other end

        // Decodes one song into the stream. False when it couldn't (logged).
        bool PlaySong(Song& song)
        {
            Input in{ song.reader.get(), song.size };
            constexpr int kBufferSize = 32768;
            auto* buffer = static_cast<uint8_t*>(av_malloc(kBufferSize));
            AVIOContext* io = buffer ? avio_alloc_context(buffer, kBufferSize, 0, &in, Input::Read, nullptr, Input::Seek) : nullptr;
            if (!io)
            {
                av_free(buffer);
                return false;
            }
            AVFormatContext* format = avformat_alloc_context();
            AVCodecContext* codec = nullptr;
            SwrContext* swr = nullptr;
            AVPacket* packet = av_packet_alloc();
            AVFrame* frame = av_frame_alloc();
            int stream = -1;
            bool ok = false;
            std::vector<float> out;
            auto fail = [&](const char* why) {
                fprintf(stderr, "[setup-music] %s %s\n", song.name.c_str(), why);
                return false;
            };
            if (format)
            {
                format->pb = io;
                format->flags |= AVFMT_FLAG_CUSTOM_IO;
            }
            if (!format || !packet || !frame)
                fail("no memory");
            else if (avformat_open_input(&format, nullptr, av_find_input_format("asf"), nullptr) < 0)
                fail("isn't ASF");  // (it frees format)
            else
            {
                for (unsigned i = 0; i < format->nb_streams && stream < 0; i++)
                    if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                        stream = int(i);
                const AVCodecParameters* par = stream >= 0 ? format->streams[stream]->codecpar : nullptr;
                const AVCodec* decoder = par ? avcodec_find_decoder(par->codec_id) : nullptr;
                AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
                if (!decoder)
                    fail("has no audio this build decodes");
                else if (!(codec = avcodec_alloc_context3(decoder)) || avcodec_parameters_to_context(codec, par) < 0 ||
                         avcodec_open2(codec, decoder, nullptr) < 0)
                    fail("can't be decoded");
                else if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, kRate, &codec->ch_layout, codec->sample_fmt,
                             codec->sample_rate, 0, nullptr) < 0 || swr_init(swr) < 0)
                    fail("can't be resampled");
                else
                {
                    fprintf(stderr, "[setup-music] playing %s from the disc (%s, %d Hz, %d ch)\n", song.name.c_str(),
                        decoder->name, codec->sample_rate, codec->ch_layout.nb_channels);
                    ok = true;
                }
            }
            auto put = [&](const uint8_t** data, int samples) {
                int room = swr_get_out_samples(swr, samples);
                if (room <= 0)
                    return;
                out.resize(size_t(room) * 2);
                uint8_t* dst = reinterpret_cast<uint8_t*>(out.data());
                int made = swr_convert(swr, &dst, room, data, samples);
                if (made > 0)
                    SDL_PutAudioStreamData(s_stream, out.data(), made * 2 * int(sizeof(float)));
            };
            bool flushing = false;
            int errors = 0;
            while (ok && !s_stop)
            {
                if (SDL_GetAudioStreamQueued(s_stream) > kQueueBytes)
                {
                    SDL_Delay(20);
                    continue;
                }
                int r = avcodec_receive_frame(codec, frame);
                if (r == 0)
                {
                    put(const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
                    av_frame_unref(frame);
                    continue;
                }
                if (r == AVERROR_EOF)
                {
                    put(nullptr, 0);  // what the resampler holds back
                    break;
                }
                if (r != AVERROR(EAGAIN))
                {
                    if (++errors > 64)
                        break;
                    continue;
                }
                if (flushing)
                    continue;
                if (av_read_frame(format, packet) < 0)
                {
                    avcodec_send_packet(codec, nullptr);
                    flushing = true;
                    continue;
                }
                if (packet->stream_index == stream && avcodec_send_packet(codec, packet) < 0)
                    errors++;
                av_packet_unref(packet);
            }
            av_packet_free(&packet);
            av_frame_free(&frame);
            swr_free(&swr);
            avcodec_free_context(&codec);
            avformat_close_input(&format);
            av_freep(&io->buffer);
            avio_context_free(&io);
            return ok;
        }

        void Decode(std::vector<Song> songs)
        {
            for (size_t i = 0, failures = 0; !s_stop && !songs.empty() && failures < songs.size(); i = (i + 1) % songs.size())
            {
                s_track = std::atoi(songs[i].name.c_str() + songs[i].name.rfind('/') + 1) + 1;
                failures = PlaySong(songs[i]) ? 0 : failures + 1;
            }
            s_track = 0;
        }

        float SettingsGain()
        {
            if (settings::GetBool(settings::Id::Mute))
                return 0;
            float volume = float(settings::GetInt(settings::Id::MasterVolume)) / 100.0f;
            return volume * volume;  // as apu/audio.cpp: closer to perceived loudness
        }
    }

    void Start(const install::DiscSource& source)
    {
        Stop();
        std::vector<Song> songs;
        for (const install::DiscFile& file : source.Files())
        {
            std::string lower = file.path;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (!lower.starts_with("eatrax/") || !lower.ends_with(".wma"))
                continue;
            std::string error;
            if (auto reader = source.Open(file, error))
                songs.push_back({ file.path, file.size, std::move(reader) });
            else
                fprintf(stderr, "[setup-music] %s can't be opened: %s\n", file.path.c_str(), error.c_str());
        }
        if (songs.empty())
        {
            fprintf(stderr, "[setup-music] the disc has no EA Trax songs: no music\n");
            return;
        }
        if (!SDL_WasInit(SDL_INIT_AUDIO))
        {
            fprintf(stderr, "[setup-music] audio is off: no music\n");
            return;
        }
        const SDL_AudioSpec spec{ SDL_AUDIO_F32, 2, kRate };
        if (!(s_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr)))
        {
            fprintf(stderr, "[setup-music] no output: %s\n", SDL_GetError());
            return;
        }
        // From a random song, then in disc order, as a radio would.
        std::rotate(songs.begin(), songs.begin() + std::random_device{}() % songs.size(), songs.end());
        s_fade = 0;
        s_fadeTarget = 1;
        s_fadeSeconds = 2;
        SDL_SetAudioStreamGain(s_stream, 0);
        SDL_ResumeAudioStreamDevice(s_stream);
        s_stop = false;
        fprintf(stderr, "[setup-music] %zu songs on the disc; starting at %s\n", songs.size(), songs.front().name.c_str());
        s_thread = std::thread(Decode, std::move(songs));
    }

    void Update()
    {
        if (!s_stream)
            return;
        float target = s_enabled ? s_fadeTarget : 0;
        float step = ImGui::GetIO().DeltaTime / std::max(s_fadeSeconds, 0.01f);
        s_fade = s_fade < target ? std::min(target, s_fade + step) : std::max(target, s_fade - step);
        SDL_SetAudioStreamGain(s_stream, s_fade * kLevel * SettingsGain());
        if (s_fadeTarget == 0 && s_fade == 0)
            Stop();
    }

    void FadeOut(float seconds)
    {
        if (!s_stream)
            return;
        s_fadeTarget = 0;
        s_fadeSeconds = seconds;
        fprintf(stderr, "[setup-music] fading out over %.1f s\n", seconds);
    }

    void Stop()
    {
        if (!s_stream)
            return;
        s_stop = true;
        if (s_thread.joinable())
            s_thread.join();
        SDL_DestroyAudioStream(s_stream);
        s_stream = nullptr;
        s_track = 0;
        fprintf(stderr, "[setup-music] stopped\n");
    }

    bool Playing()
    {
        return s_stream != nullptr;
    }

    void SetEnabled(bool enabled)
    {
        if (enabled == s_enabled)
            return;
        s_enabled = enabled;
        s_fadeSeconds = .4f;
        fprintf(stderr, "[setup-music] player turned the music %s\n", enabled ? "on" : "off");
    }

    bool Enabled()
    {
        return s_enabled;
    }

    std::string NowPlaying()
    {
        int track = s_track;
        return s_stream && track > 0 ? std::format("EA TRAX  /  TRACK {:02}", track) : std::string();
    }
}
