// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Save imports (user/save_import.h, user/stfs.h) on synthetic packages and a
// scratch folder; no game data. A console's CON with two copies of each hash
// table (the second in use), a chained file and a folder, a package with a
// second level of tables, damaged and foreign packages, Xenia's layout, the
// progress file alone, and an import applied by the next launch's restore.
//   save_import_test [folder]   (a new folder inside it is used, then deleted)
#include <user/save_import.h>
#include <user/save_backup.h>

#include <platform/sha1.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
namespace si = user::save_import;
namespace sb = user::save_backup;
using Bytes = std::vector<uint8_t>;

static int s_failures = 0;
#define CHECK(cond, ...)                                       \
    do                                                         \
    {                                                          \
        if (!(cond))                                           \
        {                                                      \
            std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            std::fprintf(stderr, __VA_ARGS__);                 \
            std::fprintf(stderr, "\n");                        \
            s_failures++;                                      \
        }                                                      \
    } while (0)

static void Be(Bytes& b, size_t at, uint32_t v, size_t n) { while (n) { b[at + --n] = uint8_t(v); v >>= 8; } }
static void Le(Bytes& b, size_t at, uint32_t v, size_t n) { for (size_t i = 0; i < n; i++) b[at + i] = uint8_t(v >> (8 * i)); }
static std::array<uint8_t, 20> Sha1(const uint8_t* p)
{
    return platform::Sha1(std::span<const uint8_t>(p, 4096));
}

struct FileSpec { std::string name; Bytes bytes; bool consecutive = true; int parent = -1; bool folder = false; };

// An STFS package as a console writes one: `k` copies of each table (2: the
// second in use, the first garbage), at least `minBlocks` blocks.
static Bytes Package(const std::vector<FileSpec>& files, uint32_t k = 2, uint32_t minBlocks = 0, uint32_t title = 0x454107DC,
    uint32_t type = 1)
{
    // Block 0: the file table; then each file's blocks (a chained file's in
    // reverse, so following the chain is the only way to read it in order).
    std::vector<Bytes> blocks(1, Bytes(4096));
    std::vector<uint32_t> next(1, 0xFFFFFF);
    struct Placed { uint32_t first, count; };
    std::vector<Placed> placed;
    for (const FileSpec& f : files)
    {
        const uint32_t count = uint32_t((f.bytes.size() + 4095) / 4096), base = uint32_t(blocks.size());
        std::vector<uint32_t> order;
        for (uint32_t i = 0; i < count; i++)
            order.push_back(f.consecutive ? base + i : base + count - 1 - i);
        blocks.resize(base + count, Bytes(4096));
        next.resize(base + count, 0xFFFFFF);
        for (uint32_t i = 0; i < count; i++)
        {
            std::copy(f.bytes.begin() + i * 4096, f.bytes.begin() + std::min<size_t>(f.bytes.size(), (i + 1) * 4096), blocks[order[i]].begin());
            next[order[i]] = i + 1 < count ? order[i + 1] : 0xFFFFFF;
        }
        placed.push_back({ count ? order[0] : 0, count });
    }
    while (blocks.size() < minBlocks)
    {
        blocks.emplace_back(4096, uint8_t(blocks.size()));
        next.push_back(0xFFFFFF);
    }
    for (size_t i = 0; i < files.size(); i++)
    {
        Bytes& t = blocks[0];
        const size_t r = i * 64;
        const FileSpec& f = files[i];
        std::memcpy(t.data() + r, f.name.data(), f.name.size());
        t[r + 40] = uint8_t(f.name.size() | (f.consecutive ? 0x40 : 0) | (f.folder ? 0x80 : 0));
        Le(t, r + 41, placed[i].count, 3);
        Le(t, r + 44, placed[i].count, 3);
        Le(t, r + 47, placed[i].first, 3);
        Be(t, r + 50, f.parent < 0 ? 0xFFFF : uint32_t(f.parent), 2);
        Be(t, r + 52, uint32_t(f.bytes.size()), 4);
    }

    const uint32_t allocated = uint32_t(blocks.size());
    const int levels = allocated <= 170 ? 0 : 1;
    auto backing = [&](uint64_t n) { return n + k * (n / 170 + 1) + (levels ? k * (n / 28900 + 1) : 0); };
    const size_t start = 0xA000;
    Bytes b(start + (backing(allocated - 1) + 1) * 4096);
    auto at = [&](uint64_t backingBlock) { return start + backingBlock * 4096; };
    for (uint32_t n = 0; n < allocated; n++)
        std::copy(blocks[n].begin(), blocks[n].end(), b.begin() + at(backing(n)));
    const uint32_t active = k - 1;
    std::vector<size_t> level0;
    for (uint32_t g = 0; g * 170 < allocated; g++)
    {
        const uint64_t first = backing(uint64_t(g) * 170) - k;
        if (k == 2)
            std::fill(b.begin() + at(first), b.begin() + at(first) + 4096, uint8_t(0x5A));  // the stale copy
        const size_t table = at(first + active);
        for (uint32_t n = g * 170; n < std::min(allocated, (g + 1) * 170); n++)
        {
            const auto d = Sha1(b.data() + at(backing(n)));
            const size_t e = table + (n % 170) * 24;
            std::copy(d.begin(), d.end(), b.begin() + e);
            b[e + 20] = 0x80;
            Be(b, e + 21, next[n], 3);
        }
        level0.push_back(table);
    }
    size_t top = level0[0];
    if (levels)
    {
        const uint64_t first = backing(0) - 2 * k;
        top = at(first + active);
        for (size_t g = 0; g < level0.size(); g++)
        {
            const auto d = Sha1(b.data() + level0[g]);
            std::copy(d.begin(), d.end(), b.begin() + top + g * 24);
        }
    }
    std::memcpy(b.data(), "CON ", 4);
    Be(b, 0x340, 0x971A, 4);
    Be(b, 0x344, type, 4);
    Be(b, 0x360, title, 4);
    const char16_t display[] = u"Burnout Revenge";
    for (size_t i = 0; display[i]; i++)
        Be(b, 0x411 + i * 2, display[i], 2);
    Be(b, 0x1712, 4, 4);
    std::memcpy(b.data() + 0x171A, "\x89PNG", 4);
    b[0x379] = 0x24;
    b[0x37B] = k == 1 ? 1 : 0;
    Le(b, 0x37C, 1, 2);
    Le(b, 0x37E, 0, 3);
    const auto d = Sha1(b.data() + top);
    std::copy(d.begin(), d.end(), b.begin() + 0x381);
    Be(b, 0x395, allocated, 4);
    return b;
}

static Bytes Pattern(size_t size, uint8_t seed)
{
    Bytes b(size);
    for (size_t i = 0; i < size; i++)
        b[i] = uint8_t(i * 7 + seed + (i >> 12));
    return b;
}

static void Put(const fs::path& path, const Bytes& b)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

static Bytes Get(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv)
{
    const fs::path root = fs::path(argc > 1 ? argv[1] : fs::temp_directory_path().string()) / "save_import_test";
    fs::remove_all(root);
    fs::create_directories(root);
    std::string error;

    // A console's CON: two copies of the table, the second in use; the
    // file renamed off the console, its contents' name kept.
    const Bytes progress = Pattern(21316, 3);
    Put(root / "console" / "E0000123456789AB", Package({ { "Player Files", progress } }));
    auto found = si::Find(root / "console" / "E0000123456789AB", error);
    CHECK(found.size() == 1 && found[0].name == "Player Files" && found[0].files.size() == 1 && found[0].files[0].bytes == progress,
        "CON package: %s", error.c_str());
    CHECK(!found.empty() && found[0].displayName == u"Burnout Revenge" && found[0].thumbnail.size() == 4, "display name and thumbnail");

    // A chained file in a folder, and a read-only package (one copy).
    const Bytes clip = Pattern(3 * 4096 + 100, 9), note = Pattern(10, 1);
    for (uint32_t k : { 1u, 2u })
    {
        const Bytes p = Package({ { "clips", {}, true, -1, true }, { "clip.bin", clip, false, 0 }, { "note", note } }, k);
        auto parsed = user::stfs::Parse(p, [](std::span<const uint8_t> s) { return Sha1(s.data()); }, error);
        CHECK(parsed && parsed->files.size() == 2 && parsed->files[0].path == "clips/clip.bin" && parsed->files[0].bytes == clip &&
            parsed->files[1].bytes == note, "chained file in a folder (k=%u): %s", k, error.c_str());
    }

    // A second level of hash tables (more than 170 blocks).
    const Bytes big = Pattern(200 * 4096 + 5, 4);
    Put(root / "big" / "Save And Share 0", Package({ { "Save And Share 0", big } }));
    found = si::Find(root / "big" / "Save And Share 0", error);
    CHECK(found.size() == 1 && found[0].files[0].bytes == big, "two levels: %s", error.c_str());

    // Damaged, another game's, not a save.
    Bytes bad = Package({ { "Player Files", progress } });
    bad[0xA000 + 3 * 4096 + 17] ^= 1;  // the file's first block (after two tables and the file table)
    Put(root / "bad" / "damaged", bad);
    CHECK(si::Find(root / "bad" / "damaged", error).empty() && error.find("checksum") != std::string::npos, "damaged: %s", error.c_str());
    Put(root / "bad" / "other", Package({ { "Player Files", progress } }, 2, 0, 0x45410950));
    CHECK(si::Find(root / "bad" / "other", error).empty() && error.find("another game") != std::string::npos, "foreign: %s", error.c_str());
    Put(root / "bad" / "profile", Package({ { "Account", progress } }, 2, 0, 0x454107DC, 0x10000));
    CHECK(si::Find(root / "bad" / "profile", error).empty() && error.find("isn't a save") != std::string::npos, "not a save: %s", error.c_str());
    Put(root / "bad" / "notes.txt", Bytes{ 'h', 'i' });
    CHECK(si::Find(root / "bad" / "notes.txt", error).empty() && !error.empty(), "a text file");

    // Xenia's content folder, beside a console package; Headers skipped.
    const fs::path xenia = root / "xenia" / "content" / "0000000000000000" / "454107DC";
    Put(xenia / "00000001" / "Player Files" / "Player Files", progress);
    Put(xenia / "Headers" / "00000001" / "Player Files.header", Bytes(0x100));
    Put(root / "xenia" / "usb" / "Content" / "E000" / "454107DC" / "00000001" / "ABCDEF", Package({ { "Save And Share 1", note } }));
    found = si::Find(root / "xenia", error);
    CHECK(found.size() == 2 && si::Describe(found) == "Save And Share 1, Player Files", "folder: %s (%s)", si::Describe(found).c_str(),
        error.c_str());

    // The progress file alone.
    Put(root / "raw" / "Player Files", progress);
    found = si::Find(root / "raw" / "Player Files", error);
    CHECK(found.size() == 1 && found[0].name == "Player Files" && found[0].files[0].path == "Player Files", "raw file: %s", error.c_str());

    // An import: the current clip stays, the progress is replaced, the next
    // launch applies it after backing the current saves up.
    const fs::path save = root / "data" / "save", backups = root / "data" / "backups";
    const Bytes old = Pattern(21316, 99), oldClip = Pattern(500, 5);
    Put(save / "Player Files" / "Player Files", old);
    Put(save / "Save And Share 0" / "Save And Share 0", oldClip);
    Put(save / "Save And Share 0.xcontent", Bytes(0x134));
    std::string name;
    CHECK(si::Request(root / "console" / "E0000123456789AB", save, backups, found, name, error) && name.ends_with("-imported"),
        "request: %s", error.c_str());
    CHECK(sb::PendingRestore(backups) == name, "pending");
    CHECK(Get(save / "Player Files" / "Player Files") == old, "nothing changes before the launch");
    sb::OnLaunch(save, backups);
    CHECK(Get(save / "Player Files" / "Player Files") == progress, "imported");
    CHECK(Get(save / "Save And Share 0" / "Save And Share 0") == oldClip, "other saves kept");
    const Bytes meta = Get(save / "Player Files.xcontent");
    CHECK(meta.size() == 0x134 && meta[3] == 1 && meta[7] == 1 && std::string(reinterpret_cast<const char*>(meta.data() + 0x108)) == "Player Files" &&
        meta[9] == 'B', "xcontent");
    CHECK(Get(save / "Player Files.thumbnail.png").size() == 4, "thumbnail");
    bool before = false, imported = false;
    for (const sb::Backup& b : sb::List(backups))
    {
        before |= b.beforeRestore && Get(b.path / "Player Files" / "Player Files") == old;
        imported |= b.imported;
    }
    CHECK(before && imported && sb::PendingRestore(backups).empty(), "backups after the import");

    fs::remove_all(root);
    if (s_failures)
        std::fprintf(stderr, "%d check(s) failed\n", s_failures);
    else
        std::printf("save_import_test: all checks passed\n");
    return s_failures ? 1 : 0;
}
