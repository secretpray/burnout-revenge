// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// My Music's shared background index and cached selections for XMP/UI.
#include "my_music.h"
#include "music_index.h"
#include "settings.h"
#include <platform/audio_file.h>
namespace ui { void Toast(std::string text, double seconds); }
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <map>
#include <thread>

namespace user::my_music
{
    namespace
    {
        struct CachedTags
        {
            std::filesystem::file_time_type modified;
            uintmax_t bytes = 0;
            std::string name;
        };
        struct LibraryScanner
        {
            std::map<std::filesystem::path, CachedTags> cached;
            LibrarySnapshot operator()(const std::filesystem::path& folder, const MusicIndex::Cancel& cancelled)
            {
                LibrarySnapshot result;
                std::error_code error;
                std::filesystem::recursive_directory_iterator it(folder, error), end;
                if (error) { result.error = error.message(); return result; }
                constexpr size_t kMaxEntries = 16384, kMaxTracks = 4096;
                size_t entries = 0;
                std::map<std::filesystem::path, CachedTags> nextCache;
                // Deliberately slow test I/O, only on the index worker. No real
                // library is needed to measure UI/selection responsiveness.
                const char* delay = std::getenv("BURNOUT_TEST_MUSIC_SCAN_MS");
                const int delayMs = delay ? std::clamp(std::atoi(delay), 0, 100) : 0;
                while (it != end && !cancelled())
                {
                    if (++entries > kMaxEntries || result.tracks.size() >= kMaxTracks) { result.limited = true; break; }
                    const auto path = it->path();
                    if (path.filename().string().starts_with(".")) it.disable_recursion_pending();
                    else if (it->is_regular_file(error) && !error && platform::audio_file::IsMusicFile(path))
                    {
                        const auto modified = it->last_write_time(error);
                        const auto bytes = error ? 0 : it->file_size(error);
                        auto old = cached.find(path);
                        std::string name;
                        if (!error && old != cached.end() && old->second.modified == modified && old->second.bytes == bytes)
                            name = old->second.name;
                        else
                        {
                            const auto tags = platform::audio_file::ReadTags(path);
                            if (cancelled()) break;
                            name = DisplayName(path, tags.title, tags.artist);
                        }
                        if (!error) nextCache.emplace(path,CachedTags{modified,bytes,name});
                        result.tracks.push_back({path, std::move(name)});
                    }
                    if (error) { result.error = error.message(); error.clear(); }
                    // Test delay is cancellable; an OS filesystem operation can
                    // still block, so MusicIndex::Stop never joins the worker.
                    for (int ms=0; ms<delayMs && !cancelled(); ++ms)
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    it.increment(error);
                    if (error) { result.error = error.message(); break; }
                }
                std::sort(result.tracks.begin(),result.tracks.end(),[](const Track& a,const Track& b){return a.file<b.file;});
                if (!cancelled()) cached = std::move(nextCache);
                return result;
            }
        };
        std::mutex s_readyMutex;
        std::function<void()> s_ready;
        MusicIndex& Index()
        {
            static MusicIndex index(LibraryScanner{});
            return index;
        }
    }

    std::shared_ptr<const LibrarySnapshot> Library(bool refresh)
    {
        auto& index = Index();
        index.Request(Folder(), refresh);
        return index.Get();
    }
    void PollLibrary()
    {
        if (!Chosen()) return;
        const auto library = Library();
        static bool hadTracks = false;
        const bool hasTracks = library->ready && !library->tracks.empty();
        std::function<void()> ready;
        { std::lock_guard lock(s_readyMutex); ready = s_ready; }
        if (hasTracks && !hadTracks && ready) ready();
        hadTracks = hasTracks && bool(ready);
    }
    void StopLibrary() { Index().Stop(); }

    bool Chosen()
    {
        return settings::GetInt(settings::Id::Music) == int32_t(settings::MusicSource::MyMusic);
    }

    Pick Next()
    {
        const auto library = Library();
        static std::mutex mutex;
        static Shuffle shuffle{std::random_device{}()};
        static std::vector<std::filesystem::path> files;
        static std::shared_ptr<const LibrarySnapshot> previous;
        std::lock_guard lock(mutex);
        if (previous != library)
        {
            files.clear();
            for (const auto& track : library->tracks) files.push_back(track.file);
            shuffle.SetFiles(files);
            previous = library;
        }
        Pick pick{.file=shuffle.Next(), .count=shuffle.Size(), .loading=!library->ready};
        if (!pick.file.empty())
        {
            const auto track = std::lower_bound(library->tracks.begin(),library->tracks.end(),pick.file,
                [](const Track& t,const std::filesystem::path& file){return t.file<file;});
            if (track != library->tracks.end()) pick.name = track->name;
        }
        return pick;
    }

    void Announce(const std::string& text) { ui::Toast(text, 5.0); }

    void OnChoiceChanged(std::function<void()> onChange)
    {
        { std::lock_guard lock(s_readyMutex); s_ready = onChange; }
        settings::Subscribe([onChange = std::move(onChange)](settings::Id id) {
            if (id == settings::Id::Music)
            {
                if (Chosen()) Library();
                onChange();
            }
        });
    }
}
