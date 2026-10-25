// Burnout Revenge runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace user::my_music
{
    struct Track { std::filesystem::path file; std::string name; };
    struct LibrarySnapshot
    {
        std::filesystem::path folder;
        uint64_t generation = 0;
        std::vector<Track> tracks;
        bool ready = false, scanning = false, limited = false;
        std::string error;
        double scanMs = 0;
    };

    // One bounded worker owns all filesystem/metadata I/O. Readers retain an
    // immutable snapshot and never join a scan. A changed folder invalidates the
    // previous generation immediately. Stop never waits on an unresponsive mount;
    // the detached worker retains only its own shared state until I/O returns.
    class MusicIndex
    {
    public:
        using Cancel = std::function<bool()>;
        using Scanner = std::function<LibrarySnapshot(const std::filesystem::path&, const Cancel&)>;
        explicit MusicIndex(Scanner scanner, std::chrono::milliseconds interval = std::chrono::seconds(2));
        ~MusicIndex();
        MusicIndex(const MusicIndex&) = delete;
        MusicIndex& operator=(const MusicIndex&) = delete;
        void Request(const std::filesystem::path& folder, bool refresh = false);
        std::shared_ptr<const LibrarySnapshot> Get() const;
        void Stop();
    private:
        struct State;
        std::shared_ptr<State> m_state;
    };

    // Ready list and count shared by Audio settings and the XMP player.
    // Requests an index asynchronously; no filesystem calls on the caller.
    std::shared_ptr<const LibrarySnapshot> Library(bool refresh = false);
    // Main thread: switch from temporary EA Trax once the first list is ready.
    void PollLibrary();
    void StopLibrary();
}
