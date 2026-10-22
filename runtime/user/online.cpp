#include <user/localization.h>
// Burnout Revenge port. GPL-3.0-or-later (see COPYING). See online.h.
#include "online.h"
#include "online_identity.h"
#include "settings.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <strings.h>

#ifdef BR_LAN_LOBBY
#include <ealobby/server.h>
#endif

namespace online
{
    namespace
    {
        struct Identity
        {
            bool enabled = false;
            std::string gamertag = kDefaultGamertag;
            uint64_t xuid = kOfflineXuid;
        };

        // The gamertag when none is set (online_identity.h).
        std::string DefaultGamertag()
        {
            const char* user = std::getenv("USER");
            return DefaultGamertagFor(user ? user : "");
        }

        const Identity& Get()
        {
            static const Identity identity = [] {
                Identity id;
                // Settings > Online (BURNOUT_ONLINE overrides it, settings.h).
                id.enabled = settings::GetBool(settings::Id::OnlineEnabled);
                if (!id.enabled)
                    return id;
                const char* tag = std::getenv("BURNOUT_GAMERTAG");
                std::string chosen = tag && *tag ? tag : settings::GetText(settings::TextId::Gamertag);
                id.gamertag = chosen.empty() ? DefaultGamertag() : SanitizeGamertag(chosen);
                id.xuid = XuidFor(id.gamertag);
                fprintf(stderr, "[online] signed in to Xbox Live as \"%s\" (XUID %016llX)\n",
                    id.gamertag.c_str(), (unsigned long long)id.xuid);
                return id;
            }();
            return identity;
        }

        std::string Text(uint32_t address)
        {
            char text[INET_ADDRSTRLEN] = "?";
            inet_ntop(AF_INET, &address, text, sizeof(text));
            return text;
        }

        uint32_t Configured(const char* name)
        {
            const char* value = std::getenv(name);
            in_addr parsed{};
            if (value && *value && inet_pton(AF_INET, value, &parsed) == 1)
                return parsed.s_addr;
            if (value && *value)
                fprintf(stderr, "[online] %s=\"%s\" is not an IPv4 address; ignored\n", name, value);
            return 0;
        }

        // The LAN address of a physical interface (wired or wireless), not
        // the default route's: with a VPN that is a tunnel the next machine
        // on the LAN can't reach. Private ranges and en* interfaces first;
        // tunnels, bridges and virtual machines' interfaces are skipped.
        // (The rule is the ReXGlue SDK's OnlineLocalAddress in Xerenge's fork,
        // shipa-2/rexglue-xerenge, BSD; written here for macOS.)
        uint32_t LanAddress()
        {
            ifaddrs* interfaces = nullptr;
            uint32_t best = 0;
            int bestRank = 0;
            if (getifaddrs(&interfaces) == 0)
            {
                for (ifaddrs* i = interfaces; i; i = i->ifa_next)
                {
                    if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) ||
                        (i->ifa_flags & (IFF_LOOPBACK | IFF_POINTOPOINT)))
                        continue;
                    const std::string name = i->ifa_name ? i->ifa_name : "";
                    auto starts = [&](const char* prefix) { return name.rfind(prefix, 0) == 0; };
                    if (starts("utun") || starts("tun") || starts("tap") || starts("bridge") || starts("vmnet") ||
                        starts("vboxnet") || starts("awdl") || starts("llw") || starts("anpi") || starts("ap"))
                        continue;
                    const uint32_t address = reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr.s_addr;
                    const uint32_t host = ntohl(address);
                    const bool privateRange = (host >> 24) == 10 || (host >> 20) == 0xAC1 || (host >> 16) == 0xC0A8;
                    const int rank = (privateRange ? 2 : 0) + (starts("en") ? 1 : 0) + 1;
                    if (rank > bestRank)
                    {
                        bestRank = rank;
                        best = address;
                    }
                }
                freeifaddrs(interfaces);
            }
            return best ? best : htonl(INADDR_LOOPBACK);
        }
    }

    bool Enabled() { return Get().enabled; }

    namespace
    {
        struct Lobby
        {
            uint32_t address = htonl(INADDR_LOOPBACK);
            bool found = false;
            bool own = false;
        };

        // The game's own lobby (online.h, phase 5): on this console's address
        // (any, on a LAN; the one given, for copies on one Mac), linked with
        // the others'. It runs until the game exits. Its log: what concerns
        // the LAN always, every message with BURNOUT_LOG_NET.
        bool StartOwnLobby(uint32_t address)
        {
#ifdef BR_LAN_LOBBY
            const char* net = std::getenv("BURNOUT_LOG_NET");
            const bool verbose = net && *net && std::strcmp(net, "0") != 0;
            ealobby::Options options;
            options.host = BindAddress() ? Text(BindAddress()) : "0.0.0.0";
            options.self_address = Text(address);
            options.locker_port = 0;
            auto* server = new ealobby::Server(options, [verbose](const std::string& line) {
                if (verbose || line.rfind("ealobby:", 0) == 0 || line.rfind("federation:", 0) == 0)
                    fprintf(stderr, "[lan-lobby] %s\n", line.c_str());
            });
            if (server->Start())
                return true;  // never stopped: it serves until the process ends
            delete server;
#else
            (void)address;
#endif
            fprintf(stderr, "[online] the game's own lobby could not start\n");
            return false;
        }

        const Lobby& GetLobby()
        {
            static const Lobby lobby = [] {
                Lobby l;
                // BURNOUT_LOBBY_SERVER, else Settings > Online > Server.
                std::string chosen;
                if (const char* env = std::getenv("BURNOUT_LOBBY_SERVER"))
                    chosen = env;
                else
                    switch (settings::OnlineServer(settings::GetInt(settings::Id::OnlineServer)))
                    {
                    case settings::OnlineServer::Community: chosen = kCommunityServer; break;
                    case settings::OnlineServer::Own: chosen = settings::GetText(settings::TextId::ServerAddress); break;
                    case settings::OnlineServer::Lan: chosen = "lan"; break;
                    }
                const char* name = chosen.c_str();
                if (name && strcasecmp(name, "lan") == 0)
                {
                    l.address = LocalAddress();
                    l.own = StartOwnLobby(l.address);
                    fprintf(stderr, "[online] lobby: the game's own, at %s (LAN, no server)\n",
                        Text(l.address).c_str());
                    return l;
                }
                if (name && *name)
                {
                    addrinfo hints{};
                    hints.ai_family = AF_INET;
                    hints.ai_socktype = SOCK_STREAM;
                    addrinfo* result = nullptr;
                    if (getaddrinfo(name, nullptr, &hints, &result) == 0 && result)
                    {
                        l.address = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr.s_addr;
                        l.found = true;
                    }
                    else
                        fprintf(stderr, "[online] lobby server \"%s\" not found; using 127.0.0.1\n", name);
                    if (result)
                        freeaddrinfo(result);
                }
                fprintf(stderr, "[online] lobby server at %s%s\n", Text(l.address).c_str(), l.found ? "" : " (none set)");
                return l;
            }();
            return lobby;
        }
    }

    bool HasLobbyServer() { return GetLobby().found; }

    std::string ChosenServer()
    {
        switch (settings::OnlineServer(int32_t(settings::GetStoredValue(settings::Id::OnlineServer))))
        {
        case settings::OnlineServer::Community: return kCommunityServer;
        case settings::OnlineServer::Own: return settings::GetText(settings::TextId::ServerAddress);
        case settings::OnlineServer::Lan: return "";
        }
        return "";
    }

    namespace
    {
        // A TCP connection to `address`:`port` within `timeoutMs`: the
        // socket, or -1 with `error` set.
        int ConnectWithin(uint32_t address, uint16_t port, int timeoutMs, std::string& error)
        {
            const int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0)
            {
                error = strerror(errno);
                return -1;
            }
            const int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
            sockaddr_in to{};
            to.sin_len = sizeof(to);
            to.sin_family = AF_INET;
            to.sin_port = htons(port);
            to.sin_addr.s_addr = address;
            if (connect(fd, reinterpret_cast<sockaddr*>(&to), sizeof(to)) < 0 && errno != EINPROGRESS)
            {
                error = strerror(errno);
                close(fd);
                return -1;
            }
            pollfd wait{ fd, POLLOUT, 0 };
            int code = 0;
            socklen_t length = sizeof(code);
            if (poll(&wait, 1, timeoutMs) <= 0)
                error = "no answer in time";
            else if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &code, &length) == 0 && code != 0)
                error = strerror(code);
            else
                return fd;
            close(fd);
            return -1;
        }
    }

    CheckResult CheckServer(const std::string& host)
    {
        CheckResult result;
        if (host.empty())
        {
            result.text = locale::Text("No server to check: the LAN needs none, and My Server needs an address.");
            return result;
        }
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* found = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0 || !found)
        {
            result.text = locale::Format("\"{}\" wasn't found: check the address and the network.", host);
            return result;
        }
        const uint32_t address = reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr.s_addr;
        freeaddrinfo(found);
        std::string error;
        const int fd = ConnectWithin(address, 31860, 5000, error);
        if (fd < 0)
        {
            result.text = locale::Format("{} ({}) doesn't answer on port 31860: {}.", host, Text(address), error);
            return result;
        }
        // @dir as the game sends it: a 12-byte header (command, code, length
        // big-endian) and KEY=VALUE lines ending in a NUL.
        const std::string body = "VERS=Burnout2006/02\nSKU=xbl2\n";
        std::string packet = "@dir";
        packet.append(4, '\0');
        const uint32_t total = uint32_t(12 + body.size() + 1);
        for (int shift = 24; shift >= 0; shift -= 8)
            packet += char((total >> shift) & 0xFF);
        packet += body;
        packet += '\0';
        std::string reply;
        if (send(fd, packet.data(), packet.size(), 0) == ssize_t(packet.size()))
        {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < until)
            {
                pollfd wait{ fd, POLLIN, 0 };
                if (poll(&wait, 1, 250) <= 0)
                    continue;
                char chunk[2048];
                const ssize_t got = recv(fd, chunk, sizeof(chunk), 0);
                if (got <= 0)
                    break;
                reply.append(chunk, size_t(got));
                if (reply.size() >= 12)
                {
                    const uint32_t length = uint32_t(uint8_t(reply[8])) << 24 | uint32_t(uint8_t(reply[9])) << 16 |
                                            uint32_t(uint8_t(reply[10])) << 8 | uint8_t(reply[11]);
                    if (reply.size() >= length)
                        break;
                }
            }
        }
        close(fd);
        if (reply.size() < 12 || reply.compare(0, 4, "@dir") != 0)
        {
            result.text = locale::Format("{} answered, but not as a Burnout Revenge lobby server does.", host);
            return result;
        }
        auto field = [&](const char* key) {
            const std::string k = std::string(key) + "=";
            size_t at = reply.find(k, 12);
            if (at == std::string::npos)
                return std::string();
            at += k.size();
            const size_t end = reply.find_first_of(std::string("\n\t|\0", 4), at);
            return reply.substr(at, end == std::string::npos ? std::string::npos : end - at);
        };
        const int locker = ConnectWithin(address, 31862, 3000, error);
        if (locker >= 0)
            close(locker);
        result.ok = true;
        result.text = locale::Format("Server found: its lobby is at {}:{}. Burnout Clips: {}", field("ADDR"), field("PORT"),
                      locale::Text(locker >= 0 ? "yes." : "no (no Locker on port 31862)."));
        return result;
    }

    namespace
    {
        struct Retry
        {
            std::mutex mutex;
            std::chrono::steady_clock::time_point last;
            bool unconfirmed = false;  // the last login allowed hasn't reached the lobby
            int failures = 0;
        };
        Retry& RetryState()
        {
            static Retry retry;
            return retry;
        }
    }

    bool LobbyConnectAllowed(uint32_t address, uint16_t port, double* waitSeconds)
    {
        if (port != 31860 || !HasLobbyServer() || address != LobbyServerAddress())
            return true;
        Retry& r = RetryState();
        std::lock_guard lock(r.mutex);
        const auto now = std::chrono::steady_clock::now();
        if (r.unconfirmed)
        {
            const double pause = std::min(60.0, 5.0 * double(1 << std::min(r.failures, 4)));
            const double since = std::chrono::duration<double>(now - r.last).count();
            if (since < pause)
            {
                if (waitSeconds)
                    *waitSeconds = pause - since;
                return false;
            }
            r.failures++;
        }
        r.last = now;
        r.unconfirmed = true;
        return true;
    }

    void NoteNetworkError()
    {
        Retry& r = RetryState();
        std::lock_guard lock(r.mutex);
        if (r.unconfirmed)
            r.last = std::chrono::steady_clock::now();
    }

    void NoteLobbyLogin()
    {
        Retry& r = RetryState();
        std::lock_guard lock(r.mutex);
        r.unconfirmed = false;
        r.failures = 0;
    }
    bool OwnLobby() { return GetLobby().own; }
    uint32_t LobbyServerAddress() { return GetLobby().address; }

    uint32_t BindAddress()
    {
        static const uint32_t address = Configured("BURNOUT_ONLINE_ADDRESS");
        return address;
    }

    uint32_t LocalAddress()
    {
        static const uint32_t address = [] {
            const uint32_t found = BindAddress() ? BindAddress() : LanAddress();
            fprintf(stderr, "[online] this console's address is %s\n", Text(found).c_str());
            return found;
        }();
        return address;
    }
    const std::string& Gamertag() { return Get().gamertag; }
    uint64_t Xuid() { return Get().xuid; }

    void FillXnAddr(uint8_t* x, uint32_t address)
    {
        memset(x, 0, 36);
        memcpy(x, &address, 4);
        memcpy(x + 4, &address, 4);
        x[8] = 3074 >> 8, x[9] = 3074 & 0xFF;
        const uint8_t* ip = reinterpret_cast<const uint8_t*>(&address);
        x[10] = 0x02, x[11] = 0x58;  // locally administered
        memcpy(x + 12, ip, 4);
        for (uint8_t i = 0; i < 20; i++)
            x[16 + i] = uint8_t(0x5A + i) ^ ip[i % 4];
    }

    void FillHostSessionInfo(uint8_t* info, uint8_t* nonce)
    {
        static std::mutex mutex;
        static std::mt19937_64 random{ std::random_device{}() };
        std::lock_guard lock(mutex);
        if (info)
        {
            for (int i = 0; i < 8; i++)
                info[i] = uint8_t(random());
            info[0] = uint8_t((info[0] & 0x0F) | 0x80);  // XNKID: an online peer-to-peer session
            FillXnAddr(info + 8, LocalAddress());
            for (int i = 0; i < 16; i++)
                info[44 + i] = uint8_t(random());
        }
        if (nonce)
            for (int i = 0; i < 8; i++)
                nonce[i] = uint8_t(random());
    }
}
