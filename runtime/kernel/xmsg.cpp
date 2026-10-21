// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// XAM message apps (XMsg*). Only XMP (0xFA, the system music player, which
// games ask about custom soundtracks) is implemented, following Xenia's
// kernel/xam/apps/xmp_app.cc message by message. Most Wanted's playlists
// aren't played (it keeps its own music). Burnout Revenge plays EA Trax
// through it: its title playlist's songs (eatrax/NN.wma) play one after
// another (apu/music.cpp, mixed into the XAudio output), in the playback
// and repeat modes it sets (apu/xmp_playlist.h), with Pause, Continue, Next,
// Previous, Stop and GetInfo; each song change is an XN_XMP_STATECHANGED,
// as is the playlist's end (idle). With Audio -> Music set to My Music the
// player's own files (user/my_music.h) play in the playlist's place. XGI (0xFB: achievements,
// sessions, stats) and XLiveBase (0xFC) messages fail with X_E_FAIL and a log
// line, as Xenia's unknown messages do; add them as the game needs them.
// Online (BURNOUT_ONLINE, user/online.h) XLiveBase answers as a console
// signed in to Live, and XOnlineGetServiceInfo names the lobby server; XGI's
// presence and achievement writes succeed (XgiOnline, XLiveBaseOnline below,
// after the ReXGlue SDK's xlivebase_app/xgi_app in Xerenge's fork, BSD).
#include <stdafx.h>
#include "function.h"
#include "overlapped.h"
#include "xam.h"

#include <cpu/guest_thread.h>
#include "vfs.h"
#include <apu/music.h>
#include <apu/xmp_playlist.h>
#include <user/my_music.h>
#include <user/online.h>
#include <hid/script_event.h>

#include <thread>

namespace
{
    constexpr uint32_t X_E_SUCCESS = 0;
    constexpr uint32_t X_E_FAIL = 0x80004005;
    constexpr uint32_t X_E_INVALIDARG = 0x80070057;
    constexpr uint32_t X_E_NOTFOUND = 0x80070490;
    constexpr uint32_t X_ERROR_IO_PENDING = 997;
    constexpr uint32_t X_ERROR_NOT_FOUND = 1168;

    // XMP notifications (XN_XMP_*).
    constexpr uint32_t kMsgStateChanged = 0x0A000001;
    constexpr uint32_t kMsgPlaybackBehaviorChanged = 0x0A000002;
    constexpr uint32_t kMsgPlaybackControllerChanged = 0x0A000003;

    struct Xmp
    {
        std::mutex mutex;
        uint32_t state = 0;             // 0 idle/stopped, 1 playing, 2 paused
        uint32_t playbackClient = 1;    // the title controls playback
        uint32_t playbackMode = 0;
        uint32_t repeatMode = 0;
        uint32_t unknownFlags = 0;
        float volume = 1.0f;
        std::unordered_map<uint32_t, uint32_t> playlists;  // handle -> song count
        struct Playlist
        {
            std::vector<xmp::Song> songs;
            std::vector<std::filesystem::path> files;  // host paths (empty: not found)
        };
        std::unordered_map<uint32_t, Playlist> titlePlaylists;  // by handle, as `playlists`
        uint32_t active = 0;       // the playlist playing (or paused), 0: none
        xmp::Sequence sequence;    // its order
        uint64_t song = 0;         // music::Play's number for the song playing
        uint32_t failures = 0;     // songs in a row that couldn't be played
        uint32_t nextSongHandle = 0;
        // Music: My Music (user/my_music.h). The song playing is one of the
        // player's own files in the title's playlist's place: to the game
        // the playlist plays on (no end, no state change: no EA Trax banner
        // either) until it plays
        // another (a skip), stops it, or Music goes back to EA Trax.
        bool mine = false;
        bool mineFailed = false;  // none of the files played: EA Trax until Music changes
        size_t mineCount = 0;     // files in the folder at the last pick
    } s_xmp;

    template<typename T>
    T* Guest(uint32_t address) { return static_cast<T*>(g_memory.Translate(address)); }

    // Under s_xmp.mutex. The playlist a message's storage pointer names: the
    // handle its first word holds (Xenia), or else the storage itself (the
    // handle CreateTitlePlaylist gave). Burnout Revenge writes over that word
    // after the playlist is made: its second song's XMPPlayTitlePlaylist
    // found no playlist and the music stopped after the first.
    uint32_t PlaylistOf(uint32_t storage)
    {
        uint32_t handle = *Guest<be<uint32_t>>(storage);
        return s_xmp.playlists.count(handle) ? handle : storage;
    }

    std::u16string GuestString(uint32_t address)
    {
        return address ? xmp::LoadBEString(Guest<uint8_t>(address)) : std::u16string();
    }

    // Under s_xmp.mutex. The active playlist's song `index` from its start.
    void PlaySong(uint32_t index)
    {
        auto& playlist = s_xmp.titlePlaylists.at(s_xmp.active);
        const auto& song = playlist.songs[index];
        fprintf(stderr, "[xmp] song %u/%zu: %s (%s - %s)\n", index + 1, playlist.songs.size(),
            xmp::ToUtf8(song.path).c_str(), xmp::ToUtf8(song.artist).c_str(), xmp::ToUtf8(song.title).c_str());
        s_xmp.song = apu::music::Play(playlist.files[index]);
    }

    bool MyMusicChosen() { return user::my_music::Chosen(); }

    // Under s_xmp.mutex. The next of the player's own files, with a toast
    // naming it; false (EA Trax plays) when the folder has none.
    bool PlayMine()
    {
        const auto pick = user::my_music::Next();
        s_xmp.mineCount = pick.count;
        if (pick.file.empty())
        {
            fprintf(stderr, "[xmp] My Music: the Music Folder has no music files, EA Trax plays\n");
            user::my_music::Announce("My Music: the Music Folder has no music files. EA Trax plays.");
            return false;
        }
        fprintf(stderr, "[xmp] My Music (%zu files): %s\n", pick.count, pick.file.filename().string().c_str());
        s_xmp.song = apu::music::Play(pick.file);
        s_xmp.mine = true;
        user::my_music::Announce("Now playing: " + pick.name);
        return true;
    }

    // Under s_xmp.mutex: nothing plays any more.
    void StopPlaylist()
    {
        s_xmp.mine = false;
        apu::music::Stop();
        s_xmp.sequence.Clear();
        s_xmp.active = 0;
        s_xmp.song = 0;
        s_xmp.state = 0;
    }

    // The audio thread: a song ended (or couldn't be played).
    void OnSongEnd(uint64_t song, bool failed)
    {
        std::lock_guard lock(s_xmp.mutex);
        if (song != s_xmp.song || !s_xmp.active)
            return;  // another song started meanwhile, or none plays
        if (s_xmp.mine)
        {
            s_xmp.failures = failed ? s_xmp.failures + 1 : 0;
            if (s_xmp.failures >= std::max<size_t>(s_xmp.mineCount, 1) || !MyMusicChosen())
            {
                if (s_xmp.failures)
                {
                    fprintf(stderr, "[xmp] My Music: none of the files plays, EA Trax plays\n");
                    s_xmp.mineFailed = true;
                }
                StopPlaylist();  // the game plays its next EA Trax song
                XamNotifyEnqueueEvent(kMsgStateChanged, s_xmp.state);
                return;
            }
            // Not on the audio thread: the folder is scanned and the file
            // opened for its tags.
            std::thread([song] {
                std::lock_guard lock(s_xmp.mutex);
                if (song == s_xmp.song && s_xmp.mine && !PlayMine())
                {
                    StopPlaylist();
                    XamNotifyEnqueueEvent(kMsgStateChanged, s_xmp.state);
                }
            }).detach();
            return;
        }
        size_t count = s_xmp.titlePlaylists.at(s_xmp.active).songs.size();
        s_xmp.failures = failed ? s_xmp.failures + 1 : 0;
        std::optional<uint32_t> next;
        if (s_xmp.failures < count)
            next = s_xmp.sequence.SongEnded();
        else
            fprintf(stderr, "[xmp] no song of the playlist can be played: stopped\n");
        if (next)
            PlaySong(*next);
        else
        {
            fprintf(stderr, "[xmp] playlist done\n");
            StopPlaylist();
        }
        XamNotifyEnqueueEvent(kMsgStateChanged, s_xmp.state);
    }

    // The menu's thread: Music changed. A song playing switches at once: to
    // the player's own, or (from them) to the game's next EA Trax song.
    void OnChoiceChanged()
    {
        std::lock_guard lock(s_xmp.mutex);
        s_xmp.mineFailed = false;
        s_xmp.failures = 0;
        if (!s_xmp.active || s_xmp.state != 1)
            return;
        if (MyMusicChosen() && !s_xmp.mine)
            PlayMine();
        else if (!MyMusicChosen() && s_xmp.mine)
        {
            StopPlaylist();
            XamNotifyEnqueueEvent(kMsgStateChanged, s_xmp.state);
        }
    }

    // At most `units` UTF-16 units with the NUL, big-endian, at `out`.
    void StoreString(uint8_t* out, const std::u16string& value, size_t units)
    {
        size_t n = std::min(value.size(), units - 1);
        for (size_t i = 0; i < units; i++)
        {
            char16_t c = i < n ? value[i] : 0;
            out[2 * i] = uint8_t(c >> 8);
            out[2 * i + 1] = uint8_t(c);
        }
    }

    uint32_t XmpDispatch(uint32_t message, uint32_t buffer, uint32_t length)
    {
        std::lock_guard lock(s_xmp.mutex);
        auto* args = Guest<be<uint32_t>>(buffer);
        switch (message)
        {
        case 0x00070002: // XMPPlayTitlePlaylist(client, storage, song)
        {
            uint32_t handle = *Guest<be<uint32_t>>(args[1]);
            handle = PlaylistOf(args[1]);
            if (!s_xmp.playlists.count(handle))
                return X_E_NOTFOUND;
            static const bool handlerSet = [] {
                apu::music::SetSongEndHandler(OnSongEnd);
                user::my_music::OnChoiceChanged(OnChoiceChanged);
                return true;
            }();
            (void)handlerSet;
            {
                auto& playlist = s_xmp.titlePlaylists[handle];
                std::optional<uint32_t> first;
                for (uint32_t i = 0; i < playlist.songs.size(); i++)
                    if (args[2] != 0 && playlist.songs[i].handle == args[2])
                        first = i;
                fprintf(stderr, "[xmp] PlayTitlePlaylist %08X, song %08X, mode %u, repeat %u\n", handle, uint32_t(args[2]),
                    s_xmp.playbackMode, s_xmp.repeatMode);
                apu::music::Stop();
                s_xmp.active = handle;
                s_xmp.failures = 0;
                if (auto index = s_xmp.sequence.Start(uint32_t(playlist.songs.size()), first))
                {
                    if (!MyMusicChosen() || s_xmp.mineFailed || !PlayMine())
                    {
                        s_xmp.mine = false;
                        PlaySong(*index);
                    }
                    s_xmp.state = 1;
                }
                else
                    StopPlaylist();
            }
            // Not for the player's own: the state change is what shows the
            // game's EA Trax banner, with the EA song's name (runs 151/153).
            if (!s_xmp.mine)
                XamNotifyEnqueueEvent(kMsgStateChanged, s_xmp.state);
            return X_E_SUCCESS;
        }
        case 0x00070003: // XMPContinue
            if (s_xmp.state == 2)  // (the game sends it every frame of a race)
            {
                fprintf(stderr, "[xmp] Continue\n");
                apu::music::Resume();
                s_xmp.state = 1;
                XamNotifyEnqueueEvent(kMsgStateChanged, s_xmp.state);
            }
            return X_E_SUCCESS;
        case 0x00070004: // XMPStop
            fprintf(stderr, "[xmp] Stop (state %u)\n", s_xmp.state);
            StopPlaylist();
            s_xmp.state = 0;
            XamNotifyEnqueueEvent(kMsgStateChanged, s_xmp.state);
            return X_E_SUCCESS;
        case 0x00070005: // XMPPause
            if (s_xmp.state == 1)
            {
                fprintf(stderr, "[xmp] Pause\n");
                apu::music::Pause();
            }
            if (s_xmp.state == 1) s_xmp.state = 2;
            XamNotifyEnqueueEvent(kMsgStateChanged, s_xmp.state);
            return X_E_SUCCESS;
        case 0x00070006: // XMPNext
        case 0x00070007: // XMPPrevious
            if (s_xmp.active)
            {
                auto index = message == 0x00070006 ? s_xmp.sequence.Next() : s_xmp.sequence.Previous();
                fprintf(stderr, "[xmp] %s\n", message == 0x00070006 ? "Next" : "Previous");
                s_xmp.failures = 0;
                if (!s_xmp.mine || !PlayMine())
                {
                    s_xmp.mine = false;
                    PlaySong(*index);
                }
                s_xmp.state = 1;
                if (!s_xmp.mine)
                    XamNotifyEnqueueEvent(kMsgStateChanged, s_xmp.state);
                return X_E_SUCCESS;
            }
            return X_E_NOTFOUND;
        case 0x00070008: // XMPSetPlaybackBehavior(client, mode, repeat, flags)
            s_xmp.playbackMode = args[1];
            s_xmp.repeatMode = args[2];
            s_xmp.unknownFlags = args[3];
            fprintf(stderr, "[xmp] SetPlaybackBehavior: mode %u, repeat %u, flags %08X\n", s_xmp.playbackMode, s_xmp.repeatMode,
                s_xmp.unknownFlags);
            s_xmp.sequence.SetBehavior(s_xmp.playbackMode == 1 ? xmp::PlaybackMode::Shuffle : xmp::PlaybackMode::InOrder,
                s_xmp.repeatMode == 1 ? xmp::RepeatMode::NoRepeat : xmp::RepeatMode::Playlist);
            XamNotifyEnqueueEvent(kMsgPlaybackBehaviorChanged, 0);
            return X_E_SUCCESS;
        case 0x00070009: // XMPGetStatus(client, state*)
            *Guest<be<uint32_t>>(args[1]) = s_xmp.state;
            return X_E_SUCCESS;
        case 0x0007000B: // XMPGetVolume(client, float*)
            *Guest<be<float>>(args[1]) = s_xmp.volume;
            return X_E_SUCCESS;
        case 0x0007000C: // XMPSetVolume(client, float)
            s_xmp.volume = reinterpret_cast<be<float>*>(args)[1];
            apu::music::SetVolume(s_xmp.volume);
            {
                // (The game fades it every frame around a race's start: only
                // steps of 0.1 are logged.)
                static float logged = -1.0f;
                if (std::fabs(s_xmp.volume - logged) >= 0.1f)
                    fprintf(stderr, "[xmp] SetVolume %.3f\n", logged = s_xmp.volume);
            }
            return X_E_SUCCESS;
        case 0x0007000D: // XMPCreateTitlePlaylist(client, storage, size, songs, count, name, flags, songHandles*, handle*)
        {
            uint32_t storage = args[1];
            *Guest<be<uint32_t>>(args[8]) = storage;  // the handle is the storage pointer (Xenia)
            *Guest<be<uint32_t>>(storage) = storage;
            s_xmp.playlists[storage] = args[4];
            {
                // A playlist made again in the same storage replaces the one playing.
                if (s_xmp.active == storage)
                    StopPlaylist();
                auto& playlist = s_xmp.titlePlaylists[storage];
                playlist.songs = args[3] ? xmp::ParseSongDescriptors(Guest<uint8_t>(args[3]), args[4], GuestString)
                                         : std::vector<xmp::Song>();
                playlist.files.clear();
                uint32_t found = 0;
                for (uint32_t i = 0; i < playlist.songs.size(); i++)
                {
                    auto& song = playlist.songs[i];
                    song.handle = ++s_xmp.nextSongHandle;
                    if (args[7])
                        Guest<be<uint32_t>>(args[7])[i] = song.handle;
                    auto resolved = vfs::Resolve(xmp::ToUtf8(song.path), true);
                    playlist.files.push_back(resolved ? resolved->host : std::filesystem::path());
                    found += resolved.has_value();
                }
                fprintf(stderr, "[xmp] CreateTitlePlaylist %08X: %zu songs (%u found), first %s\n", storage,
                    playlist.songs.size(), found, playlist.songs.empty() ? "-" : xmp::ToUtf8(playlist.songs[0].path).c_str());
            }
            // The game's music is made once the profile is loaded (its "Load
            // Successful", before the main menu): where an automated run's
            // script goes on from.
            static bool menu = false;
            if (!std::exchange(menu, true))
                hid::ScriptEvent("menu");
            return X_E_SUCCESS;
        }
        case 0x0007000E: // XMPGetInfo(client, unknown, info*)
            // XMP_SONGINFO as Xenia writes it: the song's handle, then its
            // title, artist, album, album artist and genre (20 units each)
            // from +576, its track number, duration and format.
            if (s_xmp.active && args[2])
            {
                const auto& song = s_xmp.titlePlaylists.at(s_xmp.active).songs[s_xmp.sequence.Current()];
                auto* info = Guest<uint8_t>(args[2]);
                *reinterpret_cast<be<uint32_t>*>(info) = song.handle;
                uint8_t* text = info + 4 + 572;
                StoreString(text + 0, song.title, 20);
                StoreString(text + 40, song.artist, 20);
                StoreString(text + 80, song.album, 20);
                StoreString(text + 120, song.albumArtist, 20);
                StoreString(text + 160, song.genre, 20);
                *reinterpret_cast<be<uint32_t>*>(text + 200) = song.trackNumber;
                *reinterpret_cast<be<uint32_t>*>(text + 204) = song.durationMs;
                *reinterpret_cast<be<uint32_t>*>(text + 208) = song.format;
                return X_E_SUCCESS;
            }
            return X_E_FAIL;  // nothing is playing
        case 0x00070013: // XMPDeleteTitlePlaylist(client, storage)
            if (uint32_t handle = PlaylistOf(args[1]); s_xmp.titlePlaylists.erase(handle))
            {
                if (s_xmp.active == handle)
                {
                    StopPlaylist();
                    XamNotifyEnqueueEvent(kMsgStateChanged, s_xmp.state);
                }
                return s_xmp.playlists.erase(handle) ? X_E_SUCCESS : X_E_NOTFOUND;
            }
            return s_xmp.playlists.erase(*Guest<be<uint32_t>>(args[1])) ? X_E_SUCCESS : X_E_NOTFOUND;
        case 0x0007001A: // XMPSetPlaybackController(client, controller, playbackClient)
            s_xmp.playbackClient = args[2];
            XamNotifyEnqueueEvent(kMsgPlaybackControllerChanged, !uint32_t(args[2]));
            return X_E_SUCCESS;
        case 0x0007001B: // XMPGetPlaybackController(client, controller*, locked*)
            *Guest<be<uint32_t>>(args[1]) = 0;
            *Guest<be<uint32_t>>(args[2]) = 0;
            return X_E_SUCCESS;
        case 0x00070029: // XMPGetPlaybackBehavior(client, mode*, repeat*, flags*)
            if (args[1]) *Guest<be<uint32_t>>(args[1]) = s_xmp.playbackMode;
            if (args[2]) *Guest<be<uint32_t>>(args[2]) = s_xmp.repeatMode;
            if (args[3]) *Guest<be<uint32_t>>(args[3]) = s_xmp.unknownFlags;
            return X_E_SUCCESS;
        case 0x0007002E: // storage size for a playlist of N songs
            *Guest<be<uint32_t>>(args[2]) = 4 + uint32_t(args[1]) * 128;
            return X_E_SUCCESS;
        }
        fprintf(stderr, "[xmp] unimplemented message %08X (buffer %08X, %u bytes)\n", message, buffer, length);
        return X_E_FAIL;
    }

    // XLiveBase (0xFC) online. The arguments are the in-process call's two:
    // for XOnlineGetServiceInfo the service id and the XONLINE_SERVICE_INFO to
    // fill. X_E_NOTFOUND: not answered here.
    uint32_t XLiveBaseOnline(uint32_t message, uint32_t buffer, uint32_t length)
    {
        switch (message)
        {
        case 0x00058004:  // XLiveBaseGetLogonId: a bool
            if (buffer)
                *Guest<be<uint32_t>>(buffer) = 1;
            return X_E_SUCCESS;
        case 0x00058006:  // XLiveBaseGetNatType
            if (buffer)
                *Guest<be<uint32_t>>(buffer) = 1;  // XONLINE_NAT_OPEN
            return X_E_SUCCESS;
        case 0x00058007:  // XOnlineGetServiceInfo(service id, XONLINE_SERVICE_INFO*)
        {
            if (!length)
                return X_E_FAIL;
            // { dwServiceID, IN_ADDR serviceIP, WORD wServicePort, WORD reserved }:
            // EA's DirtySock reaches its lobby (0x45410004) through this, and
            // won't go online while it fails. An address, never a name.
            const uint32_t address = online::LobbyServerAddress();  // network order
            const uint8_t* ip = reinterpret_cast<const uint8_t*>(&address);
            *Guest<be<uint32_t>>(length) = buffer;
            *Guest<be<uint32_t>>(length + 4) = uint32_t(ip[0]) << 24 | uint32_t(ip[1]) << 16 | uint32_t(ip[2]) << 8 | ip[3];
            *Guest<be<uint16_t>>(length + 8) = 3074;
            *Guest<be<uint16_t>>(length + 10) = 0;
            fprintf(stderr, "[online] service %08X is at %u.%u.%u.%u\n", buffer, ip[0], ip[1], ip[2], ip[3]);
            return X_E_SUCCESS;
        }
        case 0x00058037:  // XPresenceInitialize
        case 0x00058046:  // needed to succeed for a signed-in profile to count
        case 0x0005000B:  // XStringVerify: with no Live to ask, every string passes (or clip comments are "profane")
            return X_E_SUCCESS;
        default:
            return X_E_NOTFOUND;
        }
    }

    // XGI (0xFB) online: the presence context and properties, and achievement
    // writes, accepted (the game sets its rich presence every 10 s); and the
    // Xbox Live session a game of the lobby's runs in. Its creation, as host
    // (XSESSION_CREATE_HOST), makes the session's XSESSION_INFO - this
    // console's XNADDR, a new XNKID and XNKEY - which the host's lobby game
    // record carries to the others (SESS); a joiner passes the host's in.
    // Join, leave, start, end, modify and delete are accepted. Session search
    // returns an empty result while matchmaking is supplied by the EA lobby.
    uint32_t XgiOnline(uint32_t message, uint32_t buffer, uint32_t length)
    {
        switch (message)
        {
        case 0x000B0006:  // XGIUserSetContextEx
        case 0x000B0007:  // XGIUserSetPropertyEx
        case 0x000B0008:  // XGIUserWriteAchievements
            return X_E_SUCCESS;
        case 0x000B0010:  // XGISessionCreateImpl(session, flags, public, private, xuid, info*, nonce*)
        {
            if (!buffer)
                return X_E_FAIL;
            const uint32_t flags = *Guest<be<uint32_t>>(buffer + 4);
            const uint32_t info = *Guest<be<uint32_t>>(buffer + 20), nonce = *Guest<be<uint32_t>>(buffer + 24);
            if (flags & 1)
                online::FillHostSessionInfo(info ? Guest<uint8_t>(info) : nullptr, nonce ? Guest<uint8_t>(nonce) : nullptr);
            fprintf(stderr, "[online] XGI session create (flags %08X, %u public slots)%s\n", flags,
                uint32_t(*Guest<be<uint32_t>>(buffer + 8)), (flags & 1) ? ": host" : ": joining");
            hid::ScriptEvent("session");  // in a game's lobby: an online bot's script goes on
            return X_E_SUCCESS;
        }
        case 0x000B0016:  // XSessionSearch (sub_82421818 builds the 32-byte request)
        {
            if (!buffer || length < 32)
                return X_E_INVALIDARG;
            auto* args = Guest<be<uint32_t>>(buffer);
            const uint32_t size = args[6], results = args[7];
            if (!results || size < 8)
                return X_E_INVALIDARG;
            // XSESSION_SEARCHRESULT_HEADER: count, pointer to results. Clear
            // both even when the caller's buffer contains an earlier search.
            *Guest<be<uint32_t>>(results) = 0;
            *Guest<be<uint32_t>>(results + 4) = 0;
            fprintf(stderr, "[online] XGI session search: proc %u, user %u, max %u, props %u, contexts %u, size %u: empty\n",
                uint32_t(args[0]), uint32_t(args[1]), uint32_t(args[2]),
                uint32_t(*Guest<be<uint16_t>>(buffer + 12)), uint32_t(*Guest<be<uint16_t>>(buffer + 14)), size);
            if (getenv("BURNOUT_LOG_NET") && uint32_t(args[5]))
                for (uint32_t i = 0; i < uint32_t(*Guest<be<uint16_t>>(buffer + 14)); ++i)
                {
                    const uint32_t context = uint32_t(args[5]) + i * 8;
                    fprintf(stderr, "[online] search context %08X = %08X\n",
                        uint32_t(*Guest<be<uint32_t>>(context)), uint32_t(*Guest<be<uint32_t>>(context + 4)));
                }
            return X_E_SUCCESS;
        }
        case 0x000B0011:  // XGISessionDelete
        case 0x000B0012:  // XGISessionJoinLocal/Remote
        case 0x000B0013:  // XGISessionLeaveLocal/Remote
        case 0x000B0014:  // XSessionStart
        case 0x000B0015:  // XSessionEnd
        case 0x000B0018:  // XSessionModify
        {
            static const char* const names[] = { "delete", "join", "leave", "start", "end", "?", "?", "modify" };
            fprintf(stderr, "[online] XGI session %s\n", names[(message - 0x000B0011) & 7]);
            if (message == 0x000B0014)
                hid::ScriptEvent("race");
            else if (message == 0x000B0015)
                hid::ScriptEvent("raceend");
            return X_E_SUCCESS;
        }
        case 0x000B001A:  // XSessionArbitrationRegister: at a race's start
            fprintf(stderr, "[online] XGI session arbitration: no Xbox Live arbitration service\n");
            return X_E_SUCCESS;
        case 0x000B0025:  // XSessionWriteStats: at its end
            fprintf(stderr, "[online] XGI session stats: accepted, not stored (no ranking service)\n");
            return X_E_SUCCESS;
        default:
            return X_E_NOTFOUND;
        }
    }

    uint32_t Dispatch(uint32_t app, uint32_t message, uint32_t buffer, uint32_t length)
    {
        if (app == 0xFA)
            return XmpDispatch(message, buffer, length);
        if (online::Enabled() && (app == 0xFB || app == 0xFC))
        {
            const uint32_t result = app == 0xFC ? XLiveBaseOnline(message, buffer, length) : XgiOnline(message, buffer, length);
            if (result != X_E_NOTFOUND)
                return result;
        }
        if (app == 0xFB || app == 0xFC)
        {
            fprintf(stderr, "[xmsg] app %02X message %08X (buffer %08X, %u) not implemented (returning X_E_FAIL)\n", app, message,
                buffer, length);
            return X_E_FAIL;
        }
        return X_E_NOTFOUND;
    }
}

uint32_t XMsgInProcessCall(uint32_t app, uint32_t message, uint32_t arg1, uint32_t arg2)
{
    return Dispatch(app, message, arg1, arg2);
}

uint32_t XMsgSystemProcessCall(uint32_t app, uint32_t message, uint32_t buffer, uint32_t length)
{
    return Dispatch(app, message, buffer, length);
}

uint32_t XMsgStartIORequest(uint32_t app, uint32_t message, XXOVERLAPPED* overlapped, uint32_t buffer, uint32_t length)
{
    uint32_t result = Dispatch(app, message, buffer, length);
    if (result == X_E_NOTFOUND)
    {
        fprintf(stderr, "[xmsg] XMsgStartIORequest: app %08X undefined\n", app);
        result = X_E_INVALIDARG;
        GuestThread::SetLastError(X_ERROR_NOT_FOUND);
    }
    if (overlapped != nullptr)
    {
        CompleteOverlapped(overlapped, result);
        result = X_ERROR_IO_PENDING;
    }
    if (result == 0 || result == X_ERROR_IO_PENDING)
        GuestThread::SetLastError(0);
    return result;
}

uint32_t XMsgCancelIORequest(XXOVERLAPPED* overlapped, uint32_t wait)
{
    return 0;  // every request completes immediately
}

GUEST_FUNCTION_HOOK(__imp__XMsgInProcessCall, XMsgInProcessCall);
GUEST_FUNCTION_HOOK(__imp__XMsgSystemProcessCall, XMsgSystemProcessCall);
GUEST_FUNCTION_HOOK(__imp__XMsgStartIORequest, XMsgStartIORequest);
GUEST_FUNCTION_HOOK(__imp__XMsgCancelIORequest, XMsgCancelIORequest);
