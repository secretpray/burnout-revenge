// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include "runtime/kernel/guest_wformat.h"
#include <cstdlib>
#include <vector>

int main()
{
    auto check = [](std::u16string format, std::vector<uint64_t> args, std::u16string expected, bool success = true)
    {
        size_t index = 0;
        std::u16string result;
        bool ok = guestformat::Wide(format, [&] { if (index == args.size()) std::abort(); return args[index++]; },
            [](uint32_t address, bool narrow) { return address == 7 ? (narrow ? u"ASCII" : u"Профиль") : u"(null)"; }, result);
        if (ok != success || (ok && result != expected)) std::abort();
    };
    check(u"GAME:\\eatrax\\%02d.wma", {0}, u"GAME:\\eatrax\\00.wma");
    check(u"GAME:\\eatrax\\%02d.wma", {40}, u"GAME:\\eatrax\\40.wma");
    check(u"%d %u %08X", {0xffffffff, 0xffffffff, 0x80000000}, u"-1 4294967295 80000000");
    check(u"%I64u %lld", {0xffffffffffffffffULL, 0xffffffffffffffffULL}, u"18446744073709551615 -1");
    check(u"%s/%S/%hs/%%", {7, 7, 7}, u"Профиль/ASCII/ASCII/%");
    check(u"%*.*s", {9, 3, 7}, u"      Про");
    check(u"%n", {}, u"", false);
    check(u"%99999d", {}, u"", false);
    check(u"%", {}, u"", false);
    check(u"%.2f|%5.1f|%g|%e", {0x4000000000000000ULL, 0x400921FB54442D18ULL, 0x3FF8000000000000ULL, 0x4059000000000000ULL},
        u"2.00|  3.1|1.5|1.000000e+02");
    check(u"%p", {0x82A532CC}, u"82A532CC");

    // The narrow functions: %s is a byte string, %ls and %S wide.
    auto narrow = [](std::string format, std::vector<uint64_t> args, std::string expected, bool success = true)
    {
        size_t index = 0;
        std::string result;
        bool ok = guestformat::Narrow(format, [&] { if (index == args.size()) std::abort(); return args[index++]; },
            [](uint32_t address, bool narrowString) { return address == 7 ? (narrowString ? u"ASCII" : u"Wide\u00e9\u0416") : u"(null)"; }, result);
        if (ok != success || (ok && result != expected)) std::abort();
    };
    narrow("%s-%ls-%S-%hs", {7, 7, 7, 7}, "ASCII-Wide\xe9?-Wide\xe9?-ASCII");
    narrow("track %02d: %.1f s", {5, 0x4024000000000000ULL}, "track 05: 10.0 s");
    narrow("%n", {}, "", false);
}
