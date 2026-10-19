// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Controller input (Phase 6): SDL3 gamepads and the keyboard, presented to
// the game as XInput users 0 to 3 through XamInputGetState.
//
//   - Players: each controller is one player, in the order they connect
//     (hid/pad_slots.h: the first player 1, the second player 2; if player
//     1's goes and nobody waits, the next one up becomes player 1). The
//     keyboard, a wheel and BURNOUT_INPUT_SCRIPT are player 1's; player 1
//     always counts as connected (the keyboard), the others while they
//     have a controller. Each player's rumble goes to their controller.
//
//   - Sticks get a radial deadzone with rescaling (Settings > Input, or
//     BURNOUT_DEADZONE; default 0.08 of full deflection), so worn or drifting
//     sticks rest at exactly zero while small movements still ramp up
//     smoothly from the edge of the deadzone. Triggers get their own
//     (BURNOUT_TRIGGER_DEADZONE, default 0.02).
//   - Keyboard: each Xbox 360 control has two keys (Settings > Controls;
//     the [keys] table of settings.toml; hid/key_map.h lists them).
//     Defaults: arrows = D-pad, A/D and Left/Right = steer, Up/Down = left
//     stick too, W/S = gas/brake (RT/LT), Space = A, Backspace = B,
//     Left Shift = X, Left Ctrl = Y, Q/E = LB/RB, Enter = START, Esc = BACK,
//     O/L/K/; = right stick, Z/C = the stick clicks. A key may drive several.
//   - A USB steering wheel (hid/wheel.h and hid/wheel_map.h, Settings >
//     Wheel) is used instead of a controller while one is plugged in: its
//     own steering curve reaches the left stick and its pedals the
//     triggers, so the stick deadzone above is not applied to it. Its force
//     feedback takes the rumble the game asks for. BURNOUT_VIRTUAL_WHEEL
//     (hid/virtual_wheel.h) is a test's own G29.
//   - BURNOUT_INPUT_SCRIPT="12:START,15.5:A,30-90/4:DOWN,100~160:RT": taps
//     (150 ms) at seconds since launch, taps every `step` seconds over a
//     range, or inputs held over a range; RT/LT (triggers) and LL/LR/LU/LD
//     (left stick) besides the buttons. For automated runs; works headless.
//     It feeds the game after the deadzones.
//   - BURNOUT_VIRTUAL_PAD="2:LX=0.05,4:RT=0.06": an SDL virtual controller whose
//     axes take those values at those seconds, through the deadzones, and
//     whose rumble is logged with what the game asked for (tests of the
//     Input settings without a person). Buttons too ("5:BACK=1", "5.2:BACK=0",
//     "6:DOWN" for a tap), which reach the menus as a real controller's do.
//     BURNOUT_VIRTUAL_PAD2, the same way, is a second controller (player 2).
//     Both count seconds of guest time (cpu/guest_time.h): a suspension
//     doesn't count, so what falls due during one happens after it.
//   - BURNOUT_INPUT_LOG=1: the left stick and triggers once a second, raw and
//     as the game gets them.
//   - iOS: with no controller (and no keyboard attached) a toast says the
//     game needs one, 5 s after the game starts (NoteGameStarting) and 1 s
//     after the last one goes; one connecting takes it down.
#pragma once
#include <SDL3/SDL_events.h>

namespace hid
{
    // Main thread, after SDL video is up.
    void Initialize();
    // Main thread, as the game itself starts (after the installer, if it
    // ran): on iOS, the "needs a controller" notice is due 5 s later if
    // there is none by then. Elsewhere nothing.
    void NoteGameStarting();
    void HandleEvent(const SDL_Event& event);
    // Main thread, once per presented frame: sample devices.
    void Update();
    // While set (a menu is open), the game sees an idle controller, and
    // after it is cleared until every control has been released.
    void SetBlocked(bool blocked);
    // Main thread: seconds since any control was last pressed or moved (a
    // controller, the keyboard's controls, NFSMW_INPUT_SCRIPT), as of the
    // last Update; from the first Update on.
    double SecondsSinceInput();
    // At exit: stop any rumble (the process may outlive nothing that would).
    void Shutdown();
    // Main thread, from the frame loop (not UIKit's callback), once the game
    // is suspended (iOS: the app isn't active) and once it's back: Suspend
    // stops any rumble and suppresses later requests until Resume. Window
    // focus loss/gain is tracked independently by HandleEvent: both gates
    // must allow output before the game's latest request is sent again.
    void Suspend();
    void Resume();

    // Which buttons our menus' hints show: those of whatever the player used
    // last (a key, or a button or stick of a controller and its family).
    // Starts from what is connected: a PlayStation controller, another
    // controller, else the keyboard. Main thread.
    enum class Glyphs { Xbox, PlayStation, Keyboard };
    Glyphs LastGlyphs();
    // The same choice, readable from any thread (the renderer's game
    // prompts, gpu/pad_glyphs.h): updated as the main thread sees input and
    // controllers come and go.
    Glyphs PublishedGlyphs();
    // The buttons every prompt shows, ours and the game's alike: Settings >
    // Input > Button Prompts' set, or with Auto the device used last
    // (LastGlyphs on the main thread, PublishedGlyphs elsewhere: `anyThread`).
    Glyphs PromptGlyphs(bool anyThread = false);
}
