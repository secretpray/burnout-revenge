// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include "save_commit.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <system_error>
#include <unistd.h>

namespace savecommit
{
    namespace fs = std::filesystem;

    namespace
    {
        fs::path StagingFolder(const fs::path& saveRoot)
        {
            fs::path root = saveRoot;
            if (!root.has_filename())
                root = root.parent_path();
            return root.parent_path() / (root.filename().string() + "-incoming");
        }

        bool Inside(const fs::path& path, const fs::path& root)
        {
            const fs::path p = path.lexically_normal(), r = root.lexically_normal();
            auto pi = p.begin();
            for (auto ri = r.begin(); ri != r.end(); ++ri, ++pi)
            {
                if (ri->empty())
                    continue;  // a trailing separator
                if (pi == p.end() || *pi != *ri)
                    return false;
            }
            return pi != p.end();
        }

        int FlushToDisk(int fd)
        {
#ifdef __APPLE__
            // fsync on macOS doesn't flush the drive's cache; F_FULLFSYNC does
            // (not every file system supports it).
            if (fcntl(fd, F_FULLFSYNC) == 0)
                return 0;
#endif
            return fsync(fd) == 0 ? 0 : errno;
        }
    }

    std::optional<fs::path> StagingFor(const fs::path& host, const fs::path& saveRoot)
    {
        if (saveRoot.empty() || !Inside(host, saveRoot))
            return std::nullopt;
        static std::atomic<uint32_t> next{ 0 };
        const uint32_t n = next.fetch_add(1) + 1;
        return StagingFolder(saveRoot) / std::format("{}-{}", n, host.filename().string());
    }

    int Begin(const fs::path& staging, const fs::path& target, bool keepContents, std::string& error)
    {
        std::error_code ec;
        fs::create_directories(staging.parent_path(), ec);
        fs::remove(staging, ec);
        if (keepContents && fs::exists(target, ec))
        {
            if (!fs::copy_file(target, staging, fs::copy_options::overwrite_existing, ec))
            {
                error = std::format("can't copy {}: {}", target.string(), ec.message());
                return -1;
            }
        }
        const int fd = open(staging.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | (keepContents ? 0 : O_TRUNC), 0644);
        if (fd < 0)
        {
            error = std::format("can't create {}: {}", staging.string(), strerror(errno));
            fs::remove(staging, ec);
        }
        return fd;
    }

    std::string Commit(int fd, const fs::path& staging, const fs::path& target)
    {
        int error = FlushToDisk(fd);
        if (close(fd) != 0 && !error)
            error = errno;
        if (error)
        {
            unlink(staging.c_str());
            return std::format("can't write {}: {}", staging.string(), strerror(error));
        }
        if (rename(staging.c_str(), target.c_str()) != 0)
        {
            error = errno;
            unlink(staging.c_str());
            return std::format("can't replace {}: {}", target.string(), strerror(error));
        }
        const fs::path dir = target.parent_path();
        if (int dirFd = open(dir.empty() ? "." : dir.c_str(), O_RDONLY | O_CLOEXEC); dirFd >= 0)
        {
            fsync(dirFd);  // best effort: some file systems refuse it on directories
            close(dirFd);
        }
        return "";
    }

    size_t RemoveLeftovers(const fs::path& saveRoot)
    {
        std::error_code ec;
        const fs::path folder = StagingFolder(saveRoot);
        size_t removed = 0;
        for (const auto& entry : fs::directory_iterator(folder, ec))
        {
            std::error_code rc;
            if (fs::remove_all(entry.path(), rc) > 0)
                removed++;
        }
        return removed;
    }
}
