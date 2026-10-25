// Burnout Revenge tests. GPL-3.0-or-later (see COPYING).
#include <hid/rumble_notice.h>
#include <user/settings.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

int main(int argc, char** argv)
{
    using N = hid::RumbleNotice;
    N::Device d{true, 'm', N::Connection::Unknown, 0x045e, "Controller"};
    assert(N::Affected(d));
    d.connection = N::Connection::Wired; assert(N::Affected(d));
    d.connection = N::Connection::Wireless; assert(!N::Affected(d));
    d.connection = N::Connection::Unknown;
    d.mac = false; assert(!N::Affected(d)); d.mac = true;
    d.driver = 'h'; assert(!N::Affected(d)); d.driver = 'm';
    d.vendor = 0x054c; d.name = "DualSense"; assert(!N::Affected(d));
    d.name = "Licensed XbOx pad"; assert(N::Affected(d));
    for (int mask = 0; mask < 64; ++mask)
    {
        const bool affected=mask&1,vibration=mask&2,dismissed=mask&4,safe=mask&8,movie=mask&16,panel=mask&32;
        assert(N::ShouldShow(affected,vibration,dismissed,safe,movie,panel) ==
            (affected && vibration && !dismissed && safe && !movie && !panel));
    }
    const auto root = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(root);
    const auto file = root / "settings.toml";
    std::ofstream(file) << "# keep this comment\n[input]\nvibration = false\n[app]\nlast_version = \"0.1.24\"\nrefresh_notices = \"144\"\n";
    assert(settings::Load(file) && !settings::XboxUsbRumbleNoticeShown());
    assert(settings::RecordXboxUsbRumbleNotice());
    assert(settings::Load(file) && settings::XboxUsbRumbleNoticeShown());
    assert(settings::RefreshNoticeShown(144));
    assert(!settings::GetBool(settings::Id::Vibration));
    assert(settings::RecordVersion("0.1.25") && settings::RecordRefreshNotice(165) && settings::Save());
    assert(settings::Load(file) && settings::XboxUsbRumbleNoticeShown() && settings::RefreshNoticeShown(165));
    std::ofstream(file) << "[app]\nxbox_usb_rumble_notice = false\n";
    assert(settings::Load(file) && !settings::XboxUsbRumbleNoticeShown());
    std::filesystem::remove_all(root);
}
