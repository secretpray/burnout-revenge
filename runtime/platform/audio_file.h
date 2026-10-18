// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The player's own music files, decoded by the system (macOS: AudioToolbox's
// ExtAudioFile, platform/apple/audio_file.mm): MP3, AAC/M4A, ALAC, FLAC, WAV,
// AIFF and whatever else Core Audio reads, as 48 kHz stereo float. Elsewhere
// Open fails (apu/music.cpp then skips the song). Any thread.
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace platform::audio_file
{
    class Reader
    {
    public:
        virtual ~Reader() = default;
        // Appends up to `frames` interleaved stereo frames; false at the end.
        virtual bool Read(std::vector<float>& out, uint32_t frames) = 0;
    };

    // nullptr (with `error`) when the file can't be read as audio.
    std::unique_ptr<Reader> Open(const std::filesystem::path& path, std::string& error);

    struct Tags
    {
        std::string title, artist;  // empty when the file has none
        double seconds = 0;
    };
    Tags ReadTags(const std::filesystem::path& path);

    // Whether `path`'s extension is one the system reads (not WMA: the
    // game's own EA Trax go through FFmpeg).
    bool IsMusicFile(const std::filesystem::path& path);
}
