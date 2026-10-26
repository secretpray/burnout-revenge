// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See save_import.h.
#include "save_import.h"
#include "save_backup.h"

#include <platform/sha1.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

namespace user::save_import
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr size_t kMaxFiles = 20000;  // looked at in a folder, at most

        stfs::Digest Sha1(std::span<const uint8_t> bytes)
        {
            return platform::Sha1(bytes);
        }

        std::vector<uint8_t> ReadFile(const fs::path& path, size_t limit)
        {
            std::ifstream in(path, std::ios::binary);
            std::vector<uint8_t> bytes;
            if (!in)
                return bytes;
            in.seekg(0, std::ios::end);
            const auto size = in.tellg();
            if (size <= 0 || size_t(size) > limit)
                return bytes;
            in.seekg(0);
            bytes.resize(size_t(size));
            in.read(reinterpret_cast<char*>(bytes.data()), size);
            if (!in)
                bytes.clear();
            return bytes;
        }

        // Whether `path` starts as a package does.
        bool LooksLikePackage(const fs::path& path)
        {
            uint8_t head[4] = {};
            std::ifstream in(path, std::ios::binary);
            return in.read(reinterpret_cast<char*>(head), 4) && stfs::IsPackage(head);
        }

        // A name the game could have given a save (XCONTENT_DATA's
        // szFileName, 42 bytes), and kernel/xam.cpp would keep as it is.
        bool UsableName(const std::string& name)
        {
            return !name.empty() && name.size() <= 42 && name[0] != '.' &&
                std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= 32 && c < 127 && c != '/' && c != '\\' && c != ':'; });
        }

        bool Iequal(const std::string& a, const std::string& b)
        {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
        }

        // The display name kept beside an unpacked save (<name>.xcontent:
        // XCONTENT_DATA, the name in UTF-16BE at 8).
        std::u16string DisplayNameBeside(const fs::path& container)
        {
            fs::path meta = container;
            meta += ".xcontent";
            const std::vector<uint8_t> b = ReadFile(meta, 4096);
            std::u16string name;
            for (size_t at = 8; at + 1 < std::min<size_t>(b.size(), 0x108) && (b[at] | b[at + 1]); at += 2)
                name += char16_t((b[at] << 8) | b[at + 1]);
            return name;
        }

        // A package's saves; "" when it holds one, else why not.
        std::string FromPackage(const fs::path& path, std::span<const uint8_t> bytes, std::vector<Container>& out)
        {
            std::string error;
            auto package = stfs::Parse(bytes, Sha1, error);
            if (!package)
                return error;
            if (package->titleId != kTitleId)
                return std::format("It's another game's package (title {:08X}), not Burnout Revenge's (454107DC).", package->titleId);
            if (package->contentType != stfs::kSaveContent)
                return std::format("It isn't a save (content type {:X}).", package->contentType);
            if (package->files.empty())
                return "The save package is empty.";
            // The game names a save's container and its file alike ("Player
            // Files" holds "Player Files"); a file copied off a console may
            // have been renamed, its contents not.
            Container c;
            c.name = package->files.size() == 1 && package->files[0].path.find('/') == std::string::npos
                ? package->files[0].path : path.filename().string();
            if (!UsableName(c.name))
                return "The save's name can't be used: " + c.name;
            c.displayName = std::move(package->displayName);
            c.thumbnail = std::move(package->thumbnail);
            c.files = std::move(package->files);
            c.from = path;
            out.push_back(std::move(c));
            return {};
        }

        // An unpacked save: `dir`'s files.
        bool FromFolder(const fs::path& dir, std::vector<Container>& out)
        {
            Container c;
            c.name = dir.filename().string();
            if (!UsableName(c.name))
                return false;
            std::error_code ec;
            size_t total = 0;
            for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (!it->is_regular_file(ec))
                    continue;
                std::vector<uint8_t> bytes = ReadFile(it->path(), stfs::kMaxPackageSize);
                if ((total += bytes.size()) > stfs::kMaxPackageSize)
                    return false;
                c.files.push_back({ fs::relative(it->path(), dir, ec).generic_string(), std::move(bytes) });
            }
            if (c.files.empty())
                return false;
            c.displayName = DisplayNameBeside(dir);
            fs::path thumbnail = dir;
            thumbnail += ".thumbnail.png";
            c.thumbnail = ReadFile(thumbnail, 0x40000);
            c.from = dir;
            out.push_back(std::move(c));
            return true;
        }

        // Whether `dir` is an unpacked save: our layout (save/<name>/<name>,
        // the game names both alike), or Xenia's
        // (.../454107DC/00000001/<name>/).
        bool IsUnpacked(const fs::path& dir)
        {
            std::error_code ec;
            const fs::path parent = dir.parent_path();
            if (Iequal(parent.filename().string(), "00000001") && Iequal(parent.parent_path().filename().string(), "454107DC"))
                return true;
            const fs::path same = dir / dir.filename();
            return fs::is_regular_file(same, ec) && !LooksLikePackage(same);
        }

        bool IsSaveFileName(const std::string& name)
        {
            return Iequal(name, "Player Files") || (name.size() > 15 && Iequal(name.substr(0, 15), "Save And Share "));
        }
    }

    std::vector<Container> Find(const fs::path& source, std::string& error)
    {
        std::vector<Container> found;
        std::error_code ec;
        error.clear();
        if (fs::is_regular_file(source, ec))
        {
            std::vector<uint8_t> bytes = ReadFile(source, stfs::kMaxPackageSize);
            if (bytes.empty())
                error = "The file can't be read, or it's empty or too large.";
            else if (stfs::IsPackage(bytes))
                error = FromPackage(source, bytes, found);
            else if (IsSaveFileName(source.filename().string()))
            {
                // The progress file alone: in a container of its own name.
                Container c{ source.filename().string(), {}, {}, {}, source };
                c.files.push_back({ c.name, std::move(bytes) });
                found.push_back(std::move(c));
            }
            else
                error = "It isn't an Xbox 360 save package (CON) or the game's \"Player Files\".";
            return found;
        }
        if (!fs::is_directory(source, ec))
        {
            error = "There's nothing at " + source.string() + ".";
            return found;
        }
        if (IsUnpacked(source) && FromFolder(source, found))
            return found;

        // Everything under it, in name order (so the same folder always gives
        // the same saves); the first save of a name wins. Not our backups'
        // folder: every older copy of the same saves is in it.
        std::vector<fs::path> files, folders;
        size_t seen = 0;
        for (auto it = fs::recursive_directory_iterator(source, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator() && ++seen < kMaxFiles; it.increment(ec))
        {
            const std::string name = it->path().filename().string();
            if (it->is_directory(ec))
            {
                if (name == "backups" || name.starts_with(".") || Iequal(name, "Headers"))
                    it.disable_recursion_pending();
                else if (IsUnpacked(it->path()))
                {
                    folders.push_back(it->path());
                    it.disable_recursion_pending();
                }
            }
            else if (it->is_regular_file(ec) && it->file_size(ec) >= 0xA000 && it->file_size(ec) <= stfs::kMaxPackageSize &&
                     LooksLikePackage(it->path()))
                files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        std::sort(folders.begin(), folders.end());
        std::string why;
        size_t packages = 0;
        for (const fs::path& path : files)
        {
            std::string e = FromPackage(path, ReadFile(path, stfs::kMaxPackageSize), found);
            packages++;
            if (!e.empty() && why.empty())
                why = path.filename().string() + ": " + e;
        }
        for (const fs::path& dir : folders)
            FromFolder(dir, found);
        std::vector<Container> unique;
        std::set<std::string> names;
        for (Container& c : found)
        {
            std::string key = c.name;
            for (char& ch : key)
                ch = char(std::tolower(static_cast<unsigned char>(ch)));
            if (names.insert(key).second)
                unique.push_back(std::move(c));
        }
        if (unique.empty())
            error = packages && !why.empty() ? "No Burnout Revenge saves there. " + why
                                             : "No Burnout Revenge saves there (" + source.string() + ").";
        return unique;
    }

    std::string Describe(const std::vector<Container>& containers)
    {
        std::string text;
        for (const Container& c : containers)
            text += (text.empty() ? "" : ", ") + c.name;
        return text;
    }

    bool Write(const fs::path& folder, const std::vector<Container>& containers, std::string& error)
    {
        std::error_code ec;
        for (const Container& c : containers)
        {
            const fs::path dir = folder / c.name;
            fs::path meta = dir, thumbnail = dir;
            meta += ".xcontent";
            thumbnail += ".thumbnail.png";
            fs::remove_all(dir, ec);
            fs::remove(meta, ec);
            if (!c.thumbnail.empty())
                fs::remove(thumbnail, ec);  // (else the one there stays: Xenia keeps none)
            for (const stfs::File& file : c.files)
            {
                const fs::path to = dir / fs::path(file.path);
                fs::create_directories(to.parent_path(), ec);
                std::ofstream out(to, std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char*>(file.bytes.data()), std::streamsize(file.bytes.size()));
                if (!out)
                {
                    error = "Couldn't write " + to.string() + ".";
                    return false;
                }
            }
            // XCONTENT_DATA: device 1, content type 1 (a save), the display
            // name (UTF-16BE, 128), the file name (42), 2 bytes of padding.
            std::vector<uint8_t> data(0x134, 0);
            data[3] = 1;
            data[7] = stfs::kSaveContent;
            const std::u16string display = c.displayName.empty() ? std::u16string(c.name.begin(), c.name.end()) : c.displayName;
            for (size_t i = 0; i < std::min<size_t>(display.size(), 127); i++)
            {
                data[8 + i * 2] = uint8_t(display[i] >> 8);
                data[9 + i * 2] = uint8_t(display[i]);
            }
            std::copy(c.name.begin(), c.name.end(), data.begin() + 0x108);
            std::ofstream out(meta, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
            if (!c.thumbnail.empty())
            {
                std::ofstream png(thumbnail, std::ios::binary | std::ios::trunc);
                png.write(reinterpret_cast<const char*>(c.thumbnail.data()), std::streamsize(c.thumbnail.size()));
            }
            if (!out)
            {
                error = "Couldn't write " + meta.string() + ".";
                return false;
            }
        }
        return true;
    }

    bool Request(const fs::path& source, const fs::path& saveDir, const fs::path& backupsDir, std::vector<Container>& found,
        std::string& backupName, std::string& error)
    {
        found = Find(source, error);
        if (found.empty())
            return false;
        return save_backup::RequestImport(saveDir, backupsDir,
            [&](const fs::path& folder, std::string& e) { return Write(folder, found, e); }, backupName, error);
    }
}
