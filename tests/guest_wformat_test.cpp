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
    check(u"%f", {}, u"", false);
    check(u"%99999d", {}, u"", false);
    check(u"%", {}, u"", false);
}
