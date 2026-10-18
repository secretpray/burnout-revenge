// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// An isolated SDL focus/controller probe: no game, Vulkan or OS input injection.
// Logs event counts, never keys/text, serial numbers or device paths.
#include <SDL3/SDL.h>
#include <hid/rumble_focus.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv)
{
    const char* mode = "none";
    int seconds = 120;
    bool audio = false;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--controllers") && i + 1 < argc)
            mode = argv[++i];
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--audio"))
            audio = true;
        else
        {
            std::fprintf(stderr, "Usage: input_probe [--controllers none|native|raw] [--audio] [--seconds 1..600]\n");
            return 2;
        }
    }
    const bool controllers = std::strcmp(mode, "none") != 0;
    const bool native = !std::strcmp(mode, "native"), raw = !std::strcmp(mode, "raw");
    if ((controllers && !native && !raw) || seconds < 1 || seconds > 600)
        return 2;

    // Hints precede SDL initialization. In 'none' mode there is no joystick
    // subsystem at all, even if the OS still has a Bluetooth pad connected.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, raw ? "1" : "0");
    SDL_SetHint(SDL_HINT_HIDAPI_ENUMERATE_ONLY_CONTROLLERS, "1");
    if (raw)
    {
        SDL_SetHint(SDL_HINT_JOYSTICK_MFI, "0");
        SDL_SetHint(SDL_HINT_JOYSTICK_IOKIT, "0");
    }
    SDL_SetAppMetadata("Burnout Input Probe", "development", "io.github.secretpray.BurnoutInputProbe");
    if (!SDL_Init(SDL_INIT_VIDEO | (controllers ? SDL_INIT_GAMEPAD : 0) | (audio ? SDL_INIT_AUDIO : 0)))
    {
        std::fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    std::printf("SDL %d; mode=%s; audio=%s; game/Vulkan absent; no OS input injected\n",
        SDL_GetVersion(), mode, audio ? "on (silence)" : "off");
    SDL_Window* window = SDL_CreateWindow("Burnout input probe — Esc closes", 720, 220, 0);
    if (!window)
    {
        std::fprintf(stderr, "window failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_AudioStream* stream = nullptr;
    if (audio)
    {
        const SDL_AudioSpec spec{ SDL_AUDIO_F32, 2, 48000 };
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream || !SDL_ResumeAudioStreamDevice(stream))
        {
            std::fprintf(stderr, "audio failed: %s\n", SDL_GetError());
            if (stream) SDL_DestroyAudioStream(stream);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
    }

    SDL_Gamepad* pad = nullptr;
    hid::RumbleFocus focus;
    unsigned keyCount = 0, added = 0, removed = 0;
    const Uint64 start = SDL_GetTicks();
    Uint64 titleAt = 0;
    auto openPad = [&] {
        if (!controllers || pad) return;
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        for (int i = 0; i < count && !pad; ++i)
            pad = SDL_OpenGamepad(ids[i]);
        SDL_free(ids);
        if (pad)
        {
            ++added;
            const auto connection = SDL_GetGamepadConnectionState(pad);
            std::printf("%llu ms: controller opened, %04x:%04x, transport=%s\n",
                static_cast<unsigned long long>(SDL_GetTicks()-start),
                SDL_GetGamepadVendor(pad), SDL_GetGamepadProduct(pad),
                connection == SDL_JOYSTICK_CONNECTION_WIRED ? "wired" :
                connection == SDL_JOYSTICK_CONNECTION_WIRELESS ? "wireless" : "unknown");
        }
    };
    openPad();
    bool done = false;
    while (!done && SDL_GetTicks()-start < Uint64(seconds)*1000)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                done = true;
            else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat)
            {
                ++keyCount; // intentionally do not log any key/scancode/text
                if (e.key.key == SDLK_ESCAPE) done = true;
            }
            else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST || e.type == SDL_EVENT_WINDOW_HIDDEN ||
                     e.type == SDL_EVENT_WINDOW_MINIMIZED)
            {
                focus.SetFocused(false);
                SDL_ResetKeyboard();
                if (pad) SDL_RumbleGamepad(pad, 0, 0, 0);
                std::printf("%llu ms: inactive, motors stopped\n", static_cast<unsigned long long>(SDL_GetTicks()-start));
            }
            else if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED || e.type == SDL_EVENT_WINDOW_SHOWN ||
                     e.type == SDL_EVENT_WINDOW_RESTORED)
            {
                const bool active = e.type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
                    (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
                focus.SetFocused(active);
                std::printf("%llu ms: focused=%d, grab=%d\n", static_cast<unsigned long long>(SDL_GetTicks()-start),
                    active, SDL_GetWindowKeyboardGrab(window));
            }
            else if (e.type == SDL_EVENT_GAMEPAD_ADDED)
                openPad();
            else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad && e.gdevice.which == SDL_GetGamepadID(pad))
            {
                ++removed;
                SDL_CloseGamepad(pad);
                pad = nullptr;
                std::printf("%llu ms: controller removed\n", static_cast<unsigned long long>(SDL_GetTicks()-start));
                openPad();
            }
            else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && pad && e.gbutton.which == SDL_GetGamepadID(pad) &&
                     e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH && focus.Active())
            {
                // Only an explicit physical A/Cross press asks for vibration.
                const bool ok = SDL_RumbleGamepad(pad, 16000, 16000, 200);
                std::printf("%llu ms: 200 ms rumble request %s\n", static_cast<unsigned long long>(SDL_GetTicks()-start),
                    ok ? "accepted" : SDL_GetError());
            }
        }
        if (stream && SDL_GetAudioStreamQueued(stream) < 8192)
        {
            const float silence[2048]{};
            if (!SDL_PutAudioStreamData(stream, silence, sizeof(silence)))
            {
                std::fprintf(stderr, "audio queue failed: %s\n", SDL_GetError());
                done = true;
            }
        }
        const auto now = SDL_GetTicks();
        if (now >= titleAt)
        {
            char title[256];
            std::snprintf(title, sizeof(title), "Input probe | %s | focus %s | keys %u | pad %s | A/Cross: rumble | Esc: close",
                mode, focus.Active() ? "yes" : "no", keyCount, pad ? "yes" : "no");
            SDL_SetWindowTitle(window, title);
            if (SDL_Surface* surface = SDL_GetWindowSurface(window))
            {
                SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGB(surface, focus.Active() ? 30 : 55, 30, 30));
                SDL_UpdateWindowSurface(window);
            }
            titleAt = now + 250;
            std::fflush(stdout);
        }
        SDL_Delay(8);
    }
    if (pad)
    {
        SDL_RumbleGamepad(pad, 0, 0, 0);
        // Bounded main-thread drain of pending output, inspired by Takedawn's
        // lifetime rules. Not a guarantee for SIGKILL or a transport failure.
        for (int n = 0; n < 10; ++n) { SDL_UpdateGamepads(); SDL_Delay(5); }
        SDL_CloseGamepad(pad);
    }
    if (stream) SDL_DestroyAudioStream(stream);
    SDL_DestroyWindow(window);
    SDL_Quit(); // no guest workers exist in this diagnostic process
    std::printf("finished: key events=%u, controller opens=%u, removals=%u\n", keyCount, added, removed);
}
