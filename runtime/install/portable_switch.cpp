// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See portable_switch.h.
#include "portable_switch.h"
#include "installer.h"
#include "locate.h"
#include "placement.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <format>
#include <optional>
#include <system_error>

namespace install
{
    namespace
    {
        namespace fs = std::filesystem;

        Result Fail(Error error, std::string message, const fs::path& path = {})
        {
            Result r;
            r.error = error;
            r.message = std::move(message);
            r.file = path.string();
            return r;
        }

        // Not there, or a folder with nothing in it (hidden files count).
        bool EmptyOrMissing(const fs::path& dir)
        {
            std::error_code ec;
            fs::file_status status = fs::symlink_status(dir, ec);
            if (status.type() == fs::file_type::not_found)
                return true;
            if (ec || !fs::is_directory(status))
                return false;
            fs::directory_iterator it(dir, ec);
            return !ec && it == fs::directory_iterator();
        }

        // An unfinished copy from an earlier attempt goes (only one carrying
        // the tag: anything else of that name is the player's).
        Result ClearPartial(const fs::path& partial, std::vector<std::string>& said)
        {
            std::error_code ec;
            fs::file_status status = fs::symlink_status(partial, ec);
            if (status.type() == fs::file_type::not_found)
                return {};
            std::error_code tagEc;
            if (!fs::is_directory(status) || !fs::is_regular_file(fs::symlink_status(partial / kPortablePartialTag, tagEc)))
                return Fail(Error::DestinationNotEmpty, std::format("{} is in the way, and it isn't an unfinished copy of "
                    "BurnoutRevenge's. Move it or delete it, then try again.", partial.string()), partial);
            fs::remove_all(partial, ec);
            if (ec)
                return Fail(Error::WriteError, std::format("Couldn't delete {}, an unfinished copy from an earlier attempt ({}). "
                    "Delete it, then try again.", partial.string(), ec.message()), partial);
            said.push_back(std::format("Deleted {}, an unfinished copy from an earlier attempt.", partial.string()));
            return {};
        }

        Result StartPartial(const fs::path& partial)
        {
            std::error_code ec;
            fs::create_directories(partial.parent_path(), ec);
            fs::create_directory(partial, ec);
            if (!ec)
            {
                FILE* f = fopen((partial / kPortablePartialTag).string().c_str(), "wb");
                if (f && fputs("An unfinished copy of BurnoutRevenge's data (burnout-revenge.sh --portable).\n", f) >= 0 && fclose(f) == 0)
                    return {};
                if (f)
                    fclose(f);
                ec = std::error_code(errno ? errno : EIO, std::generic_category());
            }
            return Fail(Error::DestinationNotWritable, std::format("BurnoutRevenge can't write to {} ({}): nothing was copied.",
                partial.parent_path().string(), ec.message()), partial);
        }

        // The installed game a data folder's record and default point to, as
        // FindGameInstall picks it: the recorded one if it's usable, else the
        // default one.
        struct FoundGame
        {
            fs::path dir;                   // the game to copy (empty: none)
            std::optional<fs::path> lost;   // a recorded install that isn't usable (and no default one is)
            std::vector<fs::path> skipped;  // left out of the data copy: the game folders and their in-between folders
        };

        FoundGame FindGame(const fs::path& data, const portable::Layout& layout, const Manifest& manifest)
        {
            FoundGame found;
            fs::path fallback = data / "game";
            std::optional<fs::path> recorded = ReadInstallRecord(data / "game-location", layout);
            if (recorded && IsUsableInstall(*recorded, true, manifest))
                found.dir = *recorded;
            else if (IsUsableInstall(fallback, true, manifest))
                found.dir = fallback;
            else if (recorded)
                found.lost = recorded;
            for (const std::optional<fs::path>& game : { std::optional<fs::path>(fallback), recorded })
                if (game)
                {
                    fs::path dir = ResolvedFolder(*game);
                    found.skipped.push_back(dir);
                    found.skipped.push_back(fs::path(dir.string() + ".partial"));
                    found.skipped.push_back(fs::path(dir.string() + ".previous"));
                }
            return found;
        }

        struct CopyStats
        {
            uintmax_t files = 0, bytes = 0;
            std::vector<std::string> skipped;   // links and special files, not copied
        };

        // `from`'s contents into the folder `to` (made by the caller): folders
        // and files, with their modification times (the bug report and the log
        // rotation sort by them). Left out: the game's record at the top, and
        // the folders in `skip` (the game and its in-between folders).
        Result CopyData(const fs::path& from, const fs::path& to, const std::vector<fs::path>& skip, CopyStats& stats, bool top)
        {
            std::error_code ec;
            fs::directory_iterator it(from, ec);
            for (fs::directory_iterator end; !ec && it != end; it.increment(ec))
            {
                const fs::path source = it->path();
                const fs::path name = source.filename();
                if (top && (name == "game-location" || name == "game-location.tmp" || name == ".write-check"))
                    continue;
                if (std::any_of(skip.begin(), skip.end(), [&](const fs::path& s) { return IsAtOrInside(source, s); }))
                    continue;
                std::error_code typeEc;
                fs::file_status status = it->symlink_status(typeEc);
                const fs::path target = to / name;
                if (fs::is_symlink(status) || (!fs::is_directory(status) && !fs::is_regular_file(status)))
                {
                    stats.skipped.push_back(source.string());
                    continue;
                }
                std::error_code copyEc;
                if (fs::is_directory(status))
                {
                    fs::create_directory(target, copyEc);
                    if (copyEc)
                        return Fail(Error::WriteError, std::format("Couldn't make {} ({}): nothing was changed.", target.string(),
                            copyEc.message()), target);
                    if (Result r = CopyData(source, target, skip, stats, false); !r.Ok())
                        return r;
                }
                else
                {
                    fs::copy_file(source, target, fs::copy_options::none, copyEc);
                    if (copyEc)
                        return Fail(Error::WriteError, std::format("Couldn't copy {} to {} ({}): nothing was changed.",
                            source.string(), target.string(), copyEc.message()), source);
                    stats.files++;
                    stats.bytes += fs::file_size(source, typeEc);
                }
                std::error_code timeEc;
                fs::last_write_time(target, fs::last_write_time(source, timeEc), timeEc);
            }
            if (ec)
                return Fail(Error::ReadError, std::format("Couldn't read {} ({}): nothing was changed.", from.string(), ec.message()),
                    from);
            return {};
        }

        void SayCopied(const CopyStats& stats, const fs::path& from, const fs::path& to, std::vector<std::string>& said)
        {
            if (stats.files == 0)
                said.push_back(std::format("{} holds no settings, saves or logs: nothing of them to copy.", from.string()));
            else
                said.push_back(std::format("Copied settings, saves and logs ({} files, {}) from {} to {}.", stats.files,
                    FormatSize(stats.bytes), from.string(), to.string()));
            for (const std::string& path : stats.skipped)
                said.push_back(std::format("Not copied (a link or a special file): {}", path));
        }

        // A name for the old folder that isn't taken: <folder>.before-portable-off-<UTC date>-<time>[-N].
        fs::path AsideName(const fs::path& folder)
        {
            std::string stamp = std::format("{:%Y%m%d-%H%M%S}",
                std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
            fs::path aside = folder.string() + ".before-portable-off-" + stamp;
            std::error_code ec;
            for (int n = 2; fs::exists(fs::symlink_status(aside, ec)); n++)
                aside = folder.string() + ".before-portable-off-" + stamp + "-" + std::to_string(n);
            return aside;
        }

        bool WriteMarker(const fs::path& file)
        {
            fs::path temp = file;
            temp += ".tmp";
            FILE* f = fopen(temp.string().c_str(), "wb");
            if (!f)
                return false;
            bool ok = fputs("BurnoutRevenge portable mode: with this file here, BurnoutRevenge keeps its settings, saves, logs and "
                "caches in this folder (data/ and cache/) and installs the game into data/game. Delete this file to use this "
                "computer's own data folder again (README.md, \"Portable mode\").\n", f) >= 0;
            ok = fclose(f) == 0 && ok;
            std::error_code ec;
            if (ok)
                fs::rename(temp, file, ec);
            else
                fs::remove(temp, ec);
            return ok && !ec;
        }
    }

    Result MakePortable(const std::filesystem::path& root, const std::filesystem::path& home, const GameCopier& copyGame,
        std::vector<std::string>& said, const Manifest& manifest)
    {
        if (root.empty() || !root.is_absolute())
            return Fail(Error::NotFound, "BurnoutRevenge can't tell which folder it runs from, so it can't make that folder "
                "portable. Start it with the burnout-revenge.sh in that folder.");
        const fs::path folder = ResolvedFolder(root);
        const fs::path data = folder / portable::kDataFolder, marker = folder / portable::kMarker;
        std::error_code ec;
        if (fs::exists(fs::symlink_status(marker, ec)))
            return Fail(Error::DestinationNotEmpty, std::format("Portable mode is already on ({} is there): nothing was copied.",
                marker.string()), marker);
        if (!EmptyOrMissing(data))
            return Fail(Error::DestinationNotEmpty, std::format("{} already holds files (from an earlier portable setup?), so "
                "nothing was copied. To use them as they are, create {} yourself. To copy this computer's data instead, move "
                "{} somewhere else first.", data.string(), marker.string(), data.string()), data);

        const fs::path source = ResolvedFolder(home);
        FoundGame game = FindGame(source, portable::Layout{}, manifest);
        if (game.dir.empty() && game.lost)
            return Fail(Error::NotFound, std::format("The game is installed in {}, which isn't there now or isn't a current "
                "install (a drive not connected?), so nothing was copied. Connect it and try again. If it's gone for good, "
                "delete {} and try again: BurnoutRevenge then installs the game into {} at its first start.",
                game.lost->string(), (source / "game-location").string(), (data / "game").string()), *game.lost);

        const fs::path partial = folder / (std::string(portable::kDataFolder) + ".partial");
        if (Result r = ClearPartial(partial, said); !r.Ok())
            return r;
        if (Result r = StartPartial(partial); !r.Ok())
            return r;
        auto abandon = [&](Result r) {
            std::error_code removeEc;
            fs::remove_all(partial, removeEc);
            return r;
        };

        CopyStats stats;
        if (fs::is_directory(source, ec))
            if (Result r = CopyData(source, partial, game.skipped, stats, true); !r.Ok())
                return abandon(r);
        if (!game.dir.empty())
            if (Result r = copyGame(game.dir, partial / "game"); !r.Ok())
                return abandon(r);

        fs::remove(partial / kPortablePartialTag, ec);
        fs::remove(data, ec);  // an empty one (EmptyOrMissing): replaced
        ec.clear();
        fs::rename(partial, data, ec);
        if (ec)
            return abandon(Fail(Error::WriteError, std::format("Couldn't rename {} to {} ({}): nothing was changed.",
                partial.string(), data.string(), ec.message()), data));
        if (!WriteMarker(marker))
            return Fail(Error::WriteError, std::format("The data was copied to {}, but {} couldn't be written. Create it "
                "yourself (an empty file will do) to turn portable mode on.", data.string(), marker.string()), marker);

        SayCopied(stats, source, data, said);
        if (!game.dir.empty())
            said.push_back(std::format("Copied the game to {} and checked every file.", (data / "game").string()));
        else
            said.push_back(std::format("No installed game was found, so none was copied: BurnoutRevenge installs it into {} "
                "at its first start.", (data / "game").string()));
        said.push_back(std::format("Portable mode is on ({}): BurnoutRevenge keeps everything in {} now. Move or copy that "
            "whole folder to take it all along.", marker.string(), folder.string()));
        if (stats.files || !game.dir.empty())
            said.push_back(std::format("Nothing was removed: the old copies are still in {}{}. Delete them once you're happy "
                "with the portable folder, or keep them as a backup.", source.string(),
                game.dir.empty() || IsAtOrInside(ResolvedFolder(game.dir), source) ? ""
                    : std::format(" (and the game in {})", game.dir.string())));
        return {};
    }

    Result LeavePortable(const portable::Layout& layout, const std::filesystem::path& home, const GameCopier& copyGame,
        std::vector<std::string>& said, const Manifest& manifest)
    {
        if (!layout.Active())
            return Fail(Error::NotFound, std::format("Portable mode isn't on (there's no {} in BurnoutRevenge's folder): nothing "
                "to do.", portable::kMarker));
        const fs::path target = ResolvedFolder(home);
        const fs::path marker = layout.root / portable::kMarker;
        if (IsAtOrInside(target, layout.root))
            return Fail(Error::DestinationNotWritable, std::format("This computer's data folder, {}, is inside BurnoutRevenge's "
                "folder ({}): nothing was copied. Is XDG_DATA_HOME set to a folder in it?", target.string(),
                layout.root.string()), target);

        FoundGame game = FindGame(layout.data, layout, manifest);
        const bool copyTheGame = !game.dir.empty() && IsAtOrInside(ResolvedFolder(game.dir), layout.root);

        const fs::path partial = fs::path(target.string() + ".partial");
        if (Result r = ClearPartial(partial, said); !r.Ok())
            return r;
        if (Result r = StartPartial(partial); !r.Ok())
            return r;
        auto abandon = [&](Result r) {
            std::error_code removeEc;
            fs::remove_all(partial, removeEc);
            return r;
        };

        CopyStats stats;
        std::error_code ec;
        if (fs::is_directory(layout.data, ec))
            if (Result r = CopyData(layout.data, partial, game.skipped, stats, true); !r.Ok())
                return abandon(r);
        if (copyTheGame)
        {
            if (Result r = copyGame(game.dir, partial / "game"); !r.Ok())
                return abandon(r);
        }
        else if (!game.dir.empty() && !WriteInstallRecord(partial / "game-location", portable::RecordText(game.dir, {})))
            return abandon(Fail(Error::WriteError, std::format("Couldn't write {}: nothing was changed.",
                (partial / "game-location").string()), partial));
        fs::remove(partial / kPortablePartialTag, ec);

        // The data folder there now (usually the one from before portable
        // mode) is kept, renamed aside; an empty one is replaced.
        std::optional<fs::path> aside;
        if (fs::exists(fs::symlink_status(target, ec)))
        {
            if (EmptyOrMissing(target))
                fs::remove(target, ec);
            else
            {
                aside = AsideName(target);
                fs::rename(target, *aside, ec);
                if (ec)
                    return abandon(Fail(Error::WriteError, std::format("Couldn't rename {} to {} ({}): nothing was changed.",
                        target.string(), aside->string(), ec.message()), target));
            }
        }
        ec.clear();
        fs::rename(partial, target, ec);
        if (ec)
        {
            Result r = Fail(Error::WriteError, std::format("Couldn't rename {} to {} ({}): nothing was changed.", partial.string(),
                target.string(), ec.message()), target);
            std::error_code backEc;
            if (aside)
                fs::rename(*aside, target, backEc);
            return abandon(r);
        }
        fs::remove(marker, ec);
        if (ec)
            return Fail(Error::WriteError, std::format("The data was copied to {}, but {} couldn't be removed ({}). Delete it "
                "yourself to turn portable mode off.", target.string(), marker.string(), ec.message()), marker);

        if (aside)
            said.push_back(std::format("Renamed the data folder that was already there, {}, to {}: kept as it was. Delete it "
                "once you're happy.", target.string(), aside->string()));
        SayCopied(stats, layout.data, target, said);
        if (copyTheGame)
            said.push_back(std::format("Copied the game to {} and checked every file.", (target / "game").string()));
        else if (!game.dir.empty())
            said.push_back(std::format("The game stays where it is, {} (outside BurnoutRevenge's folder), and is recorded there.",
                game.dir.string()));
        else
            said.push_back("No installed game was found, so none was copied: BurnoutRevenge installs it at its first start.");
        said.push_back(std::format("Portable mode is off ({} removed): BurnoutRevenge uses {} again. Nothing in {} was removed: "
            "delete it once you're happy, or keep it as a backup.", marker.string(), target.string(), layout.root.string()));
        return {};
    }
}
