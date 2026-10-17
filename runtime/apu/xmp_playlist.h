// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// XMP's title playlists (kernel/xmsg.cpp), header only so the unit test
// (tests/xmp_playlist_test.cpp) builds it without the runtime:
//   - XMP_SONGDESCRIPTOR, as XMPCreateTitlePlaylist passes an array of them
//     (36 bytes each, big-endian; Xenia's kernel/xam/apps/xmp_app.cc):
//       +0  path        WCHAR* (Burnout Revenge: "GAME:\eatrax\NN.wma")
//       +4  title       WCHAR*
//       +8  artist      WCHAR*
//       +12 album       WCHAR*
//       +16 album artist WCHAR*
//       +20 genre       WCHAR*
//       +24 track number
//       +28 duration (ms)
//       +32 format (0 WMA, 1 MP3)
//   - which song plays next: the playback mode (in order or shuffled) and
//     the repeat mode (the playlist again, or stop after its last song), as
//     XMPSetPlaybackBehavior sets them. A song's end follows the repeat mode;
//     XMPNext and XMPPrevious always wrap around, as Xenia's do.
#pragma once
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace xmp
{
    // XMP_PLAYBACKMODE and XMP_REPEATMODE.
    enum class PlaybackMode : uint32_t { InOrder = 0, Shuffle = 1 };
    enum class RepeatMode : uint32_t { Playlist = 0, NoRepeat = 1 };

    inline constexpr uint32_t kSongDescriptorSize = 36;

    struct Song
    {
        uint32_t handle = 0;
        std::u16string path, title, artist, album, albumArtist, genre;
        uint32_t trackNumber = 0;
        uint32_t durationMs = 0;
        uint32_t format = 0;
    };

    inline uint32_t LoadBE32(const uint8_t* p)
    {
        return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    }

    // One descriptor at `p` (host memory). `readString(guestPointer)` gives
    // the UTF-16 string a guest pointer points to (empty for 0).
    template<typename ReadString>
    Song ParseSongDescriptor(const uint8_t* p, ReadString&& readString)
    {
        Song song;
        song.path = readString(LoadBE32(p + 0));
        song.title = readString(LoadBE32(p + 4));
        song.artist = readString(LoadBE32(p + 8));
        song.album = readString(LoadBE32(p + 12));
        song.albumArtist = readString(LoadBE32(p + 16));
        song.genre = readString(LoadBE32(p + 20));
        song.trackNumber = LoadBE32(p + 24);
        song.durationMs = LoadBE32(p + 28);
        song.format = LoadBE32(p + 32);
        return song;
    }

    template<typename ReadString>
    std::vector<Song> ParseSongDescriptors(const uint8_t* p, uint32_t count, ReadString&& readString)
    {
        std::vector<Song> songs;
        songs.reserve(count);
        for (uint32_t i = 0; i < count; i++)
            songs.push_back(ParseSongDescriptor(p + i * kSongDescriptorSize, readString));
        return songs;
    }

    // A big-endian, NUL-terminated UTF-16 string, at most `limit` units.
    inline std::u16string LoadBEString(const uint8_t* p, size_t limit = 1024)
    {
        std::u16string s;
        for (size_t i = 0; i < limit; i++)
        {
            char16_t c = char16_t(p[2 * i] << 8 | p[2 * i + 1]);
            if (c == 0)
                break;
            s.push_back(c);
        }
        return s;
    }

    inline std::string ToUtf8(const std::u16string& s)
    {
        std::string out;
        for (size_t i = 0; i < s.size(); i++)
        {
            uint32_t c = s[i];
            if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
                c = 0x10000 + ((c - 0xD800) << 10) + (s[++i] - 0xDC00);
            if (c < 0x80)
                out += char(c);
            else if (c < 0x800)
                out += { char(0xC0 | c >> 6), char(0x80 | (c & 0x3F)) };
            else if (c < 0x10000)
                out += { char(0xE0 | c >> 12), char(0x80 | (c >> 6 & 0x3F)), char(0x80 | (c & 0x3F)) };
            else
                out += { char(0xF0 | c >> 18), char(0x80 | (c >> 12 & 0x3F)), char(0x80 | (c >> 6 & 0x3F)), char(0x80 | (c & 0x3F)) };
        }
        return out;
    }

    // The order a playlist's songs play in. Indices are into the playlist;
    // in shuffle mode the order is a permutation, drawn again each time the
    // playlist starts over (never with the song just played first).
    class Sequence
    {
    public:
        explicit Sequence(uint32_t seed = std::random_device{}()) : m_rng(seed) {}

        void SetBehavior(PlaybackMode mode, RepeatMode repeat)
        {
            bool reorder = mode != m_mode;
            m_mode = mode;
            m_repeat = repeat;
            if (reorder && !m_order.empty())
                Reorder(Current(), false);
        }
        PlaybackMode Mode() const { return m_mode; }
        RepeatMode Repeat() const { return m_repeat; }

        // A playlist of `count` songs starts, from `first` if given (the
        // song XMPPlayTitlePlaylist names), else from the first in order.
        // Nothing to play without songs.
        std::optional<uint32_t> Start(uint32_t count, std::optional<uint32_t> first = std::nullopt)
        {
            m_count = count;
            m_order.clear();
            if (count == 0)
                return std::nullopt;
            if (first && *first >= count)
                first.reset();
            Reorder(first.value_or(0), !first && m_mode == PlaybackMode::Shuffle);
            return Current();
        }

        void Clear() { m_count = 0; m_order.clear(); m_position = 0; }
        bool Active() const { return !m_order.empty(); }
        uint32_t Current() const { return m_order.empty() ? 0 : m_order[m_position]; }

        // The current song ended: the next one, or nothing (the playlist is
        // done) after its last song without repeat.
        std::optional<uint32_t> SongEnded()
        {
            if (m_order.empty())
                return std::nullopt;
            if (m_position + 1 < m_order.size())
                return m_order[++m_position];
            if (m_repeat == RepeatMode::NoRepeat)
                return std::nullopt;
            return Wrap();
        }

        // XMPNext and XMPPrevious: always another song, wrapping around.
        std::optional<uint32_t> Next()
        {
            if (m_order.empty())
                return std::nullopt;
            if (m_position + 1 < m_order.size())
                return m_order[++m_position];
            return Wrap();
        }

        std::optional<uint32_t> Previous()
        {
            if (m_order.empty())
                return std::nullopt;
            m_position = m_position == 0 ? uint32_t(m_order.size()) - 1 : m_position - 1;
            return m_order[m_position];
        }

        const std::vector<uint32_t>& Order() const { return m_order; }

    private:
        // The playlist from its start again (shuffled: a new order whose
        // first song isn't the one that just played, when there are two).
        uint32_t Wrap()
        {
            if (m_mode == PlaybackMode::Shuffle)
            {
                uint32_t last = Current();
                Shuffle();
                if (m_order.size() > 1 && m_order[0] == last)
                    std::swap(m_order[0], m_order[1 + m_rng() % (m_order.size() - 1)]);
            }
            m_position = 0;
            return m_order[0];
        }

        // The order for the mode, positioned at `current` (shuffled: it plays
        // first, unless `anyFirst`).
        void Reorder(uint32_t current, bool anyFirst)
        {
            m_order.resize(m_count);
            std::iota(m_order.begin(), m_order.end(), 0u);
            m_position = current;
            if (m_mode == PlaybackMode::Shuffle)
            {
                Shuffle();
                if (!anyFirst)
                    std::iter_swap(m_order.begin(), std::find(m_order.begin(), m_order.end(), current));
                m_position = 0;
            }
        }

        void Shuffle()
        {
            for (size_t i = m_order.size(); i > 1; i--)
                std::swap(m_order[i - 1], m_order[m_rng() % i]);
        }

        std::mt19937 m_rng;
        PlaybackMode m_mode = PlaybackMode::InOrder;
        RepeatMode m_repeat = RepeatMode::Playlist;
        uint32_t m_count = 0;
        std::vector<uint32_t> m_order;
        uint32_t m_position = 0;
    };
}
