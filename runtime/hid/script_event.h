// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The game's events an automated run's BURNOUT_INPUT_SCRIPT counts from
// ("@online+5:A", hid/input.h). Apart from input.h, which needs SDL, for the
// kernel's code that reports them.
#pragma once

namespace hid
{
    // Any thread: `name` happened ("menu", "online", "session", "race",
    // "raceend"). Script inputs given as "@name+5:A" count from its latest time.
    void ScriptEvent(const char* name);
}
