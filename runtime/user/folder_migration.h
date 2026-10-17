// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <filesystem>
#include <format>
#include <string>

namespace userpaths
{
    // Rename as one operation: saves, settings and the installed game move
    // together. Optionally keep a relative link for older builds. Never
    // merge or overwrite two independently existing profiles.
    inline std::string MigrateFolder(const std::filesystem::path& base,
        const char* oldName, const char* newName, const char*& activeName, bool leaveLink = true)
    {
        namespace fs = std::filesystem;
        fs::path from = base / oldName, to = base / newName;
        std::error_code ec;
        fs::file_status old = fs::symlink_status(from, ec);
        if (ec || !fs::exists(old)) return {};
        if (fs::is_symlink(old) && fs::read_symlink(from, ec) == fs::path(newName)) return {};
        if (fs::exists(fs::symlink_status(to, ec)))
            return std::format("[paths] {} and {} both exist: using {}, {} left as it is\n",
                from.string(), to.string(), to.string(), from.string());
        fs::rename(from, to, ec);
        if (ec)
        {
            activeName = oldName;
            return std::format("[paths] couldn't rename {} to {} ({}): using it as it is\n",
                from.string(), to.string(), ec.message());
        }
        if (!leaveLink)
            return std::format("[paths] renamed {} to {}\n", from.string(), to.string());
        std::error_code linkEc;
        fs::create_directory_symlink(newName, from, linkEc);
        return std::format("[paths] renamed {} to {}{}\n", from.string(), to.string(),
            linkEc ? std::format("; no link at the old name ({})", linkEc.message()) : ", the old name now a link to it");
    }
}
