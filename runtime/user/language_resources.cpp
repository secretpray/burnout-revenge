// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#include "language_resources.h"
#include <install/disc_source.h>
#include <install/sha256.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

namespace locale
{
    namespace
    {
        struct File { const char* path; size_t size; const char* sha; };
        constexpr File files[] = {
#include "language_manifest.inc"
        };
        constexpr const char* suffixes[] = {"us", "ja", "ge", "fr", "sp", "it", "du", "sw", "fi"};
        static_assert(std::size(suffixes) == codes.size());
        std::map<std::string, std::filesystem::path, std::less<>> active;
        std::string Lower(std::string_view value)
        {
            std::string result(value);
            for (char& c : result) c = c == '\\' ? '/' : char(std::tolower(static_cast<unsigned char>(c)));
            return result;
        }
        bool Matches(const std::filesystem::path& path, const File& file)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec) || std::filesystem::file_size(path, ec) != file.size || ec) return false;
            std::ifstream in(path, std::ios::binary);
            std::vector<uint8_t> bytes(file.size);
            return bool(in.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) &&
                install::ToHex(install::Sha256Of(bytes.data(), bytes.size())) == file.sha;
        }
        std::filesystem::path OnDisc(const std::filesystem::path& root, const File& file)
        {
            auto at = root;
            for (const auto& part : std::filesystem::path(file.path))
            {
                std::error_code ec;
                auto exact = at / part;
                if (std::filesystem::exists(exact, ec)) { at = exact; continue; }
                bool found = false;
                for (const auto& entry : std::filesystem::directory_iterator(at, ec))
                    if (Lower(entry.path().filename().string()) == part.string()) { at = entry.path(); found = true; break; }
                if (!found) return {};
            }
            return at;
        }
    }
    install::Result ImportResources(const std::filesystem::path& sourcePath, const std::filesystem::path& data)
    {
        using install::Error;
        std::unique_ptr<install::DiscSource> source;
        auto opened = install::OpenDiscSource(sourcePath, source);
        if (!opened.Ok()) return {opened.error, "Can't read this disc image. Choose a complete Burnout Revenge Xbox 360 disc image.", opened.file};
        install::XexIdentity identity;
        auto identified = install::IdentifyXex(*source, identity);
        if (!identified.Ok() || identity.titleId != 0x454107DC)
            return {Error::WrongGame, "This is not a Burnout Revenge Xbox 360 disc image."};
        const std::string sha = install::ToHex(identity.sha256);
        if (sha != "5258c75d9ae649ed4fb71d4e2fe8808545db16092cfe764ea1a2504f5aae2e2d" &&
            sha != "7deff7c10a97fd27b866b57247f7d673b0079203be922fd24c4be81bc55de7c8" &&
            sha != "51e0e29d23db70cdff17aaf4219191137be8e94a2260fdddfba760d657047f10")
            return {Error::UnsupportedVersion, "This disc revision has not been verified for importing languages."};
        const auto folder = data / "languages";
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        if (ec) return {Error::WriteError, "Can't save language resources. Check free space and folder permissions.", folder.string()};
        size_t copied = 0;
        for (const auto& file : files)
        {
            const auto* entry = source->Find(file.path);
            if (!entry) continue;
            if (entry->size != file.size)
                return {Error::CorruptFile, "Language resources failed verification. Use an unmodified copy of your disc.", entry->path};
            std::string error;
            auto reader = source->Open(*entry, error);
            std::vector<uint8_t> bytes(file.size);
            if (!reader || !reader->Read(0, bytes.data(), bytes.size(), error))
                return {Error::ReadError, "Can't read language resources. Check the disc image and try again.", entry->path};
            if (install::ToHex(install::Sha256Of(bytes.data(), bytes.size())) != file.sha)
                return {Error::CorruptFile, "Language resources failed verification. Use an unmodified copy of your disc.", entry->path};
            const auto destination = folder / file.sha;
            if (!Matches(destination, file))
            {
                // Each resource is content-addressed. A crash leaves no valid
                // name pointing at a partial file. Never overwrite an active pack.
                std::string temporary = destination.string() + ".tmp-XXXXXX";
                int fd = mkstemp(temporary.data());
                if (fd < 0) return {Error::WriteError, "Can't save language resources. Check free space and folder permissions."};
                size_t done = 0;
                while (done < bytes.size())
                {
                    ssize_t n = write(fd, bytes.data() + done, bytes.size() - done);
                    if (n < 0 && errno == EINTR) continue;
                    if (n <= 0) break;
                    done += size_t(n);
                }
                const bool flushed = done == bytes.size() && fsync(fd) == 0;
                close(fd);
                const bool moved = flushed && rename(temporary.c_str(), destination.c_str()) == 0;
                if (!moved)
                {
                    unlink(temporary.c_str());
                    return {Error::WriteError, "Can't save language resources. Check free space and folder permissions."};
                }
            }
            ++copied;
        }
        if (!copied) return {Error::MissingFile, "No supported language resources were found on this disc."};
        fprintf(stderr, "[language] verified %zu resources from %s\n", copied, sourcePath.filename().string().c_str());
        return {Error::None, "Language resources imported. Restart the game to use them."};
    }
    bool PrepareGame(Language requested, const std::filesystem::path& game, const std::filesystem::path& data)
    {
        active.clear();
        requested = Checked(int(requested));
        gameLanguage.store(Language::English);
        if (requested == Language::English) return true;
        const std::string suffix = suffixes[int(requested) - 1];
        const std::string main = "language/strings/main" + suffix + ".bin", cred = "language/strings/cred" + suffix + ".bin";
        std::map<std::string, std::filesystem::path, std::less<>> candidate;
        for (const auto& file : files)
        {
            const std::string_view path = file.path;
            if (path != main && path != cred && !path.starts_with("language/fonts/")) continue;
            auto original = OnDisc(game, file);
            auto imported = data / "languages" / file.sha;
            if (!original.empty() && Matches(original, file)) candidate.emplace(file.path, std::move(original));
            else if (Matches(imported, file)) candidate.emplace(file.path, std::move(imported));
        }
        bool ready = candidate.contains(main) && candidate.contains(cred);
        for (const char* font : {"big.bin", "bigdistressed.bin", "digital.bin", "small.bin"})
            ready = ready && candidate.contains(std::string("language/fonts/") + font);
        if (requested == Language::Japanese) ready = ready && candidate.contains("language/fonts/japan.bin");
        if (ready)
        {
            if (requested == Language::Japanese)
                for (const char* font : {"small.bin", "big.bin", "bigdistressed.bin"})
                    candidate[std::string("language/fonts/") + font] = candidate.at("language/fonts/japan.bin");
            active = std::move(candidate); gameLanguage.store(requested);
        }
        fprintf(stderr, "[language] requested %s, game %s (%s)\n", codes[int(requested)-1], ready ? codes[int(requested)-1] : "en", ready ? "resources verified" : "resources missing or invalid");
        return ready;
    }
    std::optional<std::filesystem::path> Resource(std::string_view relative)
    {
        if (active.empty()) return {};
        auto it = active.find(Lower(relative));
        return it == active.end() ? std::nullopt : std::optional(it->second);
    }
}
