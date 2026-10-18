// Burnout Revenge port. GPL-3.0-or-later (see COPYING). See virtual_wheel.h.
#include <stdafx.h>
#include "virtual_wheel.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace hid::virtual_wheel
{
    namespace
    {
        // A G29 as it reports itself over USB (hid/wheel_map.h's table).
        constexpr Uint16 kVendor = 0x046d, kProduct = 0xc24f;
        constexpr int kAxes = 6, kButtons = 25;
        constexpr int kSteerAxis = 0, kClutchAxis = 1, kThrottleAxis = 2, kBrakeAxis = 3;
        constexpr Sint16 kPedalUp = 32767, kPedalDown = -32768;

        enum class Kind { Axis, Button, Hat };
        struct Item
        {
            double time;
            Kind kind;
            int index;
            int value;  // axis: raw; button: 0/1; hat: SDL_HAT_*
            std::string text;
        };
        std::vector<Item> s_items;
        size_t s_next = 0;
        SDL_Joystick* s_device = nullptr;

        Sint16 Pedal(double fraction)
        {
            const double f = std::clamp(fraction, 0.0, 1.0);
            return Sint16(std::lround(double(kPedalUp) + (double(kPedalDown) - double(kPedalUp)) * f));
        }

        bool SDLCALL Rumble(void*, Uint16 low, Uint16 high)
        {
            fprintf(stderr, "[wheel] virtual wheel: rumble low %u, high %u\n", unsigned(low), unsigned(high));
            return true;
        }

        void Parse(std::string_view text)
        {
            while (!text.empty())
            {
                const size_t comma = text.find(',');
                const std::string item(text.substr(0, comma));
                text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
                const size_t colon = item.find(':');
                if (colon == std::string::npos)
                    continue;  // "1": just the wheel
                const double at = std::atof(item.c_str());
                const std::string what = item.substr(colon + 1);
                const size_t equals = what.find('=');
                const std::string name = what.substr(0, equals);
                const std::string arg = equals == std::string::npos ? "" : what.substr(equals + 1);
                const double v = std::atof(arg.c_str());
                if (name == "STEER")
                    s_items.push_back({ at, Kind::Axis, kSteerAxis, int(std::lround(std::clamp(v, -1.0, 1.0) * 32767.0)), what });
                else if (name == "GAS")
                    s_items.push_back({ at, Kind::Axis, kThrottleAxis, Pedal(v), what });
                else if (name == "BRAKE")
                    s_items.push_back({ at, Kind::Axis, kBrakeAxis, Pedal(v), what });
                else if (name == "CLUTCH")
                    s_items.push_back({ at, Kind::Axis, kClutchAxis, Pedal(v), what });
                else if (name == "HAT")
                {
                    const int hat = arg == "UP" ? SDL_HAT_UP : arg == "DOWN" ? SDL_HAT_DOWN : arg == "LEFT" ? SDL_HAT_LEFT
                        : arg == "RIGHT" ? SDL_HAT_RIGHT : SDL_HAT_CENTERED;
                    s_items.push_back({ at, Kind::Hat, 0, hat, what });
                }
                else if (name.size() > 1 && (name[0] == 'A' || name[0] == 'B') && std::isdigit(static_cast<unsigned char>(name[1])))
                {
                    const int index = std::atoi(name.c_str() + 1);
                    if (name[0] == 'A' && index < kAxes && equals != std::string::npos)
                        s_items.push_back({ at, Kind::Axis, index, std::clamp(int(v), -32768, 32767), what });
                    else if (name[0] == 'B' && index < kButtons)
                    {
                        if (equals == std::string::npos)
                        {
                            s_items.push_back({ at, Kind::Button, index, 1, name + " down" });
                            s_items.push_back({ at + 0.15, Kind::Button, index, 0, name + " up" });
                        }
                        else
                            s_items.push_back({ at, Kind::Button, index, v != 0.0, what });
                    }
                    else
                        fprintf(stderr, "[wheel] BURNOUT_VIRTUAL_WHEEL: can't read \"%s\"\n", item.c_str());
                }
                else
                    fprintf(stderr, "[wheel] BURNOUT_VIRTUAL_WHEEL: can't read \"%s\"\n", item.c_str());
            }
            std::stable_sort(s_items.begin(), s_items.end(), [](const Item& a, const Item& b) { return a.time < b.time; });
        }
    }

    bool Attach()
    {
        const char* v = std::getenv("BURNOUT_VIRTUAL_WHEEL");
        if (!v || !*v)
            return false;
        // A test's window rarely has the focus, and SDL drops joystick
        // changes without it (as for BURNOUT_VIRTUAL_PAD).
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        Parse(v);
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_WHEEL;
        desc.vendor_id = kVendor;
        desc.product_id = kProduct;
        desc.naxes = kAxes;
        desc.nbuttons = kButtons;
        desc.nhats = 1;
        desc.name = "BurnoutRevenge virtual wheel (G29)";
        desc.Rumble = Rumble;
        const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        s_device = id ? SDL_OpenJoystick(id) : nullptr;
        if (!s_device)
        {
            fprintf(stderr, "[wheel] BURNOUT_VIRTUAL_WHEEL: no virtual wheel: %s\n", SDL_GetError());
            return false;
        }
        // At rest as a G29 is: centred, the pedals up.
        SDL_SetJoystickVirtualAxis(s_device, kSteerAxis, 0);
        for (int axis : { kClutchAxis, kThrottleAxis, kBrakeAxis })
            SDL_SetJoystickVirtualAxis(s_device, axis, kPedalUp);
        SDL_UpdateJoysticks();
        fprintf(stderr, "[wheel] virtual wheel attached (046d:c24f), %zu steps\n", s_items.size());
        return true;
    }

    std::string Step(double seconds)
    {
        if (!s_device || s_next >= s_items.size())
            return {};
        std::string taken;
        for (; s_next < s_items.size() && s_items[s_next].time <= seconds; s_next++)
        {
            const Item& item = s_items[s_next];
            switch (item.kind)
            {
            case Kind::Axis: SDL_SetJoystickVirtualAxis(s_device, item.index, Sint16(item.value)); break;
            case Kind::Button: SDL_SetJoystickVirtualButton(s_device, item.index, item.value != 0); break;
            case Kind::Hat: SDL_SetJoystickVirtualHat(s_device, item.index, Uint8(item.value)); break;
            }
            taken += (taken.empty() ? "" : " ") + item.text;
        }
        if (!taken.empty())
            SDL_UpdateJoysticks();
        return taken;
    }
}
