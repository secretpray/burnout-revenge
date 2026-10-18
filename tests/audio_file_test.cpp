// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// The player's own music through the system decoder (platform/audio_file.h,
// macOS): a 0.5 s 44.1 kHz mono WAV written here reads back as ~0.5 s of
// 48 kHz stereo with its tone in both channels; its title falls back to the
// file name; WMA (the game's EA Trax) isn't taken.
//   audio_file_test [folder]
#include <platform/audio_file.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>

static int s_failures = 0;
#define CHECK(cond, ...)                                                \
    do                                                                  \
    {                                                                   \
        if (!(cond))                                                    \
        {                                                               \
            std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);   \
            std::fprintf(stderr, __VA_ARGS__);                          \
            std::fprintf(stderr, "\n");                                 \
            s_failures++;                                               \
        }                                                               \
    } while (0)

int main(int argc, char** argv)
{
    namespace af = platform::audio_file;
    const std::filesystem::path wav = std::filesystem::path(argc > 1 ? argv[1] : ".") / "audio_file_test tone.wav";
    {
        const uint32_t rate = 44100, n = rate / 2;
        std::ofstream f(wav, std::ios::binary | std::ios::trunc);
        auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
        f.write("RIFF", 4); u32(36 + n * 2); f.write("WAVEfmt ", 8); u32(16);
        u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
        f.write("data", 4); u32(n * 2);
        for (uint32_t i = 0; i < n; i++)
            u16(uint16_t(int16_t(std::lround(16000 * std::sin(2 * M_PI * 440 * i / rate)))));
    }
    CHECK(af::IsMusicFile(wav), "a .wav is music");
    CHECK(af::IsMusicFile("song.MP3") && af::IsMusicFile("a.m4a") && af::IsMusicFile("b.flac"), "common formats");
    CHECK(!af::IsMusicFile("eatrax/00.wma"), "WMA stays FFmpeg's");
    std::string error;
    auto reader = af::Open(wav, error);
    CHECK(reader, "opened: %s", error.c_str());
    if (reader)
    {
        std::vector<float> pcm;
        while (reader->Read(pcm, 4096))
            ;
        const size_t frames = pcm.size() / 2;
        CHECK(frames > 23000 && frames < 25000, "about 0.5 s at 48 kHz (%zu frames)", frames);
        float peakL = 0, peakR = 0;
        for (size_t i = 0; i < frames; i++)
        {
            peakL = std::max(peakL, std::fabs(pcm[i * 2]));
            peakR = std::max(peakR, std::fabs(pcm[i * 2 + 1]));
        }
        CHECK(peakL > 0.3f && peakL < 0.6f && peakR > 0.3f, "the tone in both channels (%.3f, %.3f)", peakL, peakR);
    }
    CHECK(af::ReadTags(wav).title == "audio_file_test tone", "untagged: the file's name (%s)", af::ReadTags(wav).title.c_str());
    std::filesystem::remove(wav);
    std::printf("audio_file_test: %s\n", s_failures ? "FAILED" : "ok");
    return s_failures ? 1 : 0;
}
