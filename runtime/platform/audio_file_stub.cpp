// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See audio_file.h.
//
// Where there is no system decoder for the player's music (not Apple):
// nothing opens, and those songs are skipped.
#if !defined(__APPLE__) && !defined(__linux__)
#include "audio_file.h"

namespace platform::audio_file
{
    std::unique_ptr<Reader> Open(const std::filesystem::path&, std::string& error)
    {
        error = "isn't supported on this platform";
        return nullptr;
    }

    Tags ReadTags(const std::filesystem::path& path)
    {
        Tags tags;
        tags.title = path.stem().string();
        return tags;
    }

    bool IsMusicFile(const std::filesystem::path&) { return false; }
}
#endif
