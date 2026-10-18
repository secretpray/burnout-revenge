// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
//
// BURNOUT_VIRTUAL_WHEEL: an SDL virtual joystick that looks like a Logitech
// G29 (046d:c24f, SDL_JOYSTICK_TYPE_WHEEL, 6 axes, 25 buttons, a hat), for
// testing hid/wheel.cpp and Settings > Wheel without a wheel. It goes
// through the same SDL joystick path a real one does, so hid/wheel.cpp finds
// it, opens it and reads it as it would the real thing.
//
//   BURNOUT_VIRTUAL_WHEEL="1"                          the wheel, at rest
//   BURNOUT_VIRTUAL_WHEEL="20:STEER=-0.5,22:GAS=1,24:B0,26:B4=1,27:B4=0,28:HAT=UP"
//
// Steps at seconds since launch, each holding until the next one for the
// same control: STEER -1 (full left) to 1, GAS / BRAKE / CLUTCH 0 (up) to 1
// (floored), written as a G29 reports them (axis 0 the wheel, 2 the
// throttle, 3 the brake, 1 the clutch; a pedal rests at +32767 and reads
// -32768 floored); An=<raw> any axis raw; Bn taps button n (0.15 s),
// Bn=1 / Bn=0 hold and release it; HAT=UP/DOWN/LEFT/RIGHT/CENTRE.
// Its force feedback is logged, not felt: SDL's virtual joysticks have no
// haptics, so hid/wheel.cpp says the wheel can't be driven, as on a Mac.
#pragma once
#include <string>

namespace hid::virtual_wheel
{
    // Main thread, after SDL's joystick subsystem is up and before
    // hid::wheel::Initialize. True when the variable is set and the device
    // is attached.
    bool Attach();
    // Main thread, once a frame: the steps due by `seconds` since launch.
    // Returns what was done ("" if nothing), for the log.
    std::string Step(double seconds);
}
