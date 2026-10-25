// Burnout Revenge. GPL-3.0-or-later (see COPYING).
// Linux local music: patched FFmpeg, custom file IO, no network protocols.
#ifdef __linux__
#include "audio_file.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <limits>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace platform::audio_file
{
    namespace
    {
        class File
        {
        public:
            AVFormatContext* format = nullptr;
            AVIOContext* io = nullptr;
            FILE* file = nullptr;
            ~File()
            {
                avformat_close_input(&format);
                if (io) { av_freep(&io->buffer); avio_context_free(&io); }
                if (file) fclose(file);
            }
            bool Open(const std::filesystem::path& path)
            {
                file = fopen(path.c_str(), "rb");
                if (!file) return false;
                auto* buffer = static_cast<uint8_t*>(av_malloc(32768));
                if (!buffer) return false;
                io = avio_alloc_context(buffer, 32768, 0, file, Read, nullptr, Seek);
                if (!io) { av_free(buffer); return false; }
                format = avformat_alloc_context();
                if (!format) return false;
                format->pb = io;
                format->flags |= AVFMT_FLAG_CUSTOM_IO;
                // The filename is only a format hint. All bytes come from our
                // open file; the build has no protocols or network support.
                return avformat_open_input(&format, path.c_str(), nullptr, nullptr) >= 0 &&
                    avformat_find_stream_info(format, nullptr) >= 0;
            }
        private:
            static int Read(void* p, uint8_t* data, int count)
            {
                auto* f = static_cast<FILE*>(p);
                const size_t n = fread(data, 1, count, f);
                return n ? int(n) : (ferror(f) ? AVERROR(EIO) : AVERROR_EOF);
            }
            static int64_t Seek(void* p, int64_t offset, int whence)
            {
                auto* f = static_cast<FILE*>(p);
                if (whence == AVSEEK_SIZE)
                {
                    const auto at = ftello(f);
                    if (at < 0 || fseeko(f, 0, SEEK_END)) return AVERROR(EIO);
                    const auto size = ftello(f);
                    if (fseeko(f, at, SEEK_SET)) return AVERROR(EIO);
                    return size;
                }
                whence &= ~AVSEEK_FORCE;
                if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) return AVERROR(EINVAL);
                return fseeko(f, offset, whence) ? AVERROR(EIO) : ftello(f);
            }
        };

        class FFmpegReader final : public Reader
        {
        public:
            ~FFmpegReader() override
            {
                av_packet_free(&packet);
                av_frame_free(&frame);
                swr_free(&swr);
                avcodec_free_context(&codec);
            }
            bool OpenFile(const std::filesystem::path& path)
            {
                if (!input.Open(path)) return false;
                stream = av_find_best_stream(input.format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
                if (stream < 0) return false;
                const auto* par = input.format->streams[stream]->codecpar;
                const auto* decoder = avcodec_find_decoder(par->codec_id);
                if (!decoder || !(codec = avcodec_alloc_context3(decoder)) ||
                    avcodec_parameters_to_context(codec, par) < 0 || avcodec_open2(codec, decoder, nullptr) < 0)
                    return false;
                AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
                if (codec->sample_rate <= 0 || swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, 48000,
                    &codec->ch_layout, codec->sample_fmt, codec->sample_rate, 0, nullptr) < 0 || swr_init(swr) < 0)
                    return false;
                return (packet = av_packet_alloc()) && (frame = av_frame_alloc());
            }
            bool Read(std::vector<float>& out, uint32_t frames) override
            {
                if (!frames) return false;
                const size_t requested = size_t(std::min(frames, uint32_t(65536))) * 2;
                size_t written = 0;
                while (written < requested)
                {
                    if (at < pending.size())
                    {
                        const size_t n = std::min(requested - written, pending.size() - at);
                        out.insert(out.end(), pending.begin() + at, pending.begin() + at + n);
                        at += n;
                        written += n;
                        continue;
                    }
                    pending.clear(); at = 0;
                    if (done) break;
                    Decode();
                }
                return written != 0;
            }
        private:
            File input;
            AVCodecContext* codec = nullptr;
            AVPacket* packet = nullptr;
            AVFrame* frame = nullptr;
            SwrContext* swr = nullptr;
            int stream = -1, errors = 0;
            bool flushing = false, done = false;
            std::vector<float> pending;
            size_t at = 0;
            void Convert(const uint8_t** data, int samples)
            {
                const int capacity = swr_get_out_samples(swr, samples);
                if (capacity <= 0 || capacity > 48000 * 60) { done = true; return; }
                pending.resize(size_t(capacity) * 2);
                uint8_t* output = reinterpret_cast<uint8_t*>(pending.data());
                const int n = swr_convert(swr, &output, capacity, data, samples);
                pending.resize(n > 0 ? size_t(n) * 2 : 0);
                if (n < 0) done = true;
            }
            void Decode()
            {
                while (!done)
                {
                    const int r = avcodec_receive_frame(codec, frame);
                    if (r == 0)
                    {
                        Convert(const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
                        av_frame_unref(frame);
                        return;
                    }
                    if (r == AVERROR_EOF)
                    {
                        Convert(nullptr, 0);
                        done = true;
                        return;
                    }
                    if (r != AVERROR(EAGAIN))
                    {
                        if (++errors > 64) done = true;
                        continue;
                    }
                    // EAGAIN after sending EOF must not spin forever on a
                    // damaged file. Ordinary decoders return AVERROR_EOF.
                    if (flushing) { done = true; return; }
                    int read;
                    do
                    {
                        av_packet_unref(packet);
                        read = av_read_frame(input.format, packet);
                    } while (read >= 0 && packet->stream_index != stream);
                    if (read < 0)
                    {
                        avcodec_send_packet(codec, nullptr);
                        flushing = true;
                    }
                    else if (avcodec_send_packet(codec, packet) < 0 && ++errors > 64) done = true;
                    av_packet_unref(packet);
                }
            }
        };
    }

    std::unique_ptr<Reader> Open(const std::filesystem::path& path, std::string& error)
    {
        auto reader = std::make_unique<FFmpegReader>();
        if (!reader->OpenFile(path)) { error = "can't be opened or decoded as local audio"; return nullptr; }
        error.clear();
        return reader;
    }
    Tags ReadTags(const std::filesystem::path& path)
    {
        Tags tags;
        tags.title = path.stem().string();
        File input;
        if (!input.Open(path)) return tags;
        const int stream = av_find_best_stream(input.format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        auto tag = [&](const char* name) -> std::string
        {
            auto* entry = av_dict_get(input.format->metadata, name, nullptr, 0);
            if (!entry && stream >= 0) entry = av_dict_get(input.format->streams[stream]->metadata, name, nullptr, 0);
            return entry ? entry->value : "";
        };
        if (auto title = tag("title"); !title.empty()) tags.title = title;
        tags.artist = tag("artist");
        if (input.format->duration > 0 && input.format->duration != AV_NOPTS_VALUE)
            tags.seconds = double(input.format->duration) / AV_TIME_BASE;
        return tags;
    }
    bool IsMusicFile(const std::filesystem::path& path)
    {
        auto ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        for (const char* allowed : {".mp3", ".m4a", ".aac", ".flac", ".wav", ".aif", ".aiff", ".ogg", ".opus"})
            if (ext == allowed) return true;
        return false;
    }
}
#endif
