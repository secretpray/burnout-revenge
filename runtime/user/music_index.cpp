// Burnout Revenge runtime. GPL-3.0-or-later (see COPYING).
#include "music_index.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cstdlib>

namespace user::my_music
{
    struct MusicIndex::State
    {
        std::mutex mutex;
        std::condition_variable wake;
        std::atomic<bool> stop{false};
        std::atomic<uint64_t> generation{0};
        std::shared_ptr<const LibrarySnapshot> snapshot = std::make_shared<const LibrarySnapshot>();
        std::filesystem::path folder;
        Scanner scanner;
        std::chrono::milliseconds interval;
        bool requested = false;
    };

    MusicIndex::MusicIndex(Scanner scanner, std::chrono::milliseconds interval) : m_state(std::make_shared<State>())
    {
        m_state->scanner = std::move(scanner);
        m_state->interval = interval;
        // The worker captures no this pointer, UI object or guest memory. There
        // can be only one scan at a time, even across rapid folder changes.
        std::thread([s=m_state] {
            std::unique_lock lock(s->mutex);
            bool active = false;
            while (!s->stop.load())
            {
                if (active)
                    s->wake.wait_for(lock, s->interval, [&] { return s->stop.load() || s->requested; });
                else
                    s->wake.wait(lock, [&] { return s->stop.load() || s->requested; });
                if (s->stop.load()) break;
                active = true;
                s->requested = false;
                const auto folder = s->folder;
                const uint64_t generation = s->generation.load();
                lock.unlock();
                auto cancelled = [s,generation] { return s->stop.load() || s->generation.load() != generation; };
                const auto start = std::chrono::steady_clock::now();
                LibrarySnapshot result;
                try { result = s->scanner(folder, cancelled); }
                catch (const std::exception& e) { result.error = e.what(); }
                catch (...) { result.error = "Could not read the music folder."; }
                result.folder = folder;
                result.generation = generation;
                result.ready = true;
                result.scanning = false;
                result.scanMs = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                if (!cancelled() && std::getenv("BURNOUT_LOG_MUSIC_INDEX"))
                    fprintf(stderr, "[music-index] generation=%llu tracks=%zu limited=%d error=%s scan=%.3f ms\n",
                        (unsigned long long)generation,result.tracks.size(),result.limited,result.error.c_str(),result.scanMs);
                auto published = std::make_shared<const LibrarySnapshot>(std::move(result));
                lock.lock();
                if (!cancelled()) s->snapshot = std::move(published);
            }
        }).detach();
    }

    MusicIndex::~MusicIndex() { Stop(); }

    void MusicIndex::Request(const std::filesystem::path& folder, bool refresh)
    {
        std::lock_guard lock(m_state->mutex);
        if (m_state->stop.load()) return;
        if (m_state->generation.load() == 0 || folder != m_state->folder)
        {
            m_state->folder = folder;
            const auto generation = m_state->generation.fetch_add(1)+1;
            auto empty = std::make_shared<LibrarySnapshot>();
            empty->folder = folder;
            empty->generation = generation;
            empty->scanning = true;
            m_state->snapshot = empty;
            m_state->requested = true;
        }
        else if (refresh) m_state->requested = true;
        if (m_state->requested) m_state->wake.notify_one();
    }

    std::shared_ptr<const LibrarySnapshot> MusicIndex::Get() const { std::lock_guard lock(m_state->mutex); return m_state->snapshot; }

    void MusicIndex::Stop()
    {
        std::lock_guard lock(m_state->mutex);
        m_state->stop.store(true);
        m_state->generation.fetch_add(1);
        m_state->wake.notify_all();
    }
}
