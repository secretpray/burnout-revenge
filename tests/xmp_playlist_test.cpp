// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// XMP's title playlists (apu/xmp_playlist.h): XMP_SONGDESCRIPTOR arrays as
// Burnout Revenge passes them to XMPCreateTitlePlaylist, and the order the
// songs play in (in order or shuffled, with or without repeat, Next and
// Previous).
#include <apu/xmp_playlist.h>
#include <cassert>
#include <map>
#include <set>

namespace
{
    // A guest memory image: big-endian words and UTF-16 strings at addresses.
    struct Image
    {
        std::vector<uint8_t> bytes = std::vector<uint8_t>(0x1000);
        uint32_t base = 0x80000000;
        uint32_t next = 0x80000800;  // strings from here

        uint8_t* At(uint32_t address) { return bytes.data() + (address - base); }
        void Word(uint32_t address, uint32_t v)
        {
            uint8_t* p = At(address);
            p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
        }
        uint32_t String(const std::u16string& s)
        {
            uint32_t address = next;
            for (size_t i = 0; i <= s.size(); i++)
            {
                char16_t c = i < s.size() ? s[i] : 0;
                At(address)[2 * i] = uint8_t(c >> 8);
                At(address)[2 * i + 1] = uint8_t(c);
            }
            next += uint32_t(2 * (s.size() + 1) + 3) & ~3u;
            return address;
        }
        std::u16string Read(uint32_t address) { return address ? xmp::LoadBEString(At(address)) : std::u16string(); }
    };

    void TestDescriptors()
    {
        Image image;
        uint32_t songs = image.base;
        for (uint32_t i = 0; i < 3; i++)
        {
            uint32_t d = songs + i * xmp::kSongDescriptorSize;
            std::u16string n = u"0" + std::u16string(1, char16_t(u'0' + i));
            image.Word(d + 0, image.String(u"GAME:\\eatrax\\" + n + u".wma"));
            image.Word(d + 4, image.String(u"Title " + n));
            image.Word(d + 8, i == 1 ? 0 : image.String(u"Artist"));  // no artist: an empty string
            image.Word(d + 12, image.String(u"Album"));
            image.Word(d + 16, 0);
            image.Word(d + 20, image.String(u"Rock"));
            image.Word(d + 24, i + 1);
            image.Word(d + 28, 180000 + i);
            image.Word(d + 32, 0);  // WMA
        }
        auto parsed = xmp::ParseSongDescriptors(image.At(songs), 3, [&](uint32_t a) { return image.Read(a); });
        assert(parsed.size() == 3);
        assert(parsed[0].path == u"GAME:\\eatrax\\00.wma");
        assert(xmp::ToUtf8(parsed[2].path) == "GAME:\\eatrax\\02.wma");
        assert(parsed[1].title == u"Title 01");
        assert(parsed[0].artist == u"Artist" && parsed[1].artist.empty());
        assert(parsed[2].album == u"Album" && parsed[2].albumArtist.empty() && parsed[2].genre == u"Rock");
        assert(parsed[1].trackNumber == 2 && parsed[1].durationMs == 180001 && parsed[1].format == 0);

        // UTF-8 of what isn't ASCII, a surrogate pair included.
        assert(xmp::ToUtf8(u"Motörhead") == "Mot\xC3\xB6rhead");
        assert(xmp::ToUtf8(u"♪") == "\xE2\x99\xAA");
        assert(xmp::ToUtf8(u"\U0001F3B5") == "\xF0\x9F\x8E\xB5");
        // A string without its NUL stops at the limit.
        uint8_t unterminated[8] = { 0, 'a', 0, 'b', 0, 'c', 0, 'd' };
        assert(xmp::LoadBEString(unterminated, 4) == u"abcd");
    }

    void TestInOrder()
    {
        xmp::Sequence seq(1);
        seq.SetBehavior(xmp::PlaybackMode::InOrder, xmp::RepeatMode::Playlist);
        assert(!seq.Start(0) && !seq.Active());
        assert(seq.Start(3) == 0u);
        assert(seq.SongEnded() == 1u);
        assert(seq.SongEnded() == 2u);
        assert(seq.SongEnded() == 0u);  // repeat: from the start again
        assert(seq.Previous() == 2u);   // Previous wraps backwards
        assert(seq.Next() == 0u);       // and Next forwards
        assert(seq.Next() == 1u);

        // No repeat: the playlist ends after its last song, but Next wraps.
        seq.SetBehavior(xmp::PlaybackMode::InOrder, xmp::RepeatMode::NoRepeat);
        assert(seq.Current() == 1u);
        assert(seq.SongEnded() == 2u);
        assert(!seq.SongEnded());
        assert(seq.Start(3, 2u) == 2u);  // from the song XMPPlayTitlePlaylist names
        assert(seq.Next() == 0u);
        assert(seq.Start(3, 7u) == 0u);  // a song that isn't there: from the first

        seq.Clear();
        assert(!seq.Active() && !seq.Next() && !seq.Previous() && !seq.SongEnded());

        // One song with repeat: it plays again.
        assert(seq.Start(1) == 0u);
        seq.SetBehavior(xmp::PlaybackMode::InOrder, xmp::RepeatMode::Playlist);
        assert(seq.SongEnded() == 0u && seq.Next() == 0u && seq.Previous() == 0u);
    }

    bool IsPermutation(const std::vector<uint32_t>& order, uint32_t count)
    {
        std::set<uint32_t> seen(order.begin(), order.end());
        return order.size() == count && seen.size() == count && *seen.rbegin() == count - 1;
    }

    void TestShuffle()
    {
        constexpr uint32_t kSongs = 41;  // Burnout Revenge's eatrax/00..40.wma
        xmp::Sequence seq(12345);
        seq.SetBehavior(xmp::PlaybackMode::Shuffle, xmp::RepeatMode::Playlist);
        auto first = seq.Start(kSongs);
        assert(first && IsPermutation(seq.Order(), kSongs));
        bool shuffled = false;
        for (uint32_t i = 0; i < kSongs; i++)
            shuffled |= seq.Order()[i] != i;
        assert(shuffled);

        // Every song once per round, a new order each round, never the song
        // just played first in the next round.
        std::vector<uint32_t> round1 = seq.Order();
        std::vector<uint32_t> played{ *first };
        for (uint32_t i = 1; i < kSongs; i++)
            played.push_back(*seq.SongEnded());
        assert(played == round1);
        uint32_t last = played.back();
        auto next = seq.SongEnded();
        assert(next && *next != last);
        assert(IsPermutation(seq.Order(), kSongs) && seq.Order() != round1);

        // The named song plays first when shuffled too.
        for (uint32_t song : { 0u, 17u, 40u })
            assert(seq.Start(kSongs, song) == song && IsPermutation(seq.Order(), kSongs));

        // No repeat: one round, then nothing.
        seq.SetBehavior(xmp::PlaybackMode::Shuffle, xmp::RepeatMode::NoRepeat);
        seq.Start(kSongs);
        uint32_t count = 1;
        while (seq.SongEnded())
            count++;
        assert(count == kSongs);

        // Switching modes keeps the song playing; the rest follow the new mode.
        seq.SetBehavior(xmp::PlaybackMode::InOrder, xmp::RepeatMode::Playlist);
        assert(seq.Start(kSongs, 5u) == 5u);
        seq.SetBehavior(xmp::PlaybackMode::Shuffle, xmp::RepeatMode::Playlist);
        assert(seq.Current() == 5u && seq.Order()[0] == 5u && IsPermutation(seq.Order(), kSongs));
        seq.Next();
        uint32_t now = seq.Current();
        seq.SetBehavior(xmp::PlaybackMode::InOrder, xmp::RepeatMode::Playlist);
        assert(seq.Current() == now && seq.SongEnded() == (now + 1) % kSongs);

        // The same seed, the same orders.
        xmp::Sequence a(7), b(7);
        a.SetBehavior(xmp::PlaybackMode::Shuffle, xmp::RepeatMode::Playlist);
        b.SetBehavior(xmp::PlaybackMode::Shuffle, xmp::RepeatMode::Playlist);
        a.Start(kSongs);
        b.Start(kSongs);
        assert(a.Order() == b.Order());

        // Two songs shuffled with repeat alternate.
        xmp::Sequence two(3);
        two.SetBehavior(xmp::PlaybackMode::Shuffle, xmp::RepeatMode::Playlist);
        uint32_t song = *two.Start(2);
        for (int i = 0; i < 20; i++)
        {
            uint32_t n = *two.SongEnded();
            assert(n != song);
            song = n;
        }
    }
}

int main()
{
    TestDescriptors();
    TestInOrder();
    TestShuffle();
}
