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
        };

        const Lobby& GetLobby()
        {
            static const Lobby lobby = [] {
                Lobby l;
                const char* name = std::getenv("BURNOUT_LOBBY_SERVER");
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
}
