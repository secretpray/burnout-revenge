// Burnout Revenge port. GPL-3.0-or-later (see COPYING). See online.h.
#include "online.h"
#include "online_identity.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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

        const Identity& Get()
        {
            static const Identity identity = [] {
                Identity id;
                const char* on = std::getenv("BURNOUT_ONLINE");
                id.enabled = on && *on && std::strcmp(on, "0") != 0;
                if (!id.enabled)
                    return id;
                const char* tag = std::getenv("BURNOUT_GAMERTAG");
                id.gamertag = SanitizeGamertag(tag ? tag : "");
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
                const char* name = std::getenv("BURNOUT_LOBBY_SERVER");
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
