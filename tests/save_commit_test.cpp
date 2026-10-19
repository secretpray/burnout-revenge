// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Saves written through a staging copy (user/save_commit.h), on a scratch
// folder: the old save stays whole until the commit, which replaces it in
// one step; leftovers of a killed process are thrown away.
#include <user/save_commit.h>

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

namespace
{
    std::string Read(const fs::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }

    void WriteText(const fs::path& p, const std::string& text)
    {
        std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
    }

    void Put(int fd, off_t at, const std::string& text)
    {
        assert(pwrite(fd, text.data(), text.size(), at) == ssize_t(text.size()));
    }
}

int main(int argc, char** argv)
{
    const fs::path scratch = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "save_commit_test";
    fs::remove_all(scratch);
    const fs::path saveRoot = scratch / "save";
    const fs::path save = saveRoot / "Player Files" / "Player Files";
    fs::create_directories(save.parent_path());
    WriteText(save, "old progress");

    // Only files inside the save folder are staged; never twice the same copy.
    assert(!savecommit::StagingFor(scratch / "game" / "data.bin", saveRoot));
    assert(!savecommit::StagingFor(saveRoot, saveRoot));
    assert(!savecommit::StagingFor(scratch / "saves" / "x", saveRoot));
    auto a = savecommit::StagingFor(save, saveRoot), b = savecommit::StagingFor(save, saveRoot);
    assert(a && b && *a != *b);
    assert(a->parent_path() == scratch / "save-incoming");
    assert(savecommit::StagingFor(save, scratch / "save/"));

    std::string error;
    {
        // The game empties the save and writes it again: until the commit,
        // the save on disk is the old one, whole.
        int fd = savecommit::Begin(*a, save, false, error);
        assert(fd >= 0 && error.empty());
        Put(fd, 0, "new ");
        assert(Read(save) == "old progress");
        Put(fd, 4, "progress!");
        assert(Read(save) == "old progress");
        assert(savecommit::Commit(fd, *a, save).empty());
        assert(Read(save) == "new progress!");
        assert(!fs::exists(*a));
    }
    {
        // Opened without emptying: the copy starts as the save is.
        int fd = savecommit::Begin(*b, save, true, error);
        assert(fd >= 0);
        Put(fd, 0, "NEW");
        assert(savecommit::Commit(fd, *b, save).empty());
        assert(Read(save) == "NEW progress!");
    }
    {
        // A new file appears only at the commit.
        const fs::path fresh = saveRoot / "Player Files" / "Clip 1";
        auto c = savecommit::StagingFor(fresh, saveRoot);
        int fd = savecommit::Begin(*c, fresh, true, error);
        assert(fd >= 0 && !fs::exists(fresh));
        Put(fd, 0, "clip");
        assert(savecommit::Commit(fd, *c, fresh).empty());
        assert(Read(fresh) == "clip");
    }
    {
        // Killed while writing: no commit. The save is the last whole one,
        // and the next start throws the copy away.
        auto d = savecommit::StagingFor(save, saveRoot);
        int fd = savecommit::Begin(*d, save, false, error);
        Put(fd, 0, "half");
        close(fd);
        assert(Read(save) == "NEW progress!");
        assert(savecommit::RemoveLeftovers(saveRoot) == 1);
        assert(!fs::exists(*d));
        assert(savecommit::RemoveLeftovers(saveRoot) == 0);
    }
    {
        // A commit that can't replace the save (its folder is gone) leaves
        // nothing behind and says why.
        const fs::path lost = saveRoot / "Gone" / "file";
        auto e = savecommit::StagingFor(lost, saveRoot);
        int fd = savecommit::Begin(*e, lost, false, error);
        Put(fd, 0, "x");
        assert(!savecommit::Commit(fd, *e, lost).empty());
        assert(!fs::exists(*e) && !fs::exists(lost));
    }
    fs::remove_all(scratch);
    printf("save_commit_test: ok\n");
    return 0;
}
