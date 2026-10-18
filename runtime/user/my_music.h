// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The player's own music (Audio -> Music: My Music): the files in a folder
// of the user data (GetUserPath() / "Music", subfolders included), played in
// place of EA Trax (kernel/xmsg.cpp) in a shuffled round: every file once,
// then a new order, never the same file twice in a row. The files are
// decoded by the system (platform/audio_file.h).
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace user::my_music
{
    // GetUserPath() / "Music" (not created).
    std::filesystem::path Folder();

    // The files under `folder` that `isMusic` takes, sorted by path; hidden
    // files and folders (a leading dot) are skipped. Empty when the folder
    // is missing or unreadable.
    std::vector<std::filesystem::path> Scan(const std::filesystem::path& folder,
        const std::function<bool(const std::filesystem::path&)>& isMusic);

    // A shuffled round of `files`. Not thread-safe.
    class Shuffle
    {
    public:
        explicit Shuffle(uint64_t seed) : m_random(seed) {}
        // A new list: the round starts again (kept when the list is the same).
        void SetFiles(std::vector<std::filesystem::path> files);
        // The next file; empty when there are none.
        std::filesystem::path Next();
        size_t Size() const { return m_files.size(); }

    private:
        std::vector<std::filesystem::path> m_files;
        std::vector<size_t> m_order;
        size_t m_position = 0;
        std::filesystem::path m_last;
        std::mt19937_64 m_random;
    };

    // "Artist - Title" from the tags, or what of it there is, or the file's
    // name without its extension.
    std::string DisplayName(const std::filesystem::path& file, const std::string& title, const std::string& artist);

    // What the XMP player (kernel/xmsg.cpp) asks of My Music; the game's in
    // user/my_music_player.cpp (settings, the system decoder, the toast),
    // tests/kernel_compat_test.cpp has its own. Any thread.
    // Music is set to My Music.
    bool Chosen();
    struct Pick
    {
        std::filesystem::path file;  // empty: the folder has no music files
        std::string name;            // DisplayName from its tags
        size_t count = 0;            // files in the folder
    };
    // The folder scanned again and the next file of its shuffled round.
    Pick Next();
    // Tells the player (a toast).
    void Announce(const std::string& text);
    // `onChange` runs after Music changes, on the thread that changed it.
    void OnChoiceChanged(std::function<void()> onChange);
}
