// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See input.h.
// A steering wheel's own side of this is hid/wheel.cpp.
#include <stdafx.h>
#include "input.h"
#include "key_map.h"
#include "rewind_shortcuts.h"
#include "pad_slots.h"
#include "rumble_focus.h"
#include "rumble_notice.h"
#include <game/movies.h>
#include "virtual_wheel.h"
#include "wheel.h"
#include <cpu/guest_time.h>
#include <kernel/function.h>
#include <ui/ui.h>
#include <user/settings.h>
#include <user/key_names.h>
#include <user/localization.h>
#include <user/music_index.h>

#include <SDL3/SDL.h>
#include <fstream>

// key_map.h names XInput's bits as literals (it has no kernel header): keep
// them the bits the game reads.
static_assert(hid::keymap::button::DpadUp == XAMINPUT_GAMEPAD_DPAD_UP && hid::keymap::button::DpadDown == XAMINPUT_GAMEPAD_DPAD_DOWN &&
    hid::keymap::button::DpadLeft == XAMINPUT_GAMEPAD_DPAD_LEFT && hid::keymap::button::DpadRight == XAMINPUT_GAMEPAD_DPAD_RIGHT &&
    hid::keymap::button::Start == XAMINPUT_GAMEPAD_START && hid::keymap::button::Back == XAMINPUT_GAMEPAD_BACK &&
    hid::keymap::button::LeftThumb == XAMINPUT_GAMEPAD_LEFT_THUMB && hid::keymap::button::RightThumb == XAMINPUT_GAMEPAD_RIGHT_THUMB &&
    hid::keymap::button::LeftShoulder == XAMINPUT_GAMEPAD_LEFT_SHOULDER && hid::keymap::button::RightShoulder == XAMINPUT_GAMEPAD_RIGHT_SHOULDER &&
    hid::keymap::button::A == XAMINPUT_GAMEPAD_A && hid::keymap::button::B == XAMINPUT_GAMEPAD_B &&
    hid::keymap::button::X == XAMINPUT_GAMEPAD_X && hid::keymap::button::Y == XAMINPUT_GAMEPAD_Y,
    "key_map.h's button bits are XInput's");
// The key codes of user/key_names.h are SDL's scancodes (USB HID usages);
// tests/key_names_test.cpp checks every row, this pins a few.
static_assert(settings::keynames::Code("a") == SDL_SCANCODE_A && settings::keynames::Code("keypad_1") == SDL_SCANCODE_KP_1 &&
              settings::keynames::Code("right_alt") == SDL_SCANCODE_RALT && settings::keynames::Code("escape") == SDL_SCANCODE_ESCAPE);

namespace
{
    std::atomic<uint16_t> s_rewindConsumed{0};
    constexpr uint32_t ERROR_SUCCESS = 0;
    constexpr uint32_t ERROR_DEVICE_NOT_CONNECTED = 0x48F;

    struct Pad
    {
        uint16_t buttons = 0;
        uint8_t leftTrigger = 0, rightTrigger = 0;
        int16_t lx = 0, ly = 0, rx = 0, ry = 0;
    };

    // XInput users 0 to 3, one player each (hid/pad_slots.h): user 0 has the
    // keyboard, the wheel and the scripted input besides its controller.
    constexpr unsigned kUsers = hid::padslots::kSlots;

    std::mutex s_mutex;
    Pad s_pads[kUsers];               // latest sample per user (devices only)
    bool s_connected[kUsers] = {};    // guest visibility, including physical P1 loss in a local session
    uint32_t s_packets[kUsers] = {};
    struct RumbleRequest { uint16_t low = 0, high = 0; bool dirty = false; };
    RumbleRequest s_rumble[kUsers];

    // Main thread: each user's controller, and the SDL ids they are.
    SDL_Gamepad* s_gamepads[kUsers] = {};
    hid::padslots::Assignments s_assignments;
    auto& s_slots = s_assignments.ids;
    std::atomic<unsigned> s_localPlayers{0};
    std::atomic<bool> s_frontendMenu{false};
    bool s_keyboardAllowed = true; // guarded with the published XInput state
    bool s_neutralRequired[kUsers] = {};
    struct PendingPad { SDL_Gamepad* handle{}; bool start = true; };
    std::map<int64_t, PendingPad> s_pendingPads;
    void ResetKeystrokesLocked(unsigned user);
    hid::RumbleFocus s_rumbleFocus;
    bool s_rumbleErrorReported[kUsers] = {};

    void SendRumble(unsigned user, uint16_t low, uint16_t high)
    {
        if (!s_gamepads[user])
            return;
        if (SDL_RumbleGamepad(s_gamepads[user], low, high, low || high ? 5000 : 0))
            s_rumbleErrorReported[user] = false;
        else if (!s_rumbleErrorReported[user])
        {
            fprintf(stderr, "[input] player %u's controller rumble failed: %s\n", user + 1, SDL_GetError());
            s_rumbleErrorReported[user] = true;
        }
    }

    void ApplyRumbleActivity(hid::RumbleFocus::Change change)
    {
        if (change == hid::RumbleFocus::Change::Stop)
        {
            for (unsigned user = 0; user < kUsers; user++)
                SendRumble(user, 0, 0);
        }
        else if (change == hid::RumbleFocus::Change::Restore)
        {
            std::lock_guard lock(s_mutex);
            for (RumbleRequest& r : s_rumble)
                r.dirty = true; // the latest request, not a stale focus-time value
        }
    }
    std::atomic<bool> s_blocked{ false };
    // Main thread: buttons held when a menu closed, hidden from the game
    // until each is released (the B, Start or A that closed it). Per user.
    uint16_t s_latchedButtons[kUsers] = {};
    bool s_wasBlocked = false;
    bool s_scriptSuppressed = false;  // under s_mutex: no scripted input while a menu is open
    std::chrono::steady_clock::time_point s_lastInput;  // main thread: a control last in use (SecondsSinceInput)
    bool s_inputSeen = false;

    // A controller's Back is held back from the game until it's clear it
    // isn't the start of the Back + Start settings chord: released (then
    // replayed as a short tap), or held 250 ms without Start. Main thread,
    // one per user. In guest time, so a suspension can't eat the replayed tap.
    enum class BackState { Idle, Pending, Passing, Replay, Swallow };
    struct BackChord
    {
        BackState state = BackState::Idle;
        int64_t since = 0;  // guest ns

        bool ForGame(bool back, bool start)
        {
            int64_t now = guesttime::NowNs();
            switch (state)
            {
            case BackState::Idle:
                if (back)
                {
                    state = start ? BackState::Swallow : BackState::Pending;
                    since = now;
                }
                return false;
            case BackState::Pending:
                if (start)
                {
                    state = BackState::Swallow;  // the chord: the UI has it
                    return false;
                }
                if (!back)
                {
                    state = BackState::Replay;
                    since = now;
                    return true;
                }
                if (now - since >= 250'000'000)
                {
                    state = BackState::Passing;
                    return true;
                }
                return false;
            case BackState::Passing:
                if (!back)
                    state = BackState::Idle;
                return back;
            case BackState::Replay:
                if (now - since < 100'000'000)
                    return true;
                state = BackState::Idle;
                return false;
            case BackState::Swallow:
                if (!back && !start)
                    state = BackState::Idle;
                return false;
            }
            return false;
        }
    };
    BackChord s_back[kUsers];

    // A scripted input: buttons (or analog: RT/LT triggers, LL/LR/LU/LD left
    // stick) held over [time, time + duration).
    // `anchor`: an event's index in s_anchors (the time counts from its
    // latest ScriptEvent, and the input waits for it), or -1 (from launch).
    // `until`: an event that, once it comes after the anchor's, cancels the
    // input ("!race"), or -1.
    // `polls`: how many of the game's reads have seen it - a tap lasts until
    // three have, however long a frame takes (with several copies running
    // one can take a second, and a tap would fall between two reads).
    struct ScriptTap { double time; uint16_t buttons; int16_t lx, ly; double duration = 0.15; uint8_t lt = 0, rt = 0; int anchor = -1; int polls = 0; double base = 0.0; int until = -1; bool live = false; };
    std::vector<ScriptTap> s_script;
    // The events the script names ("@online+3:B") and when each last came
    // (seconds since launch, -1 not yet). Names are fixed once parsed; the
    // times are set from any thread.
    std::vector<std::string> s_anchors;
    std::vector<double> s_anchorTimes;
    std::mutex s_anchorMutex;
    // Guest ns at launch: the script and the virtual pad run on guest time,
    // so a press due while the game is suspended comes after it instead of
    // being lost.
    const int64_t s_start = guesttime::NowNs();

    double SecondsSinceLaunch()
    {
        return double(guesttime::NowNs() - s_start) / 1e9;
    }

    int16_t ToShort(float v)
    {
        return int16_t(std::clamp(v * 32767.0f, -32768.0f, 32767.0f));
    }

    // Radial deadzone with rescale: |v| < dz -> 0, else (|v|-dz)/(1-dz).
    void ApplyDeadzone(float& x, float& y, float dz)
    {
        float m = std::sqrt(x * x + y * y);
        if (m <= dz)
        {
            x = y = 0.0f;
            return;
        }
        float scaled = std::min(1.0f, (m - dz) / (1.0f - dz));
        x = x / m * scaled;
        y = y / m * scaled;
    }

    uint8_t Trigger(float v, float dz)
    {
        if (v <= dz)
            return 0;
        return uint8_t(std::clamp((v - dz) / (1.0f - dz), 0.0f, 1.0f) * 255.0f + 0.5f);
    }

    uint16_t ButtonByName(const std::string& n)
    {
        static const std::pair<const char*, uint16_t> names[] = {
            { "UP", XAMINPUT_GAMEPAD_DPAD_UP }, { "DOWN", XAMINPUT_GAMEPAD_DPAD_DOWN },
            { "LEFT", XAMINPUT_GAMEPAD_DPAD_LEFT }, { "RIGHT", XAMINPUT_GAMEPAD_DPAD_RIGHT },
            { "START", XAMINPUT_GAMEPAD_START }, { "BACK", XAMINPUT_GAMEPAD_BACK },
            { "LS", XAMINPUT_GAMEPAD_LEFT_THUMB }, { "RS", XAMINPUT_GAMEPAD_RIGHT_THUMB },
            { "LB", XAMINPUT_GAMEPAD_LEFT_SHOULDER }, { "RB", XAMINPUT_GAMEPAD_RIGHT_SHOULDER },
            { "A", XAMINPUT_GAMEPAD_A }, { "B", XAMINPUT_GAMEPAD_B }, { "X", XAMINPUT_GAMEPAD_X }, { "Y", XAMINPUT_GAMEPAD_Y },
        };
        for (auto& [name, bit] : names)
            if (n == name)
                return bit;
        return 0;
    }

    // One "when:BUTTON" item of the script; `offset` (seconds since launch)
    // is added to its times (a live command counts from when it came).
    void ParseItem(const std::string& item, std::vector<ScriptTap>& out, double offset = 0.0)
    {
        size_t colon = item.find(':');
        if (colon == std::string::npos)
            return;
        // "sec:BUTTON", "from-to/step:BUTTON" (a tap every `step` s) or
        // "from~to:BUTTON" (held).
        std::string when = item.substr(0, colon);
        std::string name = item.substr(colon + 1);
        const auto anchorIndex = [](const std::string& event) {
            auto it = std::find(s_anchors.begin(), s_anchors.end(), event);
            if (it == s_anchors.end())
            {
                s_anchors.push_back(event);
                s_anchorTimes.push_back(-1.0);
                it = s_anchors.end() - 1;
            }
            return int(it - s_anchors.begin());
        };
        int until = -1;
        if (const size_t bang = name.find('!'); bang != std::string::npos)
        {
            until = anchorIndex(name.substr(bang + 1));
            name.resize(bang);
        }
        ScriptTap proto{ 0, ButtonByName(name), 0, 0 };
        proto.until = until;
        // "@event+when": counted from the event (ScriptEvent), not launch.
        if (when.size() > 1 && when[0] == '@')
        {
            const size_t plus = when.find('+');
            const std::string event = when.substr(1, plus == std::string::npos ? std::string::npos : plus - 1);
            when = plus == std::string::npos ? "0" : when.substr(plus + 1);
            proto.anchor = anchorIndex(event);
        }
        if (name == "RT") proto.rt = 255;
        else if (name == "LT") proto.lt = 255;
        else if (name == "LL") proto.lx = -32767;
        else if (name == "LR") proto.lx = 32767;
        else if (name == "LU") proto.ly = 32767;
        else if (name == "LD") proto.ly = -32767;
        else if (proto.buttons == 0)
        {
            fprintf(stderr, "[input] script: unknown button in \"%s\"\n", item.c_str());
            return;
        }
        uint16_t button = proto.buttons;
        if (size_t tilde = when.find('~'); tilde != std::string::npos)
        {
            ScriptTap hold = proto;
            hold.time = std::atof(when.substr(0, tilde).c_str());
            hold.duration = std::atof(when.substr(tilde + 1).c_str()) - hold.time;
            hold.time += offset;
            out.push_back(hold);
            return;
        }
        size_t dash = when.find('-'), slash = when.find('/');
        if (dash != std::string::npos && slash != std::string::npos && slash > dash)
        {
            double from = std::atof(when.substr(0, dash).c_str());
            double to = std::atof(when.substr(dash + 1, slash - dash - 1).c_str());
            double step = std::max(0.2, std::atof(when.substr(slash + 1).c_str()));
            for (double t = from; t <= to; t += step)
            {
                ScriptTap tap = proto;
                tap.time = t + offset;
                out.push_back(tap);
            }
        }
        else
        {
            ScriptTap tap = proto;
            tap.time = std::atof(when.c_str()) + offset;
            out.push_back(tap);
        }
    }

    void ParseScript()
    {
        const char* s = std::getenv("BURNOUT_INPUT_SCRIPT");
        if (!s)
            return;
        std::string all = s;
        size_t pos = 0;
        while (pos < all.size())
        {
            size_t end = all.find(',', pos);
            ParseItem(all.substr(pos, end == std::string::npos ? std::string::npos : end - pos), s_script);
            pos = end == std::string::npos ? all.size() : end + 1;
        }
        fprintf(stderr, "[input] script: %zu taps\n", s_script.size());
    }

    // BURNOUT_INPUT_LIVE=<file>: a test's commands while the game runs, one
    // per line appended to the file: a script item counted from when it is
    // read ("0:A", "0~5:RT"), SHOT (the next frame, ui::RequestShot), CLEAR
    // (lets go of every live input) or "KEY F1" (a key for our menus). For
    // menus whose loading time varies: look at a frame, then press. Main
    // thread (Update); the game's thread reads the script under s_mutex.
    void ReadLive()
    {
        static const char* path = std::getenv("BURNOUT_INPUT_LIVE");
        static double last = -1.0;
        static std::streamoff offset = 0;
        const double now = SecondsSinceLaunch();
        if (!path || now - last < 0.1)
            return;
        last = now;
        std::ifstream file(path);
        if (!file || !file.seekg(offset))
            return;
        std::string line;
        while (std::getline(file, line))
        {
            if (file.eof())
                break;  // a line still being written
            offset = file.tellg();
            if (line == "SHOT")
                ui::RequestShot();
            else if (line.starts_with("KEY "))
            {
                // Our menus' keys (F1, Down, Return): what BURNOUT_UI_KEYS sends.
                const SDL_Keycode key = SDL_GetKeyFromName(line.c_str() + 4);
                // The game's window, focused or not (a test runs in the background).
                int count = 0;
                SDL_Window** windows = SDL_GetWindows(&count);
                SDL_Window* window = count > 0 ? windows[0] : nullptr;
                SDL_free(windows);
                for (bool down : { true, false })
                {
                    SDL_Event e{};
                    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                    e.key.timestamp = SDL_GetTicksNS();
                    e.key.windowID = window ? SDL_GetWindowID(window) : 0;
                    e.key.key = key;
                    e.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
                    e.key.down = down;
                    SDL_PushEvent(&e);
                }
            }
            else if (line == "CLEAR")
            {
                std::lock_guard lock(s_mutex);
                std::erase_if(s_script, [](const ScriptTap& tap) { return tap.live; });
            }
            else if (!line.empty())
            {
                std::lock_guard lock(s_mutex);
                const size_t first = s_script.size();
                ParseItem(line, s_script, now);
                for (size_t i = first; i < s_script.size(); ++i)
                    s_script[i].live = true;
            }
            fprintf(stderr, "[input] live: \"%s\" at %.1f s\n", line.c_str(), now);
        }
    }

    // Apply the scripted inputs active now on top of the device state.
    // `game`: the game's own read (counts toward a tap's polls).
    void ApplyScript(Pad& p, bool game = false)
    {
        if (s_script.empty())
            return;
        const double now = SecondsSinceLaunch();
        std::vector<double> anchors;
        {
            std::lock_guard lock(s_anchorMutex);
            anchors = s_anchorTimes;
        }
        for (auto& tap : s_script)
        {
            if (tap.anchor >= 0 && anchors[tap.anchor] < 0.0)
                continue;
            const double base = tap.anchor >= 0 ? anchors[tap.anchor] : 0.0;
            if (tap.until >= 0 && anchors[tap.until] >= base)
                continue;
            if (base != tap.base)
            {
                tap.base = base;  // the event again: its taps again
                tap.polls = 0;
            }
            const double t = now - base;
            constexpr int kTapPolls = 3;
            const bool held = tap.duration > 0.15;
            if (t >= tap.time && (t < tap.time + tap.duration || (!held && tap.polls < kTapPolls)))
            {
                if (game && !held)
                    tap.polls++;
                p.buttons |= tap.buttons;
                p.leftTrigger = std::max(p.leftTrigger, tap.lt);
                p.rightTrigger = std::max(p.rightTrigger, tap.rt);
                if (tap.lx) p.lx = tap.lx;
                if (tap.ly) p.ly = tap.ly;
            }
        }
    }

    const bool s_scriptParsed = (ParseScript(), true);

    // BURNOUT_VIRTUAL_PAD="2:LX=0.05,4:LX=0.2,6:RT=0.01": an SDL virtual
    // controller, the only one opened, whose axes move to those values at
    // seconds since launch (sticks LX/LY/RX/RY -1..1, up positive; triggers
    // LT/RT 0..1) and hold them. Its buttons too: "5:BACK=1" holds one down,
    // "5.2:BACK=0" lets it go, "6:DOWN" taps it (0.15 s); A B X Y BACK START
    // LB RB LS RS and the D-pad's UP DOWN LEFT RIGHT. Being a real SDL
    // controller, it drives the menus (Back + Start, the D-pad, A, B) as
    // well as the game. A button changes once a frame at most, so a tap due
    // whole within one long frame (a capture's stall) still shows a frame
    // pressed. "12:SHOT" saves the next presented frame (ui::RequestShot), on
    // the pad's clock. BURNOUT_INPUT_SCRIPT feeds the game after the
    // device path; this goes through it, so the deadzones and Vibration
    // reach it as they would a real controller, for tests without one. Each
    // step logs what the game then gets; the rumble the game asks for and
    // the rumble the pad receives are logged. BURNOUT_VIRTUAL_PAD2, the same
    // way, is a second one, attached after it (player 2: hid/pad_slots.h).
    // "20:DETACH,25:ATTACH" reconnects with a new SDL instance ID.
    // PAD3/PAD4 allow spare-controller and both-disconnected tests.
    struct PadStep { double time; SDL_GamepadAxis axis; float value; std::string text; int button = -1; bool down = false; };
    constexpr int kShotStep = -2;  // PadStep::button: a capture, not a button
    constexpr int kAttachStep = -4; // a new SDL instance, never the old identity
    constexpr int kDetachStep = -3;  // PadStep::button: the pad unplugged
    struct VirtualPad
    {
        int number = 0;
        std::vector<PadStep> steps;
        size_t next = 0;
        SDL_JoystickID id = 0;
        SDL_Joystick* joystick = nullptr;
    };
    VirtualPad s_virtualPads[kUsers];
    bool s_anyVirtualPad = false;  // then those are the only controllers used

    bool SDLCALL VirtualPadRumble(void* userdata, Uint16 low, Uint16 high)
    {
        fprintf(stderr, "[input] virtual pad %d: rumble low %u, high %u\n", int(intptr_t(userdata)), unsigned(low), unsigned(high));
        return true;
    }

    void CreateVirtualPad(VirtualPad& pad, int number)
    {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "BurnoutRevenge virtual pad";
        desc.Rumble = VirtualPadRumble;
        desc.userdata = reinterpret_cast<void*>(intptr_t(number));
        pad.id = SDL_AttachVirtualJoystick(&desc);
        pad.joystick = pad.id ? SDL_OpenJoystick(pad.id) : nullptr;
        if (!pad.joystick)
        {
            fprintf(stderr, "[input] virtual pad %d: no virtual controller: %s\n", number, SDL_GetError());
            return;
        }
        s_anyVirtualPad = true;
        fprintf(stderr, "[input] virtual pad %d attached, %zu steps\n", number, pad.steps.size());
    }

    void AttachVirtualPad(int number)
    {
        const std::string variable = number == 1 ? "BURNOUT_VIRTUAL_PAD" : "BURNOUT_VIRTUAL_PAD" + std::to_string(number);
        const char* v = std::getenv(variable.c_str());
        if (!v || !*v)
            return;
        VirtualPad& pad = s_virtualPads[number - 1];
        pad.number = number;
        // A test's window rarely has the keyboard focus (on the Mac it's
        // started from a shell behind other windows), and without it SDL
        // drops stick and trigger changes: the pad kept its first value.
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        static const std::pair<const char*, SDL_GamepadAxis> axes[] = {
            { "LX", SDL_GAMEPAD_AXIS_LEFTX }, { "LY", SDL_GAMEPAD_AXIS_LEFTY }, { "RX", SDL_GAMEPAD_AXIS_RIGHTX },
            { "RY", SDL_GAMEPAD_AXIS_RIGHTY }, { "LT", SDL_GAMEPAD_AXIS_LEFT_TRIGGER }, { "RT", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER },
        };
        for (std::string_view rest = v; !rest.empty();)
        {
            size_t comma = rest.find(',');
            std::string item(rest.substr(0, comma));
            rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
            size_t colon = item.find(':'), equals = item.find('=');
            if (colon == std::string::npos || (equals != std::string::npos && equals < colon))
                continue;
            std::string name = item.substr(colon + 1, equals == std::string::npos ? std::string::npos : equals - colon - 1);
            static const std::pair<const char*, SDL_GamepadButton> buttons[] = {
                { "A", SDL_GAMEPAD_BUTTON_SOUTH }, { "B", SDL_GAMEPAD_BUTTON_EAST }, { "X", SDL_GAMEPAD_BUTTON_WEST },
                { "Y", SDL_GAMEPAD_BUTTON_NORTH }, { "BACK", SDL_GAMEPAD_BUTTON_BACK }, { "START", SDL_GAMEPAD_BUTTON_START },
                { "GUIDE", SDL_GAMEPAD_BUTTON_GUIDE }, { "HOME", SDL_GAMEPAD_BUTTON_GUIDE }, { "PS", SDL_GAMEPAD_BUTTON_GUIDE },
                { "LB", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER }, { "RB", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
                { "LS", SDL_GAMEPAD_BUTTON_LEFT_STICK }, { "RS", SDL_GAMEPAD_BUTTON_RIGHT_STICK },
                { "UP", SDL_GAMEPAD_BUTTON_DPAD_UP }, { "DOWN", SDL_GAMEPAD_BUTTON_DPAD_DOWN },
                { "LEFT", SDL_GAMEPAD_BUTTON_DPAD_LEFT }, { "RIGHT", SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
            };
            if (name == "SHOT" || name == "DETACH" || name == "ATTACH")
            {
                pad.steps.push_back({ std::atof(item.c_str()), SDL_GAMEPAD_AXIS_INVALID, 0.0f, name, name == "SHOT" ? kShotStep : name == "ATTACH" ? kAttachStep : kDetachStep, false });
                continue;
            }
            bool found = false;
            for (auto& [n, button] : buttons)
                if (name == n)
                {
                    const double at = std::atof(item.c_str());
                    if (equals == std::string::npos)
                    {
                        // A tap: down, and up again 0.15 s later.
                        pad.steps.push_back({ at, SDL_GAMEPAD_AXIS_INVALID, 0.0f, name + " down", int(button), true });
                        pad.steps.push_back({ at + 0.15, SDL_GAMEPAD_AXIS_INVALID, 0.0f, name + " up", int(button), false });
                    }
                    else
                    {
                        const bool down = std::atof(item.c_str() + equals + 1) != 0.0;
                        pad.steps.push_back({ at, SDL_GAMEPAD_AXIS_INVALID, 0.0f, name + (down ? " down" : " up"), int(button), down });
                    }
                    found = true;
                }
            if (found)
                continue;
            if (equals == std::string::npos)
            {
                fprintf(stderr, "[input] BURNOUT_VIRTUAL_PAD: unknown button in \"%s\"\n", item.c_str());
                continue;
            }
            for (auto& [n, axis] : axes)
                if (name == n)
                {
                    pad.steps.push_back({ std::atof(item.c_str()), axis, float(std::atof(item.c_str() + equals + 1)), item.substr(colon + 1) });
                    found = true;
                }
            if (!found)
                fprintf(stderr, "[input] BURNOUT_VIRTUAL_PAD: unknown axis in \"%s\"\n", item.c_str());
        }
        std::stable_sort(pad.steps.begin(), pad.steps.end(), [](const PadStep& a, const PadStep& b) { return a.time < b.time; });
        CreateVirtualPad(pad, number);
    }

    // Moves a virtual pad's axes due by now. Returns the steps taken ("" if none).
    std::string StepVirtualPad(VirtualPad& pad)
    {
        if (pad.next >= pad.steps.size())
            return {};
        double t = SecondsSinceLaunch();
        std::string taken;
        uint32_t changed = 0;  // buttons changed this frame
        for (; pad.next < pad.steps.size() && pad.steps[pad.next].time <= t; pad.next++)
        {
            const PadStep& step = pad.steps[pad.next];
            if (step.button == kShotStep)
            {
                ui::RequestShot();
                taken += (taken.empty() ? "" : " ") + step.text;
                continue;
            }
            if (step.button == kAttachStep)
            {
                if (!pad.joystick) CreateVirtualPad(pad, pad.number);
                taken += (taken.empty() ? "" : " ") + step.text;
                continue;
            }
            if (step.button == kDetachStep && pad.joystick)
            {
                // SDL then sends the removal, as for a real one.
                SDL_CloseJoystick(pad.joystick);
                SDL_DetachVirtualJoystick(pad.id);
                pad.joystick = nullptr;
                ++pad.next;
                return taken + (taken.empty() ? "" : " ") + step.text;
            }
            if (!pad.joystick) continue;
            if (step.button >= 0)
            {
                if (changed & (1u << step.button))
                    break;  // its next change waits a frame (the steps stay in order)
                changed |= 1u << step.button;
                SDL_SetJoystickVirtualButton(pad.joystick, step.button, step.down);
                taken += (taken.empty() ? "" : " ") + step.text;
                continue;
            }
            float value = step.value;
            bool trigger = step.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || step.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
            if (step.axis == SDL_GAMEPAD_AXIS_LEFTY || step.axis == SDL_GAMEPAD_AXIS_RIGHTY)
                value = -value;  // SDL: down is positive
            // A virtual gamepad's triggers span the whole axis (-32768 is released).
            float raw = trigger ? std::clamp(value, 0.0f, 1.0f) * 65535.0f - 32768.0f : std::clamp(value, -1.0f, 1.0f) * 32767.0f;
            SDL_SetJoystickVirtualAxis(pad.joystick, int(step.axis), Sint16(std::lround(std::clamp(raw, -32768.0f, 32767.0f))));
            taken += (taken.empty() ? "" : " ") + step.text;
        }
        if (!taken.empty())
            SDL_UpdateJoysticks();  // read back this frame
        return taken;
    }

    // The controllers that may take a player's slot, in the order they
    // connected: the test's own pads when it has any (a real controller
    // nearby must not play or rumble), else every gamepad but a wheel SDL
    // happens to have a gamepad mapping for, which belongs to the wheel:
    // one device, one owner (hid/wheel.h).
    std::vector<int64_t> UsableGamepads()
    {
        std::vector<int64_t> usable;
        if (s_anyVirtualPad)
        {
            for (const VirtualPad& pad : s_virtualPads)
                if (pad.joystick && SDL_IsGamepad(pad.id))
                    usable.push_back(pad.id);
            return usable;
        }
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        for (int i = 0; ids && i < count; i++)
            if (!hid::wheel::Owns(ids[i]))
                usable.push_back(ids[i]);
        SDL_free(ids);
        return usable;
    }

    void LogGamepad(unsigned user, SDL_Gamepad* gamepad)
    {
        // What it can do: a pad without motors takes rumble and ignores it.
        SDL_PropertiesID props = SDL_GetGamepadProperties(gamepad);
        const auto connection = SDL_GetGamepadConnectionState(gamepad);
        const SDL_GUID guid = SDL_GetGamepadGUIDForID(SDL_GetGamepadID(gamepad));
        char guidText[33]{};
        SDL_GUIDToString(guid, guidText, sizeof(guidText));
        fprintf(stderr, "[input] player %u: SDL GUID %s, driver %c\n", user+1, guidText,
            guid.data[14] >= 32 && guid.data[14] < 127 ? guid.data[14] : '-');
        fprintf(stderr, "[input] player %u: Home/Guide/PS %s (SDL Guide; foreground quit confirmation)\n", user+1,
            SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_GUIDE) ? "available" : "not exposed");
        fprintf(stderr, "[input] player %u: %s (rumble %s, trigger rumble %s; transport %s)\n", user + 1,
            SDL_GetGamepadName(gamepad),
            SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false) ? "yes" : "no",
            SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false) ? "yes" : "no",
            connection == SDL_JOYSTICK_CONNECTION_WIRED ? "wired" :
            connection == SDL_JOYSTICK_CONNECTION_WIRELESS ? "wireless" : "unknown");
    }

    // Main thread, as controllers come and go: the players' slots
    // (hid/pad_slots.h) and their open controllers brought in step. A
    // controller that moves to another player keeps its handle; one that
    // leaves its slot is stopped and closed.
    void SyncGamepads()
    {
        const auto usable = UsableGamepads();
        const hid::padslots::Ids before = s_slots;
        const bool modeChanged = s_assignments.SetPlayers(s_localPlayers.load(std::memory_order_relaxed));
        s_assignments.Refresh(usable);
        // Keep unused devices outside the guest ports. A fresh Start edge,
        // after release, is explicit consent to join the displayed missing slot.
        for (auto it = s_pendingPads.begin(); it != s_pendingPads.end();)
            if (!hid::padslots::Contains(usable, it->first))
            {
                SDL_CloseGamepad(it->second.handle);
                it = s_pendingPads.erase(it);
            }
            else ++it;
        for (const auto id : usable)
            if (hid::padslots::SlotOf(s_slots, id) < 0 && hid::padslots::SlotOf(before, id) < 0 && !s_pendingPads.contains(id))
                if (auto* handle = SDL_OpenGamepad(SDL_JoystickID(id))) s_pendingPads[id] = {handle, true};
        for (auto& [id, pending] : s_pendingPads)
        {
            const bool start = SDL_GetGamepadButton(pending.handle, SDL_GAMEPAD_BUTTON_START);
            if (start && !pending.start && s_assignments.Confirm(id, usable))
            {
                fprintf(stderr, "[input] replacement confirmed: instance %lld -> player %d\n",
                    (long long)id, hid::padslots::SlotOf(s_slots, id)+1);
                pending.start = start;
                break; // one missing player per frame
            }
            pending.start = start;
        }
        static std::string notice;
        const int missing = s_assignments.Missing();
        const std::string next = missing < 0 ? std::string{} : locale::Format(
            "Player {} disconnected. Press Start on a replacement controller to control Player {}.", missing+1, missing+1);
        if (next != notice)
        {
            if (!notice.empty()) ui::WithdrawToast(notice);
            notice = next;
            if (!notice.empty()) ui::Toast(notice, 15.0);
        }
        if (!modeChanged && s_slots == before) return;
        SDL_Gamepad* open[kUsers] = {};
        for (unsigned user = 0; user < kUsers; user++)
        {
            if (s_slots[user] == hid::padslots::kEmpty) continue;
            const int was = hid::padslots::SlotOf(before, s_slots[user]);
            auto pending = s_pendingPads.find(s_slots[user]);
            if (was >= 0) open[user] = s_gamepads[was];
            else if (pending != s_pendingPads.end())
            {
                open[user] = pending->second.handle;
                s_pendingPads.erase(pending);
            }
            else open[user] = SDL_OpenGamepad(SDL_JoystickID(s_slots[user]));
        }
        for (unsigned user = 0; user < kUsers; user++)
        {
            SDL_Gamepad* gamepad = s_gamepads[user];
            if (!gamepad || before[user] == s_slots[user]) continue;
            // Stop the old physical device even if solo fallback moves it.
            SendRumble(user, 0, 0);
            if (hid::padslots::SlotOf(s_slots, before[user]) < 0)
            {
                if (hid::padslots::Contains(usable, before[user]))
                    s_pendingPads[before[user]] = {gamepad, true};
                else SDL_CloseGamepad(gamepad);
            }
            fprintf(stderr, "[input] player %u's controller removed\n", user + 1);
        }
        std::lock_guard lock(s_mutex);
        s_keyboardAllowed = s_assignments.KeyboardAllowed();
        for (unsigned user = 0; user < kUsers; user++)
        {
            s_gamepads[user] = open[user];
            s_connected[user] = open[user] || (user == 0 && s_keyboardAllowed);
            if (before[user] == s_slots[user]) continue;
            s_rumbleErrorReported[user] = false;
            s_rumble[user] = {}; // no previous player's rumble on a replacement
            s_pads[user] = {};
            s_back[user] = {};
            s_latchedButtons[user] = 0;
            s_neutralRequired[user] = true;
            fprintf(stderr, "[input] player %u: sampled input, button edges and rumble cleared; waiting for neutral controls\n", user+1);
            ResetKeystrokesLocked(user);
            if (open[user]) LogGamepad(user, open[user]);
        }
    }

    void MaybeRumbleNotice()
    {
#if defined(__APPLE__) && !TARGET_OS_IOS
        bool affected = false;
        for (unsigned user = 0; user < 2; ++user)
            if (auto* pad = s_gamepads[user])
            {
                const auto connection = SDL_GetGamepadConnectionState(pad);
                const auto guid = SDL_GetGamepadGUIDForID(SDL_GetGamepadID(pad));
                const char* name = SDL_GetGamepadName(pad);
                hid::RumbleNotice::Device device{true, char(guid.data[14]),
                    connection == SDL_JOYSTICK_CONNECTION_WIRELESS ? hid::RumbleNotice::Connection::Wireless :
                    connection == SDL_JOYSTICK_CONNECTION_WIRED ? hid::RumbleNotice::Connection::Wired : hid::RumbleNotice::Connection::Unknown,
                    SDL_GetGamepadVendor(pad), name ? name : ""};
                // Only a test's own virtual devices can simulate the affected
                // route. This does not claim working physical USB haptics.
                if (const char* test = std::getenv("BURNOUT_TEST_XBOX_USB_RUMBLE");
                    s_anyVirtualPad && test && std::atoi(test) == int(user+1) &&
                    (!std::strchr(test, '@') || SecondsSinceLaunch() >= std::atof(std::strchr(test, '@')+1)))
                    device = {true, 'm', hid::RumbleNotice::Connection::Unknown, 0x045e, "Xbox virtual route"};
                affected |= hid::RumbleNotice::Affected(device);
            }
        if (hid::RumbleNotice::ShouldShow(affected, settings::GetBool(settings::Id::Vibration),
            settings::XboxUsbRumbleNoticeShown(), s_frontendMenu.load(std::memory_order_relaxed) || ui::SettingsOpen(),
            game::LastIntroMovieNs() != 0 &&
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() - game::LastIntroMovieNs() < 2'000'000'000, ui::HasModal()))
            ui::ShowNotice(hid::RumbleNotice::kTitle, hid::RumbleNotice::kText, [](const char*) {
                const bool saved = settings::RecordXboxUsbRumbleNotice();
                fprintf(stderr, "[input] Xbox USB notice dismissed; saved=%d\n", saved);
            }, true);
#endif
    }

#if defined(__APPLE__) && TARGET_OS_IOS
    // A phone or tablet has nothing to play the game with but a controller
    // (or a keyboard attached to it, which counts as on desktop), and a
    // player who has just installed it may not know. So a notice when there
    // is none: once as the game starts (not over the installer, which a
    // finger works), and again when the last one goes. Each waits a moment
    // and looks again, so a controller that iOS reports a little late (a
    // Bluetooth pad woken by its button after launch takes a few seconds),
    // or one that drops out and straight back, brings up nothing; and one
    // that turns up while the notice shows takes it down. Main thread;
    // guest ns, 0: none due.
    constexpr const char* kNeedsController = "The game needs a controller: an Xbox or PlayStation one over Bluetooth, or a Backbone.";
    constexpr const char* kControllerGone = "Controller disconnected: the game needs one (Xbox or PlayStation over Bluetooth, or a Backbone).";
    int64_t s_noControllerAt = 0;
    const char* s_noControllerText = nullptr;

    void NoControllerNoticeIn(int64_t ns, const char* text)
    {
        s_noControllerAt = guesttime::NowNs() + ns;
        s_noControllerText = text;
    }

    void NoControllerNotice()
    {
        if (!s_noControllerAt || guesttime::NowNs() < s_noControllerAt)
            return;
        s_noControllerAt = 0;
        // (BURNOUT_INPUT_SCRIPT is the controller of an automated run.)
        if (s_gamepads[0] || hid::wheel::Playing() || SDL_HasKeyboard() || !s_script.empty())
            return;
        fprintf(stderr, "[input] no controller: %s\n", s_noControllerText);
        ui::Toast(s_noControllerText, 10.0);
    }

    // Something to play with turned up: a notice still showing (or waiting
    // its turn) no longer holds.
    void WithdrawNoControllerNotice()
    {
        ui::WithdrawToast(kNeedsController);
        ui::WithdrawToast(kControllerGone);
    }
#endif
}

namespace
{
    const bool s_inputLog = std::getenv("BURNOUT_INPUT_LOG") != nullptr;

    // BURNOUT_INPUT_LOG=1: once a second, the left stick as the device reports
    // it (last value and range over the second) and as the game receives it.
    // A stick at rest must reach the game as exactly 0 (the drift check).
    // The triggers too (a worn one that never reads 0 holds the brake).
    void LogStick(float rawX, float rawY, float x, float y, float rawLt, float rawRt, uint8_t lt, uint8_t rt)
    {
        if (!s_inputLog)
            return;
        static float minX = 1, maxX = -1, minY = 1, maxY = -1;
        static auto last = std::chrono::steady_clock::now();
        minX = std::min(minX, rawX); maxX = std::max(maxX, rawX);
        minY = std::min(minY, rawY); maxY = std::max(maxY, rawY);
        auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::seconds(1))
            return;
        fprintf(stderr, "[input] left stick raw (%+.4f, %+.4f) range x[%+.4f, %+.4f] y[%+.4f, %+.4f] -> game (%d, %d); "
                        "triggers raw (%.3f, %.3f) -> game (%u, %u)\n",
            rawX, rawY, minX, maxX, minY, maxY, int(ToShort(x)), int(ToShort(y)), rawLt, rawRt, unsigned(lt), unsigned(rt));
        minX = minY = 1;
        maxX = maxY = -1;
        last = now;
    }
}

namespace hid
{
    namespace { uint16_t s_rawRewindButtons{};rewind::ChordSet s_rewindChord; }
    void SetFrontendMenu(bool frontend) { s_frontendMenu.store(frontend, std::memory_order_relaxed); }
    void SetLocalPlayers(unsigned players) { s_localPlayers.store(players, std::memory_order_relaxed); }
    uint16_t RawButtons() { return s_rawRewindButtons; }
    void Initialize()
    {
#if defined(BR_GAME_BURNOUT) && defined(__APPLE__) && !TARGET_OS_IOS
        const bool rawDualSense = settings::GetBool(settings::Id::DualSenseHidapi);
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, rawDualSense ? "1" : "0");
        SDL_SetHint(SDL_HINT_HIDAPI_ENUMERATE_ONLY_CONTROLLERS, "1");
        fprintf(stderr, "[input] DualSense backend: %s\n", rawDualSense ? "SDL raw HID" : "Apple native (Bluetooth compatibility)");
#endif
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
            fprintf(stderr, "[input] SDL gamepad init failed: %s\n", SDL_GetError());
        fprintf(stderr, "[input] stick deadzone %.2f, trigger deadzone %.2f\n", settings::GetFloat(settings::Id::StickDeadzone),
            settings::GetFloat(settings::Id::TriggerDeadzone));
#if defined(__APPLE__) && !TARGET_OS_IOS
        // Keyboard input comes from Cocoa events, never a raw/exclusive HID handle.
        SDL_SetHint(SDL_HINT_HIDAPI_ENUMERATE_ONLY_CONTROLLERS, "1");
#endif
        for (unsigned i = 1; i <= kUsers; ++i) AttachVirtualPad(int(i));
        // A steering wheel (hid/wheel.h), unless this run has the test's
        // own controllers: then those are the only devices. The test's own
        // wheel (BURNOUT_VIRTUAL_WHEEL, hid/virtual_wheel.h) is found as a
        // real one would be.
        if (!s_anyVirtualPad)
        {
            virtual_wheel::Attach();
            wheel::Initialize();
        }
        SyncGamepads();
        std::lock_guard lock(s_mutex);
        s_connected[0] = s_gamepads[0] || s_keyboardAllowed;
    }

    void NoteGameStarting()
    {
#if defined(__APPLE__) && TARGET_OS_IOS
        NoControllerNoticeIn(5'000'000'000, kNeedsController);
#endif
    }

    namespace
    {
        bool s_glyphsKnown = false;
        std::atomic<Glyphs> s_publishedGlyphs{ Glyphs::Keyboard };
        Glyphs s_glyphs = Glyphs::Keyboard;

        Glyphs GlyphsOf(SDL_JoystickID id)
        {
            switch (SDL_GetGamepadTypeForID(id))
            {
            case SDL_GAMEPAD_TYPE_PS3:
            case SDL_GAMEPAD_TYPE_PS4:
            case SDL_GAMEPAD_TYPE_PS5:
                return Glyphs::PlayStation;
            default:
                return Glyphs::Xbox;  // Xbox, generic and the virtual pad (Nintendo pads read as their positions)
            }
        }

        void NoteGlyphs(Glyphs glyphs)
        {
            // (Before any input the hints showed what is connected: a change
            // from that is a change too.)
            Glyphs before = s_glyphsKnown ? s_glyphs : s_publishedGlyphs.load(std::memory_order_relaxed);
            s_glyphsKnown = true;
            s_glyphs = glyphs;
            s_publishedGlyphs.store(glyphs, std::memory_order_relaxed);
            if (glyphs == before)
                return;
            fprintf(stderr, "[input] hints now show %s buttons\n",
                glyphs == Glyphs::PlayStation ? "PlayStation" : glyphs == Glyphs::Xbox ? "Xbox" : "keyboard");
        }
    }

    Glyphs LastGlyphs()
    {
        if (!s_glyphsKnown)
        {
            // Nothing used yet: what is connected.
            int count = 0;
            SDL_JoystickID* ids = SDL_GetGamepads(&count);
            Glyphs best = Glyphs::Keyboard;
            if (wheel::Playing())
                best = wheel::PlayStationButtons() ? Glyphs::PlayStation : Glyphs::Xbox;
            for (int i = 0; i < count; i++)
                if (best != Glyphs::PlayStation)
                    best = GlyphsOf(ids[i]);
            SDL_free(ids);
            s_publishedGlyphs.store(best, std::memory_order_relaxed);
            return best;
        }
        return s_glyphs;
    }

    Glyphs ControllerGlyphs()
    { return s_gamepads[0] ? GlyphsOf(SDL_GetGamepadID(s_gamepads[0])):Glyphs::Xbox; }

    Glyphs PublishedGlyphs()
    {
        return s_publishedGlyphs.load(std::memory_order_relaxed);
    }

    Glyphs PromptGlyphs(bool anyThread)
    {
        switch (settings::ButtonPrompts(settings::GetInt(settings::Id::ButtonPrompts)))
        {
        case settings::ButtonPrompts::Xbox: return Glyphs::Xbox;
        case settings::ButtonPrompts::PlayStation: return Glyphs::PlayStation;
        case settings::ButtonPrompts::Keyboard: return Glyphs::Keyboard;
        case settings::ButtonPrompts::Auto: break;
        }
        return anyThread ? PublishedGlyphs() : LastGlyphs();
    }

    bool AssignedGamepad(SDL_JoystickID device)
    {
        for (auto* pad : s_gamepads)
            if (pad && SDL_GetGamepadID(pad) == device) return true;
        return false;
    }

    void HandleEvent(const SDL_Event& e)
    {
        wheel::HandleEvent(e);  // a wheel coming or going
        // The hints follow the last real input: a key (not a Command
        // shortcut, not a repeat), a button, or a stick pushed past halfway.
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && !(e.key.mod & SDL_KMOD_GUI))
            NoteGlyphs(Glyphs::Keyboard);
        else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
            NoteGlyphs(GlyphsOf(e.gbutton.which));
        else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && (e.gaxis.value > 16384 || e.gaxis.value < -16384))
            NoteGlyphs(GlyphsOf(e.gaxis.which));
        else if ((e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN && wheel::Owns(e.jbutton.which)) ||
                 (e.type == SDL_EVENT_JOYSTICK_HAT_MOTION && e.jhat.value && wheel::Owns(e.jhat.which)))
            NoteGlyphs(wheel::PlayStationButtons() ? Glyphs::PlayStation : Glyphs::Xbox);  // a steering wheel's
        else if (!s_glyphsKnown && (e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED))
            LastGlyphs();  // (publishes what is connected)

        if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST || e.type == SDL_EVENT_WINDOW_HIDDEN ||
            e.type == SDL_EVENT_WINDOW_MINIMIZED)
        {
            if (SDL_Window* window = SDL_GetWindowFromID(e.window.windowID))
            {
                SDL_SetWindowKeyboardGrab(window, false);
                SDL_SetWindowMouseGrab(window, false);
                SDL_SetWindowRelativeMouseMode(window, false);
            }
            SDL_ResetKeyboard();
            ApplyRumbleActivity(s_rumbleFocus.SetFocused(false));
            fprintf(stderr, "[input] window inactive (event %u): keyboard state cleared; all input grabs released\n", e.type);
        }
        else if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED || e.type == SDL_EVENT_WINDOW_SHOWN ||
                 e.type == SDL_EVENT_WINDOW_RESTORED)
        {
            SDL_Window* window = SDL_GetWindowFromID(e.window.windowID);
            const bool focused = e.type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
                (window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS));
            ApplyRumbleActivity(s_rumbleFocus.SetFocused(focused));
            fprintf(stderr, "[input] window activity: focused=%d, keyboard=%s, grab=%d\n", focused,
                SDL_HasKeyboard() ? "present" : "absent",
                window ? SDL_GetWindowKeyboardGrab(window) : false);
        }
        else if (e.type == SDL_EVENT_KEYBOARD_ADDED || e.type == SDL_EVENT_KEYBOARD_REMOVED)
        {
            fprintf(stderr, "[input] keyboard %s (device %u)\n",
                e.type == SDL_EVENT_KEYBOARD_ADDED ? "connected" : "removed", e.kdevice.which);
#if defined(__APPLE__) && TARGET_OS_IOS
            if (e.type == SDL_EVENT_KEYBOARD_ADDED) WithdrawNoControllerNotice();
#endif
        }
        else if (e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_JOYSTICK_ADDED)
        {
            // (A joystick too: a wheel that comes takes its device, which
            // may have looked like a gamepad.)
            SyncGamepads();
#if defined(__APPLE__) && TARGET_OS_IOS
            if (s_gamepads[0] || wheel::Playing())
                WithdrawNoControllerNotice();
#endif
        }
        else if (e.type == SDL_EVENT_GAMEPAD_REMOVED || e.type == SDL_EVENT_JOYSTICK_REMOVED)
        {
            const bool hadOne = s_gamepads[0] != nullptr;
            SyncGamepads();
#if defined(__APPLE__) && TARGET_OS_IOS
            if (hadOne && !s_gamepads[0] && !wheel::Playing())
                NoControllerNoticeIn(1'000'000'000, kControllerGone);
#else
            (void)hadOne;
#endif
        }

    }

    void Shutdown()
    {
        for (auto& [id, pending] : s_pendingPads) SDL_CloseGamepad(pending.handle);
        s_pendingPads.clear();
        wheel::Shutdown();
        for (unsigned user = 0; user < kUsers; user++)
            if (s_gamepads[user])
            {
                SendRumble(user, 0, 0);
                SDL_CloseGamepad(s_gamepads[user]);
                s_gamepads[user] = nullptr;
            }
    }

    void Suspend()
    {
        // Update sends a rumble for 5 s at a time and doesn't run meanwhile:
        // stop it now rather than buzz on for a game that stands still.
        ApplyRumbleActivity(s_rumbleFocus.SetSuspended(true));
        wheel::Suspend();
    }

    void Resume()
    {
        // The next Update sends the game's last request again, as the
        // Vibration setting allows.
        ApplyRumbleActivity(s_rumbleFocus.SetSuspended(false));
    }

    double SecondsSinceInput()
    {
        if (!s_inputSeen)
            return 0.0;
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - s_lastInput).count();
    }

    void ScriptEvent(const char* name)
    {
        const auto it = std::find(s_anchors.begin(), s_anchors.end(), name);
        if (it == s_anchors.end())
            return;
        const double now = SecondsSinceLaunch();
        {
            std::lock_guard lock(s_anchorMutex);
            s_anchorTimes[it - s_anchors.begin()] = now;
        }
        fprintf(stderr, "[input] script: event \"%s\" at %.1f s\n", name, now);
    }

    void SetBlocked(bool blocked)
    {
        s_blocked.store(blocked, std::memory_order_relaxed);
    }

    void Update()
    {
#if defined(__APPLE__) && TARGET_OS_IOS
        NoControllerNotice();
#endif
        // Before any input, what is connected decides, for the game's
        // prompts as for our menus: published here every frame, not only
        // when one of our panels draws a hint.
        if (!s_glyphsKnown)
            LastGlyphs();
        ReadLive();
        std::array<std::string, kUsers> padSteps;
        for (unsigned i = 0; i < kUsers; ++i) padSteps[i] = StepVirtualPad(s_virtualPads[i]);
        SyncGamepads();
        MaybeRumbleNotice();
        user::my_music::PollLibrary();
        if (const std::string wheelSteps = virtual_wheel::Step(SecondsSinceLaunch()); !wheelSteps.empty())
            fprintf(stderr, "[wheel] virtual wheel %s\n", wheelSteps.c_str());
        const float deadzone = settings::GetFloat(settings::Id::StickDeadzone);
        const float triggerDeadzone = settings::GetFloat(settings::Id::TriggerDeadzone);
        // A steering wheel first, while one is plugged in and Settings >
        // Wheel has it on: it is player 1's, in place of a controller. It
        // brings its own steering curve and its pedals their own
        // calibration (hid/wheel_map.h), so the stick's radial deadzone has
        // nothing to do here. A button bound to push the right stick (none
        // by default in Burnout: hid/wheel_map.h) does so.
        wheel::Reading wheelRead;
        const bool onWheel = wheel::Sample(wheelRead);
        // While a menu has the input the game sees idle pads; after it
        // closes, each button still held stays hidden until released, so the
        // press that closed the menu (B, Start, Esc) doesn't reach the game.
        const bool blocked = s_blocked.load(std::memory_order_relaxed);
        Pad pads[kUsers];
        for (unsigned user = 0; user < kUsers; user++)
        {
            Pad& p = pads[user];
            SDL_Gamepad* gamepad = s_gamepads[user];
            float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
            bool padBack = false;  // the controller's Back, as the device reports it
            float rawX = 0, rawY = 0;  // the left stick before its deadzone (BURNOUT_INPUT_LOG)
            if (user == 0 && onWheel)
            {
                p.buttons = uint16_t(wheelRead.buttons & ~uint16_t(XAMINPUT_GAMEPAD_BACK));
                padBack = wheelRead.back;
                if (s_back[user].ForGame(padBack, wheelRead.start))
                    p.buttons |= XAMINPUT_GAMEPAD_BACK;
                lx = wheelRead.steer;
                ry = float(wheelRead.rightY) / 32767.0f;
                lt = wheelRead.brake;
                rt = wheelRead.throttle;
                rawX = lx;
                rawY = ly;
            }
            else if (gamepad)
            {
                static const std::pair<SDL_GamepadButton, uint16_t> map[] = {
                    { SDL_GAMEPAD_BUTTON_SOUTH, XAMINPUT_GAMEPAD_A }, { SDL_GAMEPAD_BUTTON_EAST, XAMINPUT_GAMEPAD_B },
                    { SDL_GAMEPAD_BUTTON_WEST, XAMINPUT_GAMEPAD_X }, { SDL_GAMEPAD_BUTTON_NORTH, XAMINPUT_GAMEPAD_Y },
                    { SDL_GAMEPAD_BUTTON_BACK, XAMINPUT_GAMEPAD_BACK }, { SDL_GAMEPAD_BUTTON_START, XAMINPUT_GAMEPAD_START },
                    { SDL_GAMEPAD_BUTTON_LEFT_STICK, XAMINPUT_GAMEPAD_LEFT_THUMB },
                    { SDL_GAMEPAD_BUTTON_RIGHT_STICK, XAMINPUT_GAMEPAD_RIGHT_THUMB },
                    { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, XAMINPUT_GAMEPAD_LEFT_SHOULDER },
                    { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, XAMINPUT_GAMEPAD_RIGHT_SHOULDER },
                    { SDL_GAMEPAD_BUTTON_DPAD_UP, XAMINPUT_GAMEPAD_DPAD_UP }, { SDL_GAMEPAD_BUTTON_DPAD_DOWN, XAMINPUT_GAMEPAD_DPAD_DOWN },
                    { SDL_GAMEPAD_BUTTON_DPAD_LEFT, XAMINPUT_GAMEPAD_DPAD_LEFT }, { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, XAMINPUT_GAMEPAD_DPAD_RIGHT },
                };
                for (auto& [button, bit] : map)
                    if (bit != XAMINPUT_GAMEPAD_BACK && SDL_GetGamepadButton(gamepad, button))
                        p.buttons |= bit;
                padBack = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK);
                if (s_back[user].ForGame(padBack, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START)))
                    p.buttons |= XAMINPUT_GAMEPAD_BACK;
                auto axis = [](SDL_Gamepad* g, SDL_GamepadAxis a) { return float(SDL_GetGamepadAxis(g, a)) / 32767.0f; };
                lx = axis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
                ly = -axis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);  // XInput: up is positive
                rx = axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
                ry = -axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
                lt = axis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
                rt = axis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
                rawX = lx;
                rawY = ly;
                ApplyDeadzone(lx, ly, deadzone);
                ApplyDeadzone(rx, ry, deadzone);
            }
            const float rawLt = lt, rawRt = rt;
            const uint16_t rawButtons=uint16_t(p.buttons|(padBack ? XAMINPUT_GAMEPAD_BACK:0));
            uint16_t keyboardButtons=0;

            // Keyboard, player 1's (digital; replaces the controller's
            // value for what it touches): each control's key is Settings >
            // Controls (the Key options), resolved by key_map.h. A key is a
            // scancode SDL's state array is indexed by; an out-of-range
            // value (a hand edit beat Load's checks) counts as not held.
            // Cmd+Tab/H/M and other macOS shortcuts belong to the application,
            // not to the driving key map. Do not turn their held letters into gas,
            // steering or menu actions while Command is down. No global key hook.
            const bool appShortcut = (SDL_GetModState() & SDL_KMOD_GUI) != 0;
            if (const bool* k = user == 0 && SDL_GetKeyboardFocus() && !appShortcut ? SDL_GetKeyboardState(nullptr) : nullptr)
            {
                bool pressed[keymap::kControls] = {};
                auto down = [&](settings::Id id) {
                    int code = settings::GetInt(id);  // 0: no key
                    if (settings::GetBool(settings::Id::RewindEnabled))
                        for (unsigned i=0;i<6;++i)
                            if (code && code==settings::GetInt(settings::Id(unsigned(settings::Id::KeyRewind10)+i))) return false;
                    return code > 0 && code < SDL_SCANCODE_COUNT && k[code];
                };
                for (size_t i = 0; i < keymap::kControls; i++)
                    pressed[i] = down(keymap::kEntries[i].id) || down(keymap::kEntries[i].id2);
                keymap::Resolved r = keymap::Resolve(pressed);
                keyboardButtons=r.buttons;
                p.buttons |= r.buttons;
                if (r.rightTrigger)
                    rt = 1.0f;
                if (r.leftTrigger)
                    lt = 1.0f;
                float* axes[size_t(keymap::Axis::Count)] = { &lx, &ly, &rx, &ry };  // Axis order: LX, LY, RX, RY
                for (size_t a = 0; a < size_t(keymap::Axis::Count); a++)
                    if (r.axisSet[a])
                        *axes[a] = r.axis[a];
            }
            p.lx = ToShort(lx);
            p.ly = ToShort(ly);
            p.rx = ToShort(rx);
            p.ry = ToShort(ry);
            p.leftTrigger = Trigger(lt, triggerDeadzone);
            p.rightTrigger = Trigger(rt, triggerDeadzone);
            if (user == 0 && (gamepad || onWheel))
                LogStick(rawX, rawY, lx, ly, rawLt, rawRt, p.leftTrigger, p.rightTrigger);
            for (unsigned v = 0; v < kUsers; v++)
                if (gamepad && !padSteps[v].empty() && SDL_GetGamepadID(gamepad) == s_virtualPads[v].id)
                    fprintf(stderr, "[input] virtual pad %d (player %u) %s: left stick raw (%+.4f, %+.4f) -> game (%d, %d), right (%d, %d); "
                                    "triggers raw (%.3f, %.3f) -> game (%u, %u); deadzones: stick %.2f, trigger %.2f%s\n",
                        v + 1, user + 1, padSteps[v].c_str(), rawX, rawY, int(p.lx), int(p.ly), int(p.rx), int(p.ry), rawLt, rawRt,
                        unsigned(p.leftTrigger), unsigned(p.rightTrigger), deadzone, triggerDeadzone,
                        blocked ? " (a menu is open: the game sees an idle pad)" : "");

            // The last time anything was pressed or moved, the script's taps
            // included (SecondsSinceInput).
            Pad scripted = p;
            if (user == 0)
                ApplyScript(scripted);
            if (user==0)
            {
                s_rawRewindButtons=uint16_t(rawButtons|(scripted.buttons&~keyboardButtons));
                const bool active=settings::GetBool(settings::Id::RewindEnabled);
                const std::array<uint16_t,3> masks={uint16_t(settings::GetInt(settings::Id::RewindChord10)),
                    uint16_t(settings::GetInt(settings::Id::RewindChord20)),uint16_t(settings::GetInt(settings::Id::RewindChord30))};
                // Explicit virtual-pad scripts run unattended in an isolated
                // test profile; a real controller still requires game focus.
                const bool focused=SDL_GetKeyboardFocus()!=nullptr || (s_anyVirtualPad && !s_script.empty());
                const auto chord=s_rewindChord.Update(masks,s_rawRewindButtons,blocked||!active||!focused||s_neutralRequired[0],double(SDL_GetTicksNS())/1e9);
                p.buttons&=~chord.consume;
                s_rewindConsumed.store(chord.consume,std::memory_order_relaxed);
                if (chord.seconds) ui::TriggerRewind(chord.seconds);
            }
            if (scripted.buttons || scripted.leftTrigger || scripted.rightTrigger || scripted.lx || scripted.ly || scripted.rx ||
                scripted.ry || !s_inputSeen)
            {
                s_lastInput = std::chrono::steady_clock::now();
                s_inputSeen = true;
            }

            if (s_wasBlocked && !blocked)
                s_latchedButtons[user] = p.buttons;
            // A Back pressed in the menu (or the one that closed it) is never replayed.
            if ((blocked || s_wasBlocked) && padBack)
                s_back[user].state = BackState::Swallow;
            s_latchedButtons[user] &= p.buttons;
            if (s_neutralRequired[user])
            {
                if (!p.buttons && !padBack && !p.leftTrigger && !p.rightTrigger && !p.lx && !p.ly && !p.rx && !p.ry)
                {
                    s_neutralRequired[user] = false;
                    fprintf(stderr, "[input] player %u: all controls released\n", user+1);
                }
                p = {};
            }
            if (blocked)
                p = Pad{};
            else
                p.buttons &= ~s_latchedButtons[user];
        }
        s_wasBlocked = blocked;

        RumbleRequest requests[kUsers];
        {
            std::lock_guard lock(s_mutex);
            for (unsigned user = 0; user < kUsers; user++)
            {
                s_pads[user] = pads[user];
                requests[user] = s_rumble[user];
                s_rumble[user].dirty = false;
            }
            s_scriptSuppressed = blocked || s_neutralRequired[0];
        }
        static bool vibration = true;
        bool vibrationChanged = false;
        if (bool on = settings::GetBool(settings::Id::Vibration); on != vibration)
        {
            vibration = on;
            vibrationChanged = true;  // stop (or restart) a rumble under way
        }
        uint16_t wheelLow = 0, wheelHigh = 0;
        for (unsigned user = 0; user < kUsers; user++)
        {
            uint16_t low = requests[user].low, high = requests[user].high;
            const bool rumble = requests[user].dirty || vibrationChanged;
            if (rumble && s_anyVirtualPad)
            {
                // What the game asked for and what the controller gets, on
                // change. Only with the test's pads: while driving the game
                // changes its request every frame, which would be a line a frame
                // in a player's last-run.log (burnout-revenge.sh sets BURNOUT_INPUT_LOG).
                static uint64_t logged[kUsers] = { ~0ull, ~0ull, ~0ull, ~0ull };
                uint64_t now = uint64_t(low) << 17 | uint64_t(high) << 1 | (vibration ? 1u : 0u);
                if (now != logged[user] && (user == 0 || s_gamepads[user]))
                {
                    logged[user] = now;
                    fprintf(stderr, "[input] rumble, player %u: game low %u, high %u; Vibration %s%s\n", user + 1, unsigned(low),
                        unsigned(high), vibration ? "on" : "off (the controller gets 0)", s_gamepads[user] ? "" : "; no controller");
                }
            }
            if (!vibration)
                low = high = 0;
            s_rumbleFocus.Filter(low, high);
            if (rumble && s_gamepads[user])
                SendRumble(user, low, high);
            if (user == 0)
            {
                wheelLow = low;
                wheelHigh = high;
            }
        }
        // The wheel's own force feedback, player 1's: every frame, since
        // Settings > Wheel > Force Feedback and Centering Force can change
        // under it without the game asking for anything new.
        wheel::Rumble(wheelLow, wheelHigh, vibration);
        wheel::Update();
    }
}

namespace
{
    // A user's pad as the game sees it: devices, plus scripted taps for
    // player 1. Caller holds s_mutex.
    Pad CurrentPadLocked(unsigned user)
    {
        Pad p = s_pads[user];
        if (user == 0 && !s_scriptSuppressed)
            ApplyScript(p, true);
        if (user==0) p.buttons&=~s_rewindConsumed.load(std::memory_order_relaxed);
        return p;
    }

    // Keyboard P1 is available in solo and in local sessions started with it.
    // It must not hide the loss of a physical P1 during local multiplayer.
    // Caller holds s_mutex.
    bool ConnectedLocked(unsigned user)
    {
        return user < kUsers && s_connected[user];
    }

    // XInput's "any user" (0xFF) is player 1, as in Xenia.
    unsigned UserOf(uint32_t userIndex)
    {
        return (userIndex & 0xFF) == 0xFF ? 0 : userIndex;
    }

    // Keystrokes (XamInputGetKeystroke[Ex]): edges of buttons, triggers and
    // stick directions as XInput VK_PAD_* codes, with auto-repeat for the
    // navigation keys, generated on demand from the same state as GetState.
    struct KeyDef { uint16_t vk; bool repeats; bool (*down)(const Pad&); };
    const KeyDef kKeys[] = {
        { 0x5800, false, [](const Pad& p) { return (p.buttons & XAMINPUT_GAMEPAD_A) != 0; } },
        { 0x5801, false, [](const Pad& p) { return (p.buttons & XAMINPUT_GAMEPAD_B) != 0; } },
        { 0x5802, false, [](const Pad& p) { return (p.buttons & XAMINPUT_GAMEPAD_X) != 0; } },
        { 0x5803, false, [](const Pad& p) { return (p.buttons & XAMINPUT_GAMEPAD_Y) != 0; } },
        { 0x5804, false, [](const Pad& p) { return (p.buttons & XAMINPUT_GAMEPAD_RIGHT_SHOULDER) != 0; } },
        { 0x5805, false, [](const Pad& p) { return (p.buttons & XAMINPUT_GAMEPAD_LEFT_SHOULDER) != 0; } },
        { 0x5806, false, [](const Pad& p) { return p.leftTrigger > 30; } },
        { 0x5807, false, [](const Pad& p) { return p.rightTrigger > 30; } },
        { 0x5810, true, [](const Pad& p) { return (p.buttons & XAMINPUT_GAMEPAD_DPAD_UP) != 0; } },
        { 0x5811, true, [](const Pad& p) { return (p.buttons & XAMINPUT_GAMEPAD_DPAD_DOWN) != 0; } },
        { 0x5812, true, [](const Pad& p) { return (p.buttons & XAMINPUT_GAMEPAD_DPAD_LEFT) != 0; } },
        { 0x5813, true, [](const Pad& p) { return (p.buttons & XAMINPUT_GAMEPAD_DPAD_RIGHT) != 0; } },
        { 0x5814, false, [](const Pad& p) { return (p.buttons & XAMINPUT_GAMEPAD_START) != 0; } },
        { 0x5815, false, [](const Pad& p) { return (p.buttons & XAMINPUT_GAMEPAD_BACK) != 0; } },
        { 0x5816, false, [](const Pad& p) { return (p.buttons & XAMINPUT_GAMEPAD_LEFT_THUMB) != 0; } },
        { 0x5817, false, [](const Pad& p) { return (p.buttons & XAMINPUT_GAMEPAD_RIGHT_THUMB) != 0; } },
        { 0x5820, true, [](const Pad& p) { return p.ly > 16384; } },
        { 0x5821, true, [](const Pad& p) { return p.ly < -16384; } },
        { 0x5822, true, [](const Pad& p) { return p.lx > 16384; } },
        { 0x5823, true, [](const Pad& p) { return p.lx < -16384; } },
        { 0x5830, true, [](const Pad& p) { return p.ry > 16384; } },
        { 0x5831, true, [](const Pad& p) { return p.ry < -16384; } },
        { 0x5832, true, [](const Pad& p) { return p.rx > 16384; } },
        { 0x5833, true, [](const Pad& p) { return p.rx < -16384; } },
    };
    constexpr size_t kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);
    bool s_keyDown[kUsers][kKeyCount] = {};
    // Guest ns: a key held through a suspension doesn't repeat as soon as it's back.
    int64_t s_keyRepeatAt[kUsers][kKeyCount] = {};

    struct Keystroke
    {
        be<uint16_t> virtualKey;
        be<uint16_t> unicode;
        be<uint16_t> flags;   // 1 key down, 2 key up, 4 repeat
        uint8_t userIndex;
        uint8_t hidCode;
    };
    static_assert(sizeof(Keystroke) == 8);

    constexpr uint32_t ERROR_EMPTY = 0x10D2;

    void ResetKeystrokesLocked(unsigned user)
    {
        std::fill(std::begin(s_keyDown[user]), std::end(s_keyDown[user]), false);
        std::fill(std::begin(s_keyRepeatAt[user]), std::end(s_keyRepeatAt[user]), 0);
    }

    // The next keystroke of `user`, or of the first user that has one
    // (`anyUser`: XamInputGetKeystroke's "any"), with the user it came from.
    uint32_t NextKeystroke(unsigned user, bool anyUser, Keystroke* out)
    {
        std::lock_guard lock(s_mutex);
        const unsigned first = anyUser ? 0 : user, last = anyUser ? kUsers - 1 : user;
        bool any = false;
        int64_t now = guesttime::NowNs();
        for (unsigned u = first; u <= last; u++)
        {
            if (!ConnectedLocked(u))
                continue;
            any = true;
            Pad p = CurrentPadLocked(u);
            auto emit = [&](size_t i, uint16_t flags) {
                out->virtualKey = kKeys[i].vk;
                out->unicode = 0;
                out->flags = flags;
                out->userIndex = uint8_t(u);
                out->hidCode = 0;
                return ERROR_SUCCESS;
            };
            for (size_t i = 0; i < kKeyCount; i++)
            {
                bool down = kKeys[i].down(p);
                if (down != s_keyDown[u][i])
                {
                    s_keyDown[u][i] = down;
                    s_keyRepeatAt[u][i] = now + 400'000'000;
                    return emit(i, down ? 1 : 2);
                }
            }
            for (size_t i = 0; i < kKeyCount; i++)
                if (s_keyDown[u][i] && kKeys[i].repeats && now >= s_keyRepeatAt[u][i])
                {
                    s_keyRepeatAt[u][i] = now + 100'000'000;
                    return emit(i, 1 | 4);
                }
        }
        return any ? ERROR_EMPTY : ERROR_DEVICE_NOT_CONNECTED;
    }
}

// XInput's device subtypes, of which xbox.h names only the gamepad. The
// Xbox 360 version of the game supports racing wheels and asks what is
// connected through XamInputGetCapabilities (its own code queries it in two
// places), so a wheel says so for itself instead of passing as a pad.
constexpr uint8_t XAMINPUT_DEVSUBTYPE_WHEEL = 0x02;

uint32_t XamInputGetCapabilities(uint32_t userIndex, uint32_t flags, XAMINPUT_CAPABILITIES* caps)
{
    const unsigned user = UserOf(userIndex);
    {
        std::lock_guard lock(s_mutex);
        if (!ConnectedLocked(user))
            return ERROR_DEVICE_NOT_CONNECTED;
    }
    // A wheel in play says it is one. The type stays the gamepad one, as
    // it is for every XInput device; only the subtype differs.
    //
    // Reporting a real wheel's narrower capabilities as well (no sticks but
    // its own axis, the force-feedback flag) was tried and changed nothing
    // the game does, so it is not worth the risk to the paddles: they reach
    // the game as the right stick, which such a wheel hasn't got.
    const bool asWheel = user == 0 && hid::wheel::Playing();
    memset(caps, 0, sizeof(*caps));
    caps->Type = XAMINPUT_DEVTYPE_GAMEPAD;
    caps->SubType = asWheel ? XAMINPUT_DEVSUBTYPE_WHEEL : XAMINPUT_DEVSUBTYPE_GAMEPAD;
    // What the device has stays the pad's whole set, whichever subtype it
    // reports: the wheel's paddles reach the game as the right stick and
    // its buttons as the pad's, so the game is told those exist. A real
    // 360 wheel reports no right stick at all, and the game would have no
    // reason to read one.
    caps->Gamepad.wButtons = ByteSwap(uint16_t(0xF3FF));
    caps->Gamepad.bLeftTrigger = 0xFF;
    caps->Gamepad.bRightTrigger = 0xFF;
    for (int16_t* v : { &caps->Gamepad.sThumbLX, &caps->Gamepad.sThumbLY, &caps->Gamepad.sThumbRX, &caps->Gamepad.sThumbRY })
        *v = ByteSwap(int16_t(0xFFC0));
    caps->Vibration.wLeftMotorSpeed = 0xFFFF;
    caps->Vibration.wRightMotorSpeed = 0xFFFF;
    // On the first ask, and on any change: whether the game looks at all,
    // and what it was told.
    static int told[kUsers] = { -1, -1, -1, -1 };
    if (int(asWheel) != told[user])
    {
        told[user] = int(asWheel);
        fprintf(stderr, "[input] the game asked what player %u has: told it a %s\n", user + 1, asWheel ? "racing wheel" : "gamepad");
    }
    return ERROR_SUCCESS;
}

uint32_t XamInputGetState(uint32_t userIndex, uint32_t flags, XAMINPUT_STATE* state)
{
    const unsigned user = UserOf(userIndex);
    Pad p;
    uint32_t packet;
    {
        std::lock_guard lock(s_mutex);
        if (!ConnectedLocked(user))
            return ERROR_DEVICE_NOT_CONNECTED;
        p = CurrentPadLocked(user);
        static Pad last[kUsers];
        if (memcmp(&p, &last[user], sizeof(p)) != 0)
        {
            s_packets[user]++;
            last[user] = p;
        }
        packet = s_packets[user];
    }
    state->dwPacketNumber = ByteSwap(packet);
    state->Gamepad.wButtons = ByteSwap(p.buttons);
    state->Gamepad.bLeftTrigger = p.leftTrigger;
    state->Gamepad.bRightTrigger = p.rightTrigger;
    state->Gamepad.sThumbLX = ByteSwap(p.lx);
    state->Gamepad.sThumbLY = ByteSwap(p.ly);
    state->Gamepad.sThumbRX = ByteSwap(p.rx);
    state->Gamepad.sThumbRY = ByteSwap(p.ry);
    return ERROR_SUCCESS;
}

uint32_t XamInputSetState(uint32_t userIndex, uint32_t flags, XAMINPUT_VIBRATION* vibration)
{
    const unsigned user = UserOf(userIndex);
    std::lock_guard lock(s_mutex);
    if (!ConnectedLocked(user))
        return ERROR_DEVICE_NOT_CONNECTED;
    s_rumble[user].low = ByteSwap(vibration->wLeftMotorSpeed);
    s_rumble[user].high = ByteSwap(vibration->wRightMotorSpeed);
    s_rumble[user].dirty = true;
    return ERROR_SUCCESS;
}

// User 0xFF ("any", or the flag 0x40000000) takes the first user with a
// keystroke and says which; queries for non-gamepad devices report none
// connected.
uint32_t XamInputGetKeystroke(uint32_t userIndex, uint32_t flags, Keystroke* keystroke)
{
    if (!keystroke)
        return 0xA0;  // ERROR_BAD_ARGUMENTS
    if ((flags & 0xFF) && (flags & 1) == 0)
        return ERROR_DEVICE_NOT_CONNECTED;
    const bool anyUser = (userIndex & 0xFF) == 0xFF || (flags & 0x40000000);
    if (!anyUser && userIndex >= kUsers)
        return ERROR_DEVICE_NOT_CONNECTED;
    return NextKeystroke(anyUser ? 0 : userIndex, anyUser, keystroke);
}

uint32_t XamInputGetKeystrokeEx(be<uint32_t>* userIndex, uint32_t flags, Keystroke* keystroke)
{
    uint32_t result = XamInputGetKeystroke(userIndex ? uint32_t(*userIndex) : 0xFF, flags, keystroke);
    if (result == ERROR_SUCCESS && userIndex)
        *userIndex = keystroke->userIndex;
    return result;
}

GUEST_FUNCTION_HOOK(__imp__XamInputGetCapabilities, XamInputGetCapabilities);
GUEST_FUNCTION_HOOK(__imp__XamInputGetState, XamInputGetState);
GUEST_FUNCTION_HOOK(__imp__XamInputSetState, XamInputSetState);
GUEST_FUNCTION_HOOK(__imp__XamInputGetKeystrokeEx, XamInputGetKeystrokeEx);
