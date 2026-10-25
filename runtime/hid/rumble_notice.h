// Burnout Revenge runtime. GPL-3.0-or-later (see COPYING).
// SDL GUID driver classification follows SpeedBreaker v0.1.9 (57b0c69).
#pragma once
#include <cctype>
#include <cstdint>
#include <string_view>

namespace hid
{
    struct RumbleNotice
    {
        enum class Connection { Unknown, Wired, Wireless };
        struct Device
        {
            bool mac = false;
            char driver = 0;
            Connection connection = Connection::Unknown;
            uint16_t vendor = 0;
            std::string_view name;
        };
        static bool Affected(const Device& d)
        {
            if (!d.mac || d.driver != 'm' || d.connection == Connection::Wireless) return false;
            if (d.vendor == 0x045e) return true;
            constexpr std::string_view xbox = "xbox";
            for (size_t i = 0; i + xbox.size() <= d.name.size(); ++i)
            {
                size_t n = 0;
                while (n < xbox.size() && std::tolower(static_cast<unsigned char>(d.name[i+n])) == xbox[n]) ++n;
                if (n == xbox.size()) return true;
            }
            return false;
        }
        static bool ShouldShow(bool affected, bool vibration, bool dismissed, bool safeMenu, bool movie, bool panel)
        { return affected && vibration && !dismissed && safeMenu && !movie && !panel; }
        static constexpr const char* kTitle = "Xbox USB vibration";
        static constexpr const char* kText =
            "An Xbox controller is using Apple's GameController connection over USB. This connection may accept vibration requests without moving the motors. Try pairing the controller over Bluetooth for vibration.";
    };
}
