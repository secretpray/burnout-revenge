// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#include "achievements.h"
#include "save_commit.h"
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace achievements
{
    Store& Current() { static Store store; return store; }

    bool Store::Initialize(std::span<const uint8_t> database, std::filesystem::path file, uint32_t language)
    {
        std::lock_guard lock(mutex);
        definitions = ParseDatabase(database, language); path = std::move(file); notices.clear(); error.clear(); unreadable = false;
        for (auto& user : users)
        {
            user.clear();
            for (auto& d : definitions) user.push_back({ d.id, 0 });
        }
        if (definitions.empty()) { error = "Achievement data couldn't be read from the game."; unreadable = true; return false; }
        std::ifstream input(path);
        if (!input)
        {
            std::error_code ec;
            if (std::filesystem::exists(path, ec) || ec) error = "Saved achievements couldn't be read.";
            unreadable = !error.empty();
            return !unreadable;
        }
        std::string magic; uint32_t version = 0, title = 0;
        if (!(input >> magic >> version >> std::hex >> title >> std::dec) || magic != "BRACH" || version != 1 || title != 0x454107DC)
        { error = "Saved achievements couldn't be read. The original file has been kept."; unreadable = true; return false; }
        auto loaded = users;
        uint32_t user, id; int64_t timestamp;
        while (input >> std::ws && input.peek() != std::char_traits<char>::eof())
        {
            if (!(input >> user >> id >> timestamp) || user >= users.size() || timestamp <= 0 || timestamp > 253402300799ll)
            { error = "Saved achievements couldn't be read. The original file has been kept."; unreadable = true; return false; }
            auto it = std::find_if(loaded[user].begin(), loaded[user].end(), [&](auto& e) { return e.id == id; });
            if (it == loaded[user].end() || it->unlocked)
            { error = "Saved achievements couldn't be read. The original file has been kept."; unreadable = true; return false; }
            it->unlocked = timestamp;
        }
        if (input.bad()) { error = "Saved achievements couldn't be read."; unreadable = true; return false; }
        users = std::move(loaded);
        return true;
    }

    bool Store::Save()
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) { error = "Achievements couldn't be saved: " + ec.message(); return false; }
        std::string text = "BRACH 1 454107DC\n";
        for (size_t u = 0; u < users.size(); ++u)
            for (auto& e : users[u]) if (e.unlocked)
                text += std::to_string(u) + " " + std::to_string(e.id) + " " + std::to_string(e.unlocked) + "\n";
        const auto staging = path.parent_path() / ".achievements-incoming" / (std::to_string(getpid()) + ".tmp");
        std::string why;
        int fd = savecommit::Begin(staging, path, false, why);
        if (fd < 0) { error = "Achievements couldn't be saved: " + why; return false; }
        size_t offset = 0;
        while (offset < text.size())
        {
            ssize_t written = write(fd, text.data() + offset, text.size() - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0)
            {
                close(fd); std::filesystem::remove(staging, ec);
                error = "Achievements couldn't be saved."; return false;
            }
            offset += size_t(written);
        }
        why = savecommit::Commit(fd, staging, path);
        if (!why.empty()) { error = "Achievements couldn't be saved: " + why; return false; }
        error.clear();
        return true;
    }

    bool Store::Earn(uint32_t user, uint32_t id)
    {
        std::lock_guard lock(mutex);
        if (user >= users.size()) return false;
        auto it = std::find_if(users[user].begin(), users[user].end(), [&](auto& e) { return e.id == id; });
        if (it == users[user].end()) return false;
        if (it->unlocked) return true;
        // A damaged/unreadable file is never replaced by an empty profile.
        if (unreadable) return false;
        it->unlocked = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (!Save()) { it->unlocked = 0; return false; }
        notices.push_back({id, user});
        fprintf(stderr, "[achievements] user %u earned %u, saved at %lld\n", user, id, (long long)it->unlocked);
        return true;
    }
    std::vector<Entry> Store::Entries(uint32_t user) const
    {
        std::lock_guard lock(mutex); return user < users.size() ? users[user] : std::vector<Entry>();
    }
    std::string Store::Error() const { std::lock_guard lock(mutex); return error; }
    bool Store::HasNotices() const { std::lock_guard lock(mutex); return !notices.empty(); }
    std::optional<Notice> Store::PopNotice()
    {
        std::lock_guard lock(mutex);
        if (notices.empty()) return {};
        Notice n = notices.front(); notices.pop_front(); return n;
    }
}
