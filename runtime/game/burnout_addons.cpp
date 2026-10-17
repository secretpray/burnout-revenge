// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include <stdafx.h>
#include "burnout_addons.h"
#include "burnout_addon_package.h"
#include <kernel/xam.h>
#include <user/paths.h>
#include <user/settings.h>
#include <install/sha256.h>
#include <SDL3/SDL.h>
#include <CommonCrypto/CommonDigest.h>
#include <fstream>
#include <set>

namespace game
{
    namespace
    {
        namespace fs = std::filesystem;
        std::vector<BurnoutAddon> s_addons;
        bool Verify(std::span<const uint8_t> bytes, std::span<const uint8_t> expected)
        {
            uint8_t digest[CC_SHA1_DIGEST_LENGTH];
            CC_SHA1(bytes.data(), CC_LONG(bytes.size()), digest);
            return std::equal(expected.begin(), expected.end(), digest);
        }
        struct Loaded { addons::Package package; std::vector<uint8_t> bytes; std::string id; };
        std::optional<Loaded> Read(const fs::path& path, std::string& error)
        {
            std::string id = path.filename().string();
            if (id.empty() || id.size() > XCONTENT_MAX_FILENAME ||
                !std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isxdigit(c); }))
            { error = "Keep the original hexadecimal Xbox package filename"; return {}; }
            std::error_code ec;
            uint64_t size = fs::file_size(path, ec);
            if (ec || size > addons::kMaxPackageSize) { error = "Unreadable or oversized car pack"; return {}; }
            std::vector<uint8_t> bytes(size);
            std::ifstream f(path, std::ios::binary);
            if (!f.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) { error = "Can't read car pack"; return {}; }
            auto package = addons::Parse(bytes, Verify, error);
            if (!package) return {};
            return Loaded{ std::move(*package), std::move(bytes), std::move(id) };
        }
        std::vector<fs::path> Candidates(const fs::path& source)
        {
            std::set<fs::path> files;
            std::error_code ec;
            if (fs::is_regular_file(source, ec)) return {source};
            for (const fs::path& dir : {source, source / "00000002", source / "454107DC/00000002"})
                for (const auto& entry : fs::directory_iterator(dir, ec))
                {
                    std::string name = entry.path().filename().string();
                    if (!name.empty() && name[0] != '.' && !entry.is_symlink(ec) && entry.is_regular_file(ec))
                        files.insert(entry.path());
                    if (files.size() >= 128) return {files.begin(), files.end()};
                }
            return {files.begin(), files.end()};
        }
        bool Write(const fs::path& path, std::span<const uint8_t> bytes)
        {
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            if (ec || fs::is_symlink(path, ec)) return false;
            fs::path temp = path; temp += ".partial";
            if (fs::is_symlink(temp, ec)) return false;
            std::ofstream f(temp, std::ios::binary | std::ios::trunc);
            if (!f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size())) return false;
            f.close(); if (!f) return false;
            fs::rename(temp, path, ec);
            return !ec;
        }
        std::string Name(const std::u16string& text)
        {
            char* converted = SDL_iconv_string("UTF-8", "UTF-16LE", reinterpret_cast<const char*>(text.c_str()), (text.size()+1)*2);
            std::string result = converted ? converted : "Unnamed car pack";
            SDL_free(converted);
            return result;
        }
    }
    fs::path BurnoutAddonFolder() { return GetUserPath() / "addons/454107DC/00000002"; }
    const std::vector<BurnoutAddon>& BurnoutAddons() { return s_addons; }
    std::string ImportBurnoutAddons(const fs::path& source)
    {
        size_t copied = 0, rejected = 0; std::string lastError;
        for (const fs::path& path : Candidates(source))
        {
            auto loaded = Read(path, lastError);
            if (!loaded) { rejected++; fprintf(stderr, "[addons] rejected %s: %s\n", path.c_str(), lastError.c_str()); continue; }
            fs::path dest = BurnoutAddonFolder() / loaded->id;
            std::error_code ec;
            if (fs::equivalent(path, dest, ec)) continue;
            if (Write(dest, loaded->bytes)) copied++;
            else { rejected++; lastError = "Can't write the Add-on folder"; }
        }
        return std::format("{} pack(s) copied; {} skipped. {}", copied, rejected,
            rejected ? lastError : "Restart Burnout Revenge to connect newly copied packs.");
    }
    void InitializeBurnoutAddons()
    {
        // A bounded search of Xbox content folders, never a recursive drive scan.
        std::error_code ec;
        if (settings::GetBool(settings::Id::AutoImportAddons))
        {
            std::vector<fs::path> sources{ GetGamePath() / "454107DC", GetGamePath().parent_path() / "454107DC" };
            if (const char* custom = std::getenv("BURNOUT_ADDON_DIR"); custom && *custom) sources.emplace_back(custom);
            for (const auto& volume : fs::directory_iterator("/Volumes", ec))
                sources.push_back(volume.path() / "XBOX360/454107DC");
            for (const auto& source : sources)
                if (fs::exists(source, ec))
                    fprintf(stderr, "[addons] import %s: %s\n", source.c_str(), ImportBurnoutAddons(source).c_str());
        }
        else
            fprintf(stderr, "[addons] automatic external import disabled; loading installed packs only\n");
        for (const fs::path& path : Candidates(BurnoutAddonFolder()))
        {
            std::string error;
            auto loaded = Read(path, error);
            if (!loaded) { fprintf(stderr, "[addons] invalid installed pack %s: %s\n", path.c_str(), error.c_str()); continue; }
            std::string digest = install::ToHex(install::Sha256Of(loaded->bytes.data(), loaded->bytes.size()));
            fs::path root = GetUserPath() / "addons/mounted" / loaded->id / digest;
            bool written = true;
            for (const auto& file : loaded->package.files)
                written = Write(root / file.name, file.bytes) && written;
            if (!written) { fprintf(stderr, "[addons] can't extract %s\n", path.c_str()); continue; }
            XCONTENT_DATA content{1, XCONTENTTYPE_DLC};
            memcpy(content.szFileName, loaded->id.data(), loaded->id.size());
            for (size_t i = 0; i < loaded->package.displayName.size() && i < XCONTENT_MAX_DISPLAYNAME-1; i++)
                content.szDisplayName[i] = uint16_t(loaded->package.displayName[i]);
            XamRegisterContent(content, root.string());
            s_addons.push_back({loaded->id, Name(loaded->package.displayName)});
            fprintf(stderr, "[addons] connected %s: %s (%zu files)\n", loaded->id.c_str(), s_addons.back().name.c_str(), loaded->package.files.size());
        }
        fprintf(stderr, "[addons] %zu Burnout Revenge pack(s) connected\n", s_addons.size());
    }
}
