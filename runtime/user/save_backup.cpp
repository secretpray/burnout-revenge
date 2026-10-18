// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See save_backup.h.
#include "save_backup.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>

namespace user::save_backup
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr const char* kPending = "restore-pending.txt";
        constexpr const char* kIncoming = ".incoming-";
        constexpr const char* kBeforeRestore = "-before-restore";
        constexpr const char* kImported = "-imported";

        // Every file under `dir`, by its path relative to it, with its bytes.
        std::map<std::string, std::string> Contents(const fs::path& dir)
        {
            std::map<std::string, std::string> files;
            std::error_code ec;
            if (!fs::is_directory(dir, ec))
                return files;
            for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (!it->is_regular_file(ec))
                    continue;
                std::ifstream in(it->path(), std::ios::binary);
                files[fs::relative(it->path(), dir, ec).generic_string()] =
                    std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            }
            return files;
        }

        bool IsBackupName(const std::string& name)
        {
            int y, mo, d, h, mi, s;
            return name.size() >= 19 && std::sscanf(name.c_str(), "%4d-%2d-%2d_%2d-%2d-%2d", &y, &mo, &d, &h, &mi, &s) == 6;
        }

        std::string Now()
        {
            std::time_t t = std::time(nullptr);
            std::tm local{};
            localtime_r(&t, &local);
            char buffer[32];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H-%M-%S", &local);
            return buffer;
        }

        // A free name for a backup made now, with `suffix`: after every name
        // made in the same second (-02 ... two digits, so names still sort by
        // time), even ones pruned since (a reused name would sort as older).
        std::string FreeName(const fs::path& backupsDir, const std::string& suffix)
        {
            const std::string base = Now() + suffix;
            std::error_code ec;
            int last = 0;  // 0: none yet; 1: the plain name; n: -n
            for (const auto& entry : fs::directory_iterator(backupsDir, ec))
            {
                std::string name = entry.path().filename().string();
                if (name.starts_with(kIncoming))
                    name = name.substr(std::strlen(kIncoming));
                if (name == base)
                    last = std::max(last, 1);
                else if (name.size() == base.size() + 3 && name.starts_with(base) && name[base.size()] == '-')
                    last = std::max(last, std::atoi(name.c_str() + base.size() + 1));
            }
            static std::string s_lastBase;
            static int s_lastNumber = 0;
            if (s_lastBase == base)
                last = std::max(last, s_lastNumber);  // pruned since, in this process
            const int number = last + 1;
            s_lastBase = base;
            s_lastNumber = number;
            return number == 1 ? base : std::format("{}-{:02}", base, number);
        }

        // `from` copied whole into backupsDir/<name>: into an incoming
        // folder, checked, then renamed. False (logged in `log`) if any of it failed.
        bool Snapshot(const fs::path& from, const fs::path& backupsDir, const std::string& name, std::vector<std::string>& log)
        {
            std::error_code ec;
            fs::create_directories(backupsDir, ec);
            const fs::path incoming = backupsDir / (kIncoming + name);
            fs::remove_all(incoming, ec);
            fs::copy(from, incoming, fs::copy_options::recursive, ec);
            if (ec || Contents(incoming) != Contents(from))
            {
                log.push_back(std::format("[saves] couldn't back up {} ({}): the saves are unchanged", from.string(),
                    ec ? ec.message() : "the copy differs"));
                fs::remove_all(incoming, ec);
                return false;
            }
            fs::rename(incoming, backupsDir / name, ec);
            if (ec)
            {
                log.push_back(std::format("[saves] couldn't finish the backup {}: {}", name, ec.message()));
                fs::remove_all(incoming, ec);
                return false;
            }
            return true;
        }

        // The newest kKeep ordinary backups stay; older ones go.
        void Prune(const fs::path& backupsDir, std::vector<std::string>& log)
        {
            size_t kept = 0;
            for (const Backup& b : List(backupsDir))
            {
                if (b.beforeRestore || b.imported || ++kept <= kKeep)
                    continue;
                std::error_code ec;
                fs::remove_all(b.path, ec);
                log.push_back(std::format("[saves] removed the old backup {}{}", b.name, ec ? " (failed: " + ec.message() + ")" : ""));
            }
        }

        // The pending restore, if any, before the game runs.
        void ApplyRestore(const fs::path& saveDir, const fs::path& backupsDir, std::vector<std::string>& log)
        {
            const std::string name = PendingRestore(backupsDir);
            if (name.empty())
                return;
            CancelRestore(backupsDir);  // tried once, whatever happens
            const fs::path chosen = backupsDir / name;
            if (Contents(chosen).empty())
            {
                log.push_back(std::format("[saves] restore of {} not done: it holds no files", name));
                return;
            }
            std::error_code ec;
            if (!Contents(saveDir).empty() && !Snapshot(saveDir, backupsDir, FreeName(backupsDir, kBeforeRestore), log))
            {
                log.push_back("[saves] restore not done: the current saves couldn't be backed up first");
                return;
            }
            const fs::path incoming = saveDir.parent_path() / (saveDir.filename().string() + ".restoring");
            const fs::path old = saveDir.parent_path() / (saveDir.filename().string() + ".replaced");
            fs::remove_all(incoming, ec);
            fs::remove_all(old, ec);
            fs::copy(chosen, incoming, fs::copy_options::recursive, ec);
            if (ec || Contents(incoming) != Contents(chosen))
            {
                fs::remove_all(incoming, ec);
                log.push_back(std::format("[saves] restore of {} not done: the copy failed; the saves are unchanged", name));
                return;
            }
            if (fs::exists(saveDir, ec))
                fs::rename(saveDir, old, ec);
            if (!ec)
                fs::rename(incoming, saveDir, ec);
            if (ec)
            {
                // Put back what was there.
                std::error_code back;
                if (!fs::exists(saveDir, back) && fs::exists(old, back))
                    fs::rename(old, saveDir, back);
                fs::remove_all(incoming, back);
                log.push_back(std::format("[saves] restore of {} not done: {}; the saves are unchanged", name, ec.message()));
                return;
            }
            fs::remove_all(old, ec);
            log.push_back(std::format("[saves] restored the backup {} ({})", name, DisplayName(name)));
        }
    }

    std::vector<std::string> OnLaunch(const fs::path& saveDir, const fs::path& backupsDir)
    {
        std::vector<std::string> log;
        std::error_code ec;
        // Leftovers of a launch that stopped half way through a copy.
        if (fs::is_directory(backupsDir, ec))
            for (const auto& entry : fs::directory_iterator(backupsDir, ec))
                if (entry.path().filename().string().starts_with(kIncoming))
                    fs::remove_all(entry.path(), ec);
        ApplyRestore(saveDir, backupsDir, log);
        const auto current = Contents(saveDir);
        if (current.empty())
            return log;
        std::vector<Backup> backups = List(backupsDir);
        auto newest = std::find_if(backups.begin(), backups.end(), [](const Backup& b) { return !b.beforeRestore; });
        if (newest != backups.end() && Contents(newest->path) == current)
            return log;  // unchanged since the last one
        const std::string name = FreeName(backupsDir, "");
        if (Snapshot(saveDir, backupsDir, name, log))
        {
            uint64_t bytes = 0;
            for (const auto& [path, data] : current)
                bytes += data.size();
            log.push_back(std::format("[saves] backed up {} files ({} bytes) as {}", current.size(), bytes, name));
            Prune(backupsDir, log);
        }
        return log;
    }

    std::vector<Backup> List(const fs::path& backupsDir)
    {
        std::vector<Backup> backups;
        std::error_code ec;
        if (!fs::is_directory(backupsDir, ec))
            return backups;
        for (const auto& entry : fs::directory_iterator(backupsDir, ec))
        {
            const std::string name = entry.path().filename().string();
            if (!entry.is_directory(ec) || !IsBackupName(name))
                continue;
            Backup b{ name, entry.path() };
            for (const auto& [path, data] : Contents(entry.path()))
            {
                b.bytes += data.size();
                b.files++;
            }
            b.beforeRestore = name.find(kBeforeRestore) != std::string::npos;
            b.imported = name.find(kImported) != std::string::npos;
            backups.push_back(std::move(b));
        }
        // Names sort by time (zero-padded, a -2 after the plain one).
        std::sort(backups.begin(), backups.end(), [](const Backup& a, const Backup& b) { return a.name > b.name; });
        return backups;
    }

    bool RequestRestore(const fs::path& backupsDir, const std::string& name, std::string& error)
    {
        if (!IsBackupName(name) || name.find('/') != std::string::npos || Contents(backupsDir / name).empty())
        {
            error = "That backup holds no saves.";
            return false;
        }
        std::ofstream out(backupsDir / kPending, std::ios::trunc);
        out << name << '\n';
        if (!out)
        {
            error = "Couldn't record the restore.";
            return false;
        }
        return true;
    }

    bool RequestImport(const fs::path& saveDir, const fs::path& backupsDir,
        const std::function<bool(const fs::path& folder, std::string& error)>& add, std::string& name, std::string& error)
    {
        std::error_code ec;
        fs::create_directories(backupsDir, ec);
        name = FreeName(backupsDir, kImported);
        const fs::path incoming = backupsDir / (kIncoming + name);
        fs::remove_all(incoming, ec);
        if (fs::is_directory(saveDir, ec))
            fs::copy(saveDir, incoming, fs::copy_options::recursive, ec);
        else
            fs::create_directories(incoming, ec);
        if (ec || Contents(incoming) != Contents(saveDir))
        {
            error = "Couldn't copy the current saves: " + (ec ? ec.message() : std::string("the copy differs")) + ".";
            fs::remove_all(incoming, ec);
            return false;
        }
        if (!add(incoming, error))
        {
            fs::remove_all(incoming, ec);
            return false;
        }
        fs::rename(incoming, backupsDir / name, ec);
        if (ec)
        {
            error = "Couldn't record the import: " + ec.message() + ".";
            fs::remove_all(incoming, ec);
            return false;
        }
        return RequestRestore(backupsDir, name, error);
    }

    std::string PendingRestore(const fs::path& backupsDir)
    {
        std::ifstream in(backupsDir / kPending);
        std::string name;
        std::getline(in, name);
        return IsBackupName(name) && name.find('/') == std::string::npos ? name : std::string();
    }

    void CancelRestore(const fs::path& backupsDir)
    {
        std::error_code ec;
        fs::remove(backupsDir / kPending, ec);
    }

    std::string DisplayName(const std::string& name)
    {
        int y, mo, d, h, mi, s;
        if (!IsBackupName(name) || std::sscanf(name.c_str(), "%4d-%2d-%2d_%2d-%2d-%2d", &y, &mo, &d, &h, &mi, &s) != 6 || mo < 1 || mo > 12)
            return {};
        static const char* const kMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        return std::format("{} {} {}, {:02}:{:02}", d, kMonths[mo - 1], y, h, mi);
    }
}
