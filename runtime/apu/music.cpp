// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The system music player's output (see music.h). The decoder thread reads
// the file itself (FFmpeg's libavformat is built with the ASF demuxer and no
// protocols, scripts/build_ffmpeg_xma.sh), decodes WMA v2 and resamples to
// the output's 48 kHz stereo float. The ring between it and the audio
// thread is under one mutex: the audio thread takes 256 samples from it per
// 5.33 ms frame, the decoder refills it below 1 s.
//
// Each Play is a song with its own number. The decoder drops what it
// decoded for an older one; the end handler gets the number back, so a song
// change that came in meanwhile (XMPNext) isn't followed by a second one.
#include <stdafx.h>
#include "music.h"
#include "audio.h"
#include <cpu/guest_thread.h>
#ifdef __APPLE__
#include <pthread.h>
#endif

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace
{
    constexpr uint32_t kRate = apu::kSampleRate;  // 48 kHz
    constexpr uint32_t kRingFrames = kRate * 3 / 2;   // 1.5 s of stereo
    constexpr uint32_t kRefillBelow = kRate;          // refill below 1 s

    // One song's decoding: ASF/WMA from a file to 48 kHz stereo float.
    class Decoder
    {
    public:
        ~Decoder()
        {
            av_packet_free(&m_packet);
            av_frame_free(&m_frame);
            swr_free(&m_swr);
            avcodec_free_context(&m_codec);
            avformat_close_input(&m_format);
            if (m_io)
            {
                av_freep(&m_io->buffer);
                avio_context_free(&m_io);
            }
            if (m_file)
                fclose(m_file);
        }

        bool Open(const std::filesystem::path& path)
        {
            if (!(m_file = fopen(path.string().c_str(), "rb")))
                return Fail("can't be opened");
            constexpr int kBufferSize = 32768;
            auto* buffer = static_cast<uint8_t*>(av_malloc(kBufferSize));
            if (!buffer || !(m_io = avio_alloc_context(buffer, kBufferSize, 0, m_file, Read, nullptr, Seek)))
            {
                av_free(buffer);
                return Fail("no memory");
            }
            if (!(m_format = avformat_alloc_context()))
                return Fail("no memory");
            m_format->pb = m_io;
            m_format->flags |= AVFMT_FLAG_CUSTOM_IO;
            if (avformat_open_input(&m_format, nullptr, av_find_input_format("asf"), nullptr) < 0)
                return Fail("isn't ASF");  // (it frees m_format)
            for (unsigned i = 0; i < m_format->nb_streams && m_stream < 0; i++)
                if (m_format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                    m_stream = int(i);
            if (m_stream < 0)
                return Fail("has no audio");
            const AVCodecParameters* par = m_format->streams[m_stream]->codecpar;
            const AVCodec* codec = avcodec_find_decoder(par->codec_id);
            if (!codec)
                return Fail(std::string("is ") + avcodec_get_name(par->codec_id) + ", which isn't decoded");
            if (!(m_codec = avcodec_alloc_context3(codec)) || avcodec_parameters_to_context(m_codec, par) < 0 ||
                avcodec_open2(m_codec, codec, nullptr) < 0)
                return Fail("can't be decoded");
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            if (swr_alloc_set_opts2(&m_swr, &stereo, AV_SAMPLE_FMT_FLT, kRate, &m_codec->ch_layout,
                    m_codec->sample_fmt, m_codec->sample_rate, 0, nullptr) < 0 || swr_init(m_swr) < 0)
                return Fail("can't be resampled");
            if (!(m_packet = av_packet_alloc()) || !(m_frame = av_frame_alloc()))
                return Fail("no memory");
            fprintf(stderr, "[music] %s: %s, %d Hz, %d ch\n", path.filename().string().c_str(), codec->name,
                m_codec->sample_rate, m_codec->ch_layout.nb_channels);
            return true;
        }

        // Appends the next decoded stretch to `out` (interleaved stereo).
        // False once the song is done (and everything it had appended).
        bool Decode(std::vector<float>& out)
        {
            while (!m_done)
            {
                int r = avcodec_receive_frame(m_codec, m_frame);
                if (r == 0)
                {
                    Convert(out, (const uint8_t**)m_frame->extended_data, m_frame->nb_samples);
                    av_frame_unref(m_frame);
                    return true;
                }
                if (r == AVERROR_EOF)
                {
                    Convert(out, nullptr, 0);  // what the resampler holds back
                    m_done = true;
                    break;
                }
                if (r != AVERROR(EAGAIN))
                {
                    // A bad block: skip it, as a player would.
                    if (++m_errors > 64)
                        m_done = true;
                    continue;
                }
                if (m_flushing)
                    continue;  // (the decoder ends with AVERROR_EOF)
                r = av_read_frame(m_format, m_packet);
                if (r < 0)
                {
                    avcodec_send_packet(m_codec, nullptr);
                    m_flushing = true;
                    continue;
                }
                if (m_packet->stream_index == m_stream && avcodec_send_packet(m_codec, m_packet) < 0)
                    m_errors++;
                av_packet_unref(m_packet);
            }
            return false;
        }

        uint64_t Frames() const { return m_frames; }
        const std::string& Error() const { return m_error; }

    private:
        bool Fail(const std::string& why)
        {
            m_error = why;
            return false;
        }

        void Convert(std::vector<float>& out, const uint8_t** in, int samples)
        {
            int room = swr_get_out_samples(m_swr, samples);
            if (room <= 0)
                return;
            size_t at = out.size();
            out.resize(at + size_t(room) * 2);
            uint8_t* dst = reinterpret_cast<uint8_t*>(out.data() + at);
            int got = swr_convert(m_swr, &dst, room, in, samples);
            out.resize(at + size_t(std::max(got, 0)) * 2);
            m_frames += uint64_t(std::max(got, 0));
        }

        static int Read(void* opaque, uint8_t* buffer, int size)
        {
            size_t n = fread(buffer, 1, size_t(size), static_cast<FILE*>(opaque));
            return n ? int(n) : AVERROR_EOF;
        }

        static int64_t Seek(void* opaque, int64_t offset, int whence)
        {
            FILE* file = static_cast<FILE*>(opaque);
            if (whence & AVSEEK_SIZE)
            {
                off_t at = ftello(file);
                fseeko(file, 0, SEEK_END);
                off_t size = ftello(file);
                fseeko(file, at, SEEK_SET);
                return size;
            }
            return fseeko(file, off_t(offset), whence & ~AVSEEK_FORCE) == 0 ? ftello(file) : -1;
        }

        FILE* m_file = nullptr;
        AVIOContext* m_io = nullptr;
        AVFormatContext* m_format = nullptr;
        AVCodecContext* m_codec = nullptr;
        SwrContext* m_swr = nullptr;
        AVPacket* m_packet = nullptr;
        AVFrame* m_frame = nullptr;
        int m_stream = -1;
        std::string m_error;
        bool m_flushing = false, m_done = false;
        uint32_t m_errors = 0;
        uint64_t m_frames = 0;
    };

    struct State
    {
        std::mutex mutex;
        std::condition_variable wake;  // the decoder: a song to open, or room in the ring
        std::filesystem::path path;
        uint64_t song = 0;             // the song playing (0: none)
        uint64_t nextSong = 0;
        bool open = false;             // the decoder is to open `path` for `song`
        bool decoded = false;          // `song` is decoded to its end (all of it in the ring)
        bool failed = false;           // ... without a sample
        bool reported = false;         // its end went to the handler
        bool paused = false;
        float volume = 1.0f;
        std::vector<float> ring = std::vector<float>(kRingFrames * 2);
        uint32_t head = 0, size = 0;   // frames
        bool threadStarted = false;
    } s;

    std::function<void(uint64_t, bool)> s_handler;
    float s_gain = 0.0f;  // the audio thread's: the gain the last frame ended at

    void DecoderThread()
    {
        SetHostThreadName("sb-music");
#ifdef __APPLE__
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#endif
        std::vector<float> chunk;
        for (;;)
        {
            std::filesystem::path path;
            uint64_t song;
            {
                std::unique_lock lock(s.mutex);
                s.wake.wait(lock, [] { return s.open; });
                s.open = false;
                path = s.path;
                song = s.song;
            }
            Decoder decoder;
            bool opened = decoder.Open(path);
            if (!opened)
                fprintf(stderr, "[music] %s %s: skipped\n", path.filename().string().c_str(), decoder.Error().c_str());
            bool more = opened;
            while (more)
            {
                chunk.clear();
                while (chunk.size() < 8192 && (more = decoder.Decode(chunk)))
                    ;
                // BR_MUSIC_SONG_SECONDS=<n> (testing): each song ends after n
                // seconds, so the song changes come within a short run.
                static const double songSeconds = [] { const char* v = std::getenv("BR_MUSIC_SONG_SECONDS"); return v ? std::atof(v) : 0.0; }();
                if (songSeconds > 0 && double(decoder.Frames()) >= songSeconds * kRate)
                    more = false;
                std::unique_lock lock(s.mutex);
                size_t at = 0, frames = chunk.size() / 2;
                while (at < frames && s.song == song && !s.open)
                {
                    s.wake.wait(lock, [&] { return s.song != song || s.open || s.size < kRefillBelow; });
                    if (s.song != song || s.open)
                        break;
                    uint32_t n = uint32_t(std::min<size_t>(frames - at, kRingFrames - s.size));
                    for (uint32_t i = 0; i < n; i++)
                    {
                        uint32_t slot = (s.head + s.size + i) % kRingFrames;
                        s.ring[slot * 2] = chunk[(at + i) * 2];
                        s.ring[slot * 2 + 1] = chunk[(at + i) * 2 + 1];
                    }
                    s.size += n;
                    at += n;
                }
                if (s.song != song || s.open)
                    break;  // another song (or none): this one is dropped
                if (!more)
                {
                    s.decoded = true;
                    s.failed = decoder.Frames() == 0;
                    fprintf(stderr, "[music] %s decoded: %.1f s\n", path.filename().string().c_str(), double(decoder.Frames()) / kRate);
                    if (s.failed)
                        fprintf(stderr, "[music] %s: no audio decoded\n", path.filename().string().c_str());
                }
            }
            if (!opened)
            {
                std::lock_guard lock(s.mutex);
                if (s.song == song && !s.open)
                    s.decoded = s.failed = true;
            }
        }
    }
}

namespace apu::music
{
    uint64_t Play(const std::filesystem::path& file)
    {
        std::lock_guard lock(s.mutex);
        if (!s.threadStarted)
        {
            s.threadStarted = true;
            std::thread(DecoderThread).detach();
        }
        s.path = file;
        s.song = ++s.nextSong;
        s.open = true;
        s.decoded = s.failed = s.reported = false;
        s.paused = false;
        s.head = s.size = 0;
        s.wake.notify_all();
        return s.song;
    }

    void Stop()
    {
        std::lock_guard lock(s.mutex);
        s.song = 0;
        s.open = false;
        s.decoded = s.failed = s.reported = false;
        s.head = s.size = 0;
        s.wake.notify_all();
    }

    void Pause()
    {
        std::lock_guard lock(s.mutex);
        s.paused = true;
    }

    void Resume()
    {
        std::lock_guard lock(s.mutex);
        s.paused = false;
    }

    void SetVolume(float volume)
    {
        std::lock_guard lock(s.mutex);
        s.volume = std::clamp(volume, 0.0f, 1.0f);
    }

    void SetSongEndHandler(std::function<void(uint64_t song, bool failed)> handler)
    {
        s_handler = std::move(handler);
    }

    void MixInto(float* out, uint32_t frames, uint32_t channels)
    {
        float music[2 * kSamplesPerFrame];
        frames = std::min(frames, kSamplesPerFrame);
        uint32_t got = 0;
        float gain;
        bool ended = false, failed = false;
        uint64_t song = 0;
        {
            std::lock_guard lock(s.mutex);
            gain = s.volume;
            if (s.song != 0 && !s.paused && !s.open)
            {
                got = std::min(frames, s.size);
                for (uint32_t i = 0; i < got; i++)
                {
                    uint32_t slot = (s.head + i) % kRingFrames;
                    music[i * 2] = s.ring[slot * 2];
                    music[i * 2 + 1] = s.ring[slot * 2 + 1];
                }
                s.head = (s.head + got) % kRingFrames;
                s.size -= got;
                if (s.size < kRefillBelow)
                    s.wake.notify_all();
                if (s.decoded && s.size == 0 && !s.reported)
                {
                    s.reported = true;
                    ended = true;
                    failed = s.failed;
                    song = s.song;
                }
            }
        }
        // The gain ramps over the frame from where the last one ended.
        float from = s_gain;
        for (uint32_t i = 0; i < got; i++)
        {
            float g = from + (gain - from) * float(i + 1) / float(frames);
            out[i * channels] += music[i * 2] * g;
            out[i * channels + 1] += music[i * 2 + 1] * g;
        }
        s_gain = gain;
        // BURNOUT_AUDIO_LOG=1: the music's own level every ~2 s, next to the
        // mix's (audio.cpp).
        static const bool logLevels = std::getenv("BURNOUT_AUDIO_LOG") != nullptr;
        if (logLevels)
        {
            static double sumSquares = 0;
            static uint32_t logFrames = 0;
            for (uint32_t i = 0; i < got * 2; i++)
                sumSquares += double(music[i]) * music[i] * gain * gain;
            if (++logFrames == 375)
            {
                std::lock_guard lock(s.mutex);
                fprintf(stderr, "[music] level: rms %.4f (gain %.2f%s, %.2f s queued)\n", std::sqrt(sumSquares / (375.0 * frames * 2)),
                    gain, s.song == 0 ? ", stopped" : s.paused ? ", paused" : "", double(s.size) / kRate);
                sumSquares = 0;
                logFrames = 0;
            }
        }
        // Outside the lock: the handler starts the next song (Play).
        if (ended && s_handler)
            s_handler(song, failed);
    }
}
