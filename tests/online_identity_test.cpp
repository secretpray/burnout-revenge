// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Who this console is online: the gamertag and its XUID (user/online_identity.h).
#include <user/online_identity.h>

#include <cassert>
#include <cstdio>

using namespace online;

int main()
{
    // FNV-1a as the lobby server reckons a player's id (Xerenge's ealobby:
    // PlayerId is this with the top bit cleared; OPID0=2062879033 for
    // "BRMacProbeA" in the 19.10 transcript).
    static_assert(Fnv1a("") == 2166136261u);
    assert((Fnv1a("BRMacProbeA") & 0x7FFFFFFFu) == 2062879033u);
    assert((Fnv1a("BRMacProbeB") & 0x7FFFFFFFu) == 2012546176u);

    // Each gamertag its own XUID, never the offline one, with the offline top half.
    static_assert(XuidFor("Player") != kOfflineXuid);
    assert(XuidFor("BRMacProbeA") != XuidFor("BRMacProbeB"));
    assert((XuidFor("Player") >> 32) == (kOfflineXuid >> 32));
    assert((XuidFor("Player") & 0x80000000u) != 0);
    assert(XuidFor("Player") == XuidFor("Player"));

    // Gamertags: letters, digits, '-' and '_', at most 15; nothing left is the default.
    assert(SanitizeGamertag("BRMacProbeA") == "BRMacProbeA");
    assert(SanitizeGamertag("Mac Player!") == "MacPlayer");
    assert(SanitizeGamertag("a_very-long_gamertag_name") == "a_very-long_gam");
    assert(SanitizeGamertag("") == kDefaultGamertag);
    assert(SanitizeGamertag("!!!") == kDefaultGamertag);
    // The default gamertag: Player and four digits, the same for one
    // account, a valid gamertag, and not the account's name.
    assert(DefaultGamertagFor("alice") == DefaultGamertagFor("alice"));
    assert(DefaultGamertagFor("alice").size() == 10 && DefaultGamertagFor("alice").rfind("Player", 0) == 0);
    assert(SanitizeGamertag(DefaultGamertagFor("alice")) == DefaultGamertagFor("alice"));
    assert(DefaultGamertagFor("alice").find("alice") == std::string::npos);
    assert(DefaultGamertagFor("alice") != DefaultGamertagFor("bob"));
    puts("online_identity_test: gamertags and their XUIDs");
}
