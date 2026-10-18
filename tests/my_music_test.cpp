// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// The player's own music (user/my_music.h): the folder scan takes music files
// in subfolders and skips hidden ones, and the shuffle plays every file once
// a round, never one twice in a row.
//   my_music_test [folder]   (a new folder inside it is used, then deleted)
#include <user/my_music.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace fs = std::filesystem;
namespace mm = user::my_music;

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

static void Touch(const fs::path& file)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file) << "x";
}

static bool IsMusic(const fs::path& path)
{
    auto ext = path.extension().string();
    return ext == ".mp3" || ext == ".m4a";
}

int main(int argc, char** argv)
{
    fs::path root = (argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path()) / "my_music_test";
    fs::remove_all(root);

    CHECK(mm::Scan(root / "missing", IsMusic).empty(), "a missing folder has no music");

    Touch(root / "b.mp3");
    Touch(root / "a.m4a");
    Touch(root / "album" / "c.mp3");
    Touch(root / "notes.txt");
    Touch(root / ".hidden.mp3");
    Touch(root / ".trash" / "d.mp3");
    auto files = mm::Scan(root, IsMusic);
    CHECK(files.size() == 3, "3 music files, got %zu", files.size());
    if (files.size() == 3)
    {
        CHECK(files[0].filename() == "a.m4a", "sorted: %s", files[0].string().c_str());
        CHECK(files[1].filename() == "c.mp3", "the subfolder's by its path: %s", files[1].string().c_str());
    }

    mm::Shuffle empty(1);
    CHECK(empty.Next().empty(), "no files: nothing");

    for (uint64_t seed = 0; seed < 200; seed++)
    {
        mm::Shuffle shuffle(seed);
        shuffle.SetFiles(files);
        fs::path last;
        for (int round = 0; round < 4; round++)
        {
            std::set<fs::path> seen;
            for (size_t i = 0; i < files.size(); i++)
            {
                fs::path next = shuffle.Next();
                CHECK(next != last, "seed %llu: %s twice in a row", (unsigned long long)seed, next.string().c_str());
                seen.insert(next);
                last = next;
            }
            CHECK(seen.size() == files.size(), "seed %llu round %d: every file once", (unsigned long long)seed, round);
        }
    }

    mm::Shuffle one(7);
    one.SetFiles({ files[0] });
    CHECK(one.Next() == files[0] && one.Next() == files[0], "one file repeats");

    CHECK(mm::DisplayName("x/Song.mp3", "Title", "Artist") == "Artist - Title", "artist and title");
    CHECK(mm::DisplayName("x/Song.mp3", "Title", "") == "Title", "title only");
    CHECK(mm::DisplayName("x/Song.mp3", "", "Artist") == "Song", "no title: the file's name");

    fs::remove_all(root);
    if (s_failures == 0)
        std::printf("my_music_test: ok\n");
    return s_failures == 0 ? 0 : 1;
}
