// Burnout Revenge port. GPL-3.0-or-later (see COPYING). See online.h.
#include "online.h"
#include "online_identity.h"

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
                fprintf(stderr, "[online] signed in to Xbox Live as \"%s\" (XUID %016llX); nothing is contacted yet\n",
                    id.gamertag.c_str(), (unsigned long long)id.xuid);
                return id;
            }();
            return identity;
        }
    }

    bool Enabled() { return Get().enabled; }
    const std::string& Gamertag() { return Get().gamertag; }
    uint64_t Xuid() { return Get().xuid; }
}
