// Burnout Revenge. GPL-3.0-or-later (see COPYING).
// Independent FFmpeg CLI fixtures: metadata, resampling, bounded reads and EOF.
#include <platform/audio_file.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
int main(int argc, char** argv)
{
    assert(argc == 2);
    setbuf(stdout, nullptr);
    namespace af = platform::audio_file;
    const std::filesystem::path dir(argv[1]);
    for (const char* name : {"tone.mp3", "tone.m4a", "tone.flac", "tone.ogg", "tone.opus", "tone.wav", "tone.aiff"})
    {
        const auto path = dir / name;
        assert(af::IsMusicFile(path));
        std::string error;
        auto reader = af::Open(path, error);
        if (!reader) std::fprintf(stderr, "%s: %s\n", name, error.c_str());
        assert(reader);
        std::vector<float> out{0.25f, -0.25f};
        for (unsigned i = 0; reader->Read(out, i % 2 ? 4096 : 17); ++i)
        {
            assert(out.size() <= 50000);
            assert(i < 5000);
        }
        assert(out[0] == 0.25f && out[1] == -0.25f);
        const size_t frames = out.size() / 2 - 1;
        assert(frames >= 23000 && frames < 26000);
        float peak = 0;
        for (float value : out) { assert(std::isfinite(value)); peak = std::max(peak, std::fabs(value)); }
        assert(peak >= 0.1f && peak < 1.1f);
        const size_t end = out.size();
        assert(!reader->Read(out, 4096) && out.size() == end);
        const auto tags = af::ReadTags(path);
        std::printf("%s tags: %s / %s\n", name, tags.title.c_str(), tags.artist.c_str());
        assert(tags.title == "Linux fixture" && tags.artist == "Synthetic artist");
        assert(tags.seconds > 0.45 && tags.seconds < 0.65);
        std::printf("%s: %zu frames, metadata/EOF PASS\n", name, frames);
    }
    std::string error;
    assert(!af::Open(dir / "missing.wav", error) && !error.empty());
    assert(!af::Open(dir / "corrupt.mp3", error));
    assert(!af::IsMusicFile(dir / "image.png"));
}
