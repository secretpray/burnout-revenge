// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Optional classic Crash launch; diagnostics and the fixed hold experiment
// are enabled separately from the player setting.
#include <stdafx.h>
#include "rewind_probe.h"
#include "crash_launch_model.h"
#include "crash_launch_status.h"
#include "crash_launch_scope.h"
#include <report/report.h>
#include <user/settings.h>
#include <hid/input.h>
#include <cstdlib>
#include <cmath>
#include <optional>

extern "C" PPC_FUNC(__imp__sub_82132AD0);
extern "C" PPC_FUNC(__imp__sub_8227FB28);
extern "C" PPC_FUNC(__imp__sub_82290CE8);
extern "C" PPC_FUNC(__imp__sub_82262128);
extern "C" PPC_FUNC(__imp__sub_8219C750);

namespace { void NetworkTick(PPCContext&, uint8_t*, uint32_t); }

// Native network Crash variants share this state machine. Diagnostics read
// their participant mapping without changing launch timing or network state.
extern "C" PPC_FUNC(__imp__sub_8213FDA0);
PPC_FUNC(sub_8213FDA0)
{
    const uint32_t event = ctx.r3.u32;
    __imp__sub_8213FDA0(ctx, base);
    NetworkTick(ctx, base, event);
    static const bool log = std::getenv("BURNOUT_LOG_NETWORK_CRASH") != nullptr;
    if (!log || (event != 0x82888190 && event != 0x82889780 && event != 0x8288AC70)) return;
    static thread_local uint32_t lastEvent{}, lastState = UINT32_MAX;
    const uint32_t state = PPC_LOAD_U32(event + 3488);
    if (event == lastEvent && state == lastState) return;
    lastEvent = event; lastState = state;
    fprintf(stderr, "[network-crash] event=%08X vtable=%08X state=%u session=%08X players=%u world_type=%u clock=%.6f",
        event,PPC_LOAD_U32(event),state,PPC_LOAD_U32(game::codes::kSession),
        PPC_LOAD_U32(game::codes::kLocalPlayers),PPC_LOAD_U32(0x82CE000C),
        std::bit_cast<float>(PPC_LOAD_U32(0x82AB6FA4)));
    for (unsigned i=0;i<6;++i)
        fprintf(stderr," p%u=%u/%u",i,PPC_LOAD_U32(event+2396+i*36),PPC_LOAD_U8(0x82CE0030+i*19200+15480));
    fprintf(stderr," car=%08X\n",PPC_LOAD_U32(0x82CE0030+18240));
}

namespace
{
    constexpr uint32_t kWorld = 0x82AB6F80;
    constexpr uint32_t kEvent = game::crash_launch::kSoloEvent;
    constexpr uint32_t kParticipant = 0x82CE0030;

    struct HoldExperiment
    {
        bool active{}, deferred{};
        float releaseAt{};
        uint32_t heldSteps{};
        bool meter{}, failed{}, finalized{}, nativeMessage{};
        float lastWorld{}, resultAt{};
        game::crash_launch::Model model;
    };
    thread_local HoldExperiment hold;
    struct LaunchFrame { float time; game::crash_launch::Model model; };
    struct ControlFrame { float time; std::array<uint32_t, 9> words; };
    struct LaunchRecording
    {
        bool complete{};
        float releaseAt{};
        float originalMaximum{};
        uint32_t gearCount{};
        std::array<float, 12> originalGears{};
        std::vector<LaunchFrame> frames;
        std::vector<ControlFrame> controls;
    };
    thread_local LaunchRecording recording;
    thread_local HoldExperiment replayHold;

    thread_local std::optional<bool> eventMeterChoice;
    bool RequestedMeter()
    {
        static const bool enabled = [] {
            const char* value = std::getenv("BURNOUT_CRASH_LAUNCH_METER_PROBE");
            return value && std::string_view(value) == "1";
        }();
        return enabled || settings::GetBool(settings::Id::CrashLaunchMeter);
    }
    bool MeterEnabled() { return eventMeterChoice.value_or(RequestedMeter()); }

    bool Diagnostics()
    {
        static const bool enabled = [] {
            const char* value = std::getenv("BURNOUT_CRASH_LAUNCH_PROBE");
            return value && std::string_view(value) == "1";
        }();
        return enabled;
    }

    template<class... Args> void Trace(const char* format, Args... args)
    {
        if (!Diagnostics()) return;
        if constexpr (sizeof...(Args)==0) fputs(format,stderr);
        else fprintf(stderr,format,args...);
    }

    float HoldSeconds()
    {
        if (MeterEnabled()) return 10.f;
        static const float seconds = [] {
            const char* value = std::getenv("BURNOUT_CRASH_LAUNCH_HOLD_SECONDS");
            const char* rewind = std::getenv("BURNOUT_REWIND_ENABLED");
            if (!rewind || std::string_view(rewind) != "0") return 0.f;
            if (!value) return 0.f;
            char* end = nullptr;
            const float parsed = std::strtof(value, &end);
            return end && !*end && std::isfinite(parsed) && parsed >= .1f && parsed <= 10.f ? parsed : 0.f;
        }();
        return seconds;
    }

    bool Enabled(uint8_t* base)
    {
        static const bool enabled = [] {
            const char* value = std::getenv("BURNOUT_CRASH_LAUNCH_PROBE");
            return value && std::string_view(value) == "1";
        }();
        return (enabled || MeterEnabled()) && game::crash_launch::SoloEvent(base);
    }

    bool HoldScope(uint8_t* base, bool allowPlayback = false)
    {
        if (!Enabled(base) || HoldSeconds() == 0 ||
            PPC_LOAD_U32(game::rewind::kPhase) != 5 ||
            PPC_LOAD_U32(kEvent) != 0x82014870 ||
            // This flag is 1 in solo Crash; 0 is the ordinary driving scope
            // used by the codes module. Do not reuse that module's predicate.
            PPC_LOAD_U8(game::codes::kSpecialMode) != 1 ||
            (!allowPlayback && PPC_LOAD_U8(game::rewind::kWorldPlayback) != 0)) return false;
        const uint32_t controller = PPC_LOAD_U32(game::codes::kSoloSession + 8);
        return controller && controller != game::codes::kPartyController &&
            controller != game::codes::kCrashController;
    }

    float ReadFloat(uint8_t* base, uint32_t address)
    {
        return std::bit_cast<float>(PPC_LOAD_U32(address));
    }

    void WriteFloat(uint8_t* base, uint32_t address, float value)
    {
        PPC_STORE_U32(address, std::bit_cast<uint32_t>(value));
    }

    bool ReplayingLaunch(uint8_t* base)
    {
        return MeterEnabled() && recording.complete && HoldScope(base, true) &&
            PPC_LOAD_U8(game::rewind::kWorldPlayback) != 0;
    }

    void ApplyMeterEngine(uint8_t* base, uint32_t physical,
        const game::crash_launch::Model& model = hold.model)
    {
        // Retail pending UpdateBoostStart drives the two rear wheels and the
        // transmission directly. USA 360's countdown update uses these same
        // fields; its sound-physics update reads engine velocity at +6708.
        WriteFloat(base, physical + 6544, model.engineAcceleration);
        WriteFloat(base, physical + 6708, model.EngineAngularVelocity(ReadFloat(base, physical + 6672)));
        for (uint32_t wheel : {1888u, 2080u})
        {
            WriteFloat(base, physical + wheel + 60, model.WheelAngularVelocity());
            PPC_STORE_U32(physical + wheel + 92, 1);
        }
    }

    bool LaunchPressed(const PPCContext& ctx, uint8_t* base)
    {
        const uint32_t mapping = game::rewind::kGame;
        const auto index = int8_t(PPC_LOAD_U8(mapping + 2585));
        if (index < 0 || index > 3) return false;
        (void)ctx;
        // Action 7 maps to raw button axis 16 in this executable. Read its
        // current analog value, not sub_821118D0's one-frame/repeat pulse.
        const uint32_t axis = PPC_LOAD_U32(0x820130C8 + 7 * 4);
        if (axis >= 28) return false;
        // sub_82108770 stores the current value at actionState+4. These
        // processed action states start at mapping+8, stride12; their +0
        // pulse is what sub_821118D0 reads. They are not device objects.
        const float value = ReadFloat(base, mapping + 1156 + uint32_t(index) * 360 + 12 + axis * 12);
        return std::isfinite(value) && value > .05f;
    }

    void LogHudComponents(uint8_t* base)
    {
        constexpr uint32_t hud = 0x82A51300;
        uint32_t node = PPC_LOAD_U32(hud + 1096);
        for (unsigned i = 0; node && i < 128; ++i)
        {
            if (node < 0x82000000 || node > 0xBFFFF000) break;
            const uint32_t table = PPC_LOAD_U32(node);
            if (table < 0x82000000 || table > 0x82FFFFFF) break;
            Trace( "[crash-hud-component] object=%08X vtable=%08X update=%08X transition=%08X flags=%08X\n",
                node, table, PPC_LOAD_U32(table), PPC_LOAD_U32(table + 4), PPC_LOAD_U32(node + 20));
            node = PPC_LOAD_U32(node + 12);
        }
    }

    bool ReleaseMeterCar(PPCContext& ctx, uint8_t* base, HoldExperiment& launch = hold)
    {
        using game::crash_launch::Result;
        const uint32_t physical = PPC_LOAD_U32(kParticipant + 18240);
        const bool playback = &launch == &replayHold;
        const float oldMaximum = playback ? recording.originalMaximum : ReadFloat(base, physical + 6484);
        const uint32_t gears = playback ? recording.gearCount : PPC_LOAD_U32(physical + 6736);
        if (!std::isfinite(oldMaximum) || oldMaximum <= 10 || gears == 0 || gears > 12)
        {
            launch = {};
            fprintf(stderr, "[crash-meter] abort=invalid-car-parameters\n");
            return false;
        }
        std::array<float, 12> oldGears{};
        for (uint32_t i = 0; i < gears; ++i)
            oldGears[i] = playback ? recording.originalGears[i] : ReadFloat(base, physical + 6620 + i * 4);
        if (!playback)
        {
            recording.originalMaximum = oldMaximum;
            recording.gearCount = gears;
            recording.originalGears = oldGears;
        }
        else if (PPC_LOAD_U32(kEvent + 2396) == 7)
        {
            // A checkpoint after launch already contains its scaled car and
            // effects. Do not rescale gears or explode it again after a seek.
            bool restored = ReadFloat(base, physical + 6484) == launch.model.Maximum(oldMaximum);
            for (uint32_t i = 0; i < gears; ++i)
                restored = restored && ReadFloat(base, physical + 6620 + i * 4) == oldGears[i] * launch.model.GearScale(oldMaximum);
            if (restored)
            {
                launch.finalized = true;
                Trace( "[crash-meter-replay] restored_launch=1 world=%.6f\n", ReadFloat(base, kWorld + 36));
                return false;
            }
        }
        PPCContext invoke = ctx;
        invoke.r3.u64 = kEvent;
        invoke.r4.u64 = 0;
        invoke.r5.u64 = 1;
        __imp__sub_82132AD0(invoke, base);
        const auto& model = launch.model;
        const float maximum = model.Maximum(oldMaximum);
        for (uint32_t i = 0; i < gears; ++i)
            WriteFloat(base, physical + 6620 + i * 4, oldGears[i] * model.GearScale(oldMaximum));
        WriteFloat(base, physical + 6484, maximum);
        const uint32_t descriptor = PPC_LOAD_U32(physical + 6532);
        WriteFloat(base, descriptor + 448, maximum);
        WriteFloat(base, kEvent + 2856, model.fraction);
        WriteFloat(base, kEvent + 2864, model.steering);
        const bool power = model.result == Result::Power;
        PPC_STORE_U8(kEvent + 2880, power);
        PPC_STORE_U8(kEvent + 2916, power);
        PPC_STORE_U8(kParticipant + 13470, power);
        PPC_STORE_U8(kParticipant + 13471, 0);
        PPC_STORE_U8(physical + 6614, model.result == Result::Power || model.result == Result::Fast);
        PPC_STORE_U8(kEvent + 2408, model.result == Result::Stall || model.result == Result::Blown);
        launch.finalized = true;
        Trace( "[crash-meter-car] world=%.6f old_max=%.7g maximum=%.7g fraction=%.7g power=%u gears=%u scale=%.7g",
            ReadFloat(base, kWorld + 36), oldMaximum, ReadFloat(base, physical + 6484),
            model.fraction, power, gears, maximum / oldMaximum + .1f);
        for (uint32_t i = 0; i < gears; ++i)
            Trace( " gear%u=%.7g:%.7g", i, oldGears[i], ReadFloat(base, physical + 6620 + i * 4));
        Trace( "\n");
        return true;
    }

    void BlowOutEffects(const PPCContext& ctx, uint8_t* base)
    {
        const uint32_t physical = PPC_LOAD_U32(kParticipant + 18240);
        // Retail DoBlowOut starts a native crash, explodes at size .3 without
        // contagious/physical blast, then fires an upward air ram at .4.
        // Body-part blowout damage and its dedicated camera event remain to
        // be mapped; this bounded experiment reuses the native 360 effects.
        PPCContext invoke = ctx;
        invoke.r3.u64 = kWorld + 0x47098;
        invoke.r4.u64 = physical;
        invoke.r5.u64 = 15;
        invoke.r6.u64 = invoke.r7.u64 = invoke.r8.u64 = invoke.r9.u64 = invoke.r10.u64 = 0;
        __imp__sub_8227FB28(invoke, base);
        invoke = ctx;
        invoke.r3.u64 = physical;
        invoke.r5.u64 = invoke.r6.u64 = 0;
        invoke.f1.f64 = .3f;
        invoke.f2.f64 = 1.f;
        __imp__sub_82290CE8(invoke, base);
        invoke = ctx;
        invoke.r3.u64 = physical;
        invoke.r4.u64 = 9;
        invoke.r6.u64 = 0;
        invoke.f1.f64 = .4f;
        __imp__sub_82262128(invoke, base);
        Trace( "[crash-meter-blowout] world=%.6f native_crash=%u explosion=.3 air_ram=.4 body_parts=pending camera_event=pending\n",
            ReadFloat(base, kWorld + 36), PPC_LOAD_U8(physical + 599));
    }

    void UpdateReplayLaunch(PPCContext& ctx, uint8_t* base)
    {
        if (!ReplayingLaunch(base)) { replayHold = {}; return; }
        if (PPC_LOAD_U32(kEvent + 2384) != 18) return;
        const float time = ReadFloat(base, kWorld + 36);
        if (time < replayHold.lastWorld) replayHold = {};
        replayHold.lastWorld = time;
        auto frame = std::upper_bound(recording.frames.begin(), recording.frames.end(), time,
            [](float t, const LaunchFrame& value) { return t < value.time; });
        if (frame == recording.frames.begin()) return;
        replayHold.model = std::prev(frame)->model;
        replayHold.active = time < recording.releaseAt;
        if (replayHold.active)
            ApplyMeterEngine(base, PPC_LOAD_U32(kParticipant + 18240), replayHold.model);
        else if (!replayHold.finalized)
        {
            // The input stream alone cannot reconstruct host-side launch
            // rules. Replay uses the recorded result at its original time.
            const bool newlyFinalized = ReleaseMeterCar(ctx, base, replayHold);
            using game::crash_launch::Result;
            if (replayHold.model.result == Result::Stall || replayHold.model.result == Result::Blown)
                WriteFloat(base, kEvent + 2772, recording.releaseAt + 5.f);
            if (newlyFinalized && replayHold.model.result == Result::Blown) BlowOutEffects(ctx, base);
            Trace( "[crash-meter-replay] release=%.6f recorded=%.6f result=%u fraction=%.7g steering=%.7g\n",
                time, recording.releaseAt, unsigned(replayHold.model.result),
                replayHold.model.fraction, replayHold.model.steering);
        }
    }

    bool ShowLaunchMessage(const PPCContext& ctx, uint8_t* base, const game::crash_launch::Model& model = hold.model)
    {
        using game::crash_launch::Result;
        // USA 360's retained bonus map at 825F4064..825F40C7 supplies
        // localized text, texture AgressivePos, and the original animation.
        const uint32_t event = model.result == Result::Stall ? 204 :
            model.result == Result::Fast ? 206 : model.result == Result::Power ? 207 :
            model.result == Result::Blown ? 208 : 205;
        PPCContext invoke = ctx;
        invoke.r3.u64 = 0x82A51300;
        invoke.r4.u64 = event;
        invoke.r5.u64 = 0;
        invoke.r6.s64 = -1;
        invoke.r7.u64 = 1;
        invoke.r8.u64 = 0;
        invoke.r9.s64 = -1;
        invoke.r10.u64 = 1;
        invoke.f1.f64 = 0;
        // The final argument is passed in this caller's parameter area.
        const uint32_t argument = ctx.r1.u32 + 92;
        const uint32_t saved = PPC_LOAD_U32(argument);
        PPC_STORE_U32(argument, 0);
        __imp__sub_8219C750(invoke, base);
        PPC_STORE_U32(argument, saved);
        const bool accepted = invoke.r3.u32 != 0;
        Trace( "[crash-meter-message] event=%u accepted=%u world=%.6f\n",
            event, accepted, ReadFloat(base, kWorld + 36));
        return accepted;
    }

    struct NetworkLaunch
    {
        uint32_t event{}, participant{};
        bool started{}, previousPress{}, deferred{}, finalized{}, nativeMessage{};
        float lastWorld{}, startedAt{}, resultAt{};
        game::crash_launch::Model model;
    };
    thread_local NetworkLaunch network;

    bool NetworkScope(uint8_t* base, uint32_t event)
    {
        if (!MeterEnabled() || !game::crash_launch::NetworkEvent(base,event) ||
            PPC_LOAD_U32(game::rewind::kPhase)!=5 || PPC_LOAD_U8(game::rewind::kWorldPlayback)) return false;
        const uint32_t table=PPC_LOAD_U32(event);
        return table==0x82016B50 || table==0x82016D68 || table==0x82016F80;
    }
    bool NetworkHolding(uint8_t* base)
    {
        return network.event && !network.finalized && NetworkScope(base,network.event) &&
            PPC_LOAD_U32(network.event+3488)>=11 && PPC_LOAD_U32(network.event+3488)<=12;
    }
    void ReleaseNetworkCar(const PPCContext& ctx, uint8_t* base)
    {
        using namespace game::crash_launch;
        const uint32_t physical=PPC_LOAD_U32(kParticipant+18240);
        const float maximum=ReadFloat(base,physical+6484);
        const uint32_t gears=PPC_LOAD_U32(physical+6736);
        if (!std::isfinite(maximum) || maximum<=10 || gears==0 || gears>12)
        {
            PPCContext fallback=ctx;
            fallback.r3.u64=network.event; fallback.r4.u64=0; fallback.r5.u64=1;
            __imp__sub_82132AD0(fallback,base);
            network={};
            return;
        }
        std::array<float,12> ratios{};
        for (unsigned i=0;i<gears;++i) ratios[i]=ReadFloat(base,physical+6620+i*4);
        PPCContext invoke=ctx;
        invoke.r3.u64=network.event; invoke.r4.u64=0; invoke.r5.u64=1;
        __imp__sub_82132AD0(invoke,base);
        const auto& model=network.model;
        const float scaled=model.Maximum(maximum);
        for (unsigned i=0;i<gears;++i) WriteFloat(base,physical+6620+i*4,ratios[i]*model.GearScale(maximum));
        WriteFloat(base,physical+6484,scaled);
        WriteFloat(base,PPC_LOAD_U32(physical+6532)+448,scaled);
        WriteFloat(base,network.event+2856,model.fraction);
        WriteFloat(base,network.event+2864,model.steering);
        const bool power=model.result==Result::Power;
        PPC_STORE_U8(network.event+2880,power);
        PPC_STORE_U8(network.event+2916,power);
        PPC_STORE_U8(kParticipant+13470,power);
        PPC_STORE_U8(kParticipant+13471,0);
        PPC_STORE_U8(physical+6614,power || model.result==Result::Fast);
        PPC_STORE_U8(network.event+2408+network.participant*36,model.result==Result::Stall || model.result==Result::Blown);
        network.finalized=true;
        network.resultAt=ReadFloat(base,kWorld+36);
        if (model.result==Result::Blown) BlowOutEffects(ctx,base);
        network.nativeMessage=ShowLaunchMessage(ctx,base,model);
        // Keep the native network deadline, turn progression and transport.
        // Only this peer's local car receives its chosen launch outcome.
        fprintf(stderr,"[crash-meter-network] released event=%08X participant=%u result=%u maximum=%.7g world=%.6f\n",
            network.event,network.participant,unsigned(model.result),scaled,network.resultAt);
    }
    bool NetworkFinalize(const PPCContext& ctx, uint8_t* base)
    {
        if (ctx.r4.u32!=0 || network.event!=ctx.r3.u32 || !NetworkScope(base,network.event)) return false;
        const uint32_t state=PPC_LOAD_U32(network.event+3488);
        if (state<11 || state>12) return false;
        if (network.finalized) return true;
        network.deferred=true;
        if (network.started && network.model.phase==game::crash_launch::Phase::Done)
            ReleaseNetworkCar(ctx,base);
        return true;
    }
    void NetworkTick(PPCContext& ctx, uint8_t* base, uint32_t event)
    {
        using namespace game::crash_launch;
        if (!NetworkScope(base,event)) { network={}; return; }
        const uint32_t state=PPC_LOAD_U32(event+3488);
        if (state<11 || state>12) { network={}; return; }
        const float world=ReadFloat(base,kWorld+36);
        const bool pressed=LaunchPressed(ctx,base);
        if (!network.event && state==11)
        {
            const uint32_t participant=PPC_LOAD_U8(kParticipant+15480);
            if (participant>=6 || PPC_LOAD_U32(event+2396+participant*36)!=6) return;
            network.event=event; network.participant=participant;
            network.lastWorld=world; network.previousPress=pressed;
            auto& p=network.model.parameters;
            for (unsigned i=0;i<6;++i) p.limits[i]=ReadFloat(base,kParticipant+13424+i*4);
            p.multiplier=ReadFloat(base,kParticipant+13448);
            p.minimumSpeed=ReadFloat(base,kParticipant+13452);
            if (!p.Valid()) { network={}; return; }
            hid::ScriptEvent("crashwaiting");
            fprintf(stderr,"[crash-meter-network] ready event=%08X participant=%u\n",event,participant);
        }
        if (network.event!=event) return;
        if (!network.started && pressed && !network.previousPress)
        {
            network.started=true; network.startedAt=world;
            hid::ScriptEvent("crashlaunch");
        }
        network.previousPress=pressed;
        const float dt=world>network.lastWorld ? ReadFloat(base,kWorld+32):0.f;
        network.lastWorld=world;
        if (network.started && network.model.phase!=Phase::Done)
            network.model.Step(dt,pressed);
        if (!network.finalized && network.started && network.deferred && network.model.phase==Phase::Done)
            ReleaseNetworkCar(ctx,base);
        if (!network.event) return;
        if (!network.finalized)
        {
            ApplyMeterEngine(base,PPC_LOAD_U32(kParticipant+18240),network.model);
            WriteFloat(base,event+2416+network.participant*36,network.model.level);
        }
        const float age=world-network.resultAt;
        Status status;
        status.visible=!network.finalized || age<2.5f;
        status.waiting=!network.started;
        status.showStartPrompt=status.waiting;
        status.nativeMessage=network.nativeMessage;
        status.time=world-network.startedAt;
        status.alpha=network.finalized ? std::clamp(1.f-(age-.5f)*.5f,0.f,1.f):1.f;
        status.model=network.model;
        hudStatus.Publish(status);
    }

    void Log(uint8_t* base, const char* point, uint32_t caller)
    {
        if (!Diagnostics()) return;
        auto f = [base](uint32_t address) { return std::bit_cast<float>(PPC_LOAD_U32(address)); };
        Trace( "[crash-launch] point=%s wall=%.6f caller=%08X phase=%u state=%u world=%.6f session=%.6f dt=%.6f flags=%08X playback=%u deadline=%.6f traffic_start=%.6f traffic_enabled=%u clock_run=%u rng=%08X:%08X",
            point, report::SessionSeconds(), caller, PPC_LOAD_U32(game::rewind::kPhase),
            PPC_LOAD_U32(kEvent + 2384), f(kWorld + 36), f(game::rewind::kGame + 128),
            f(kWorld + 32), PPC_LOAD_U32(game::rewind::kClockFlags),
            PPC_LOAD_U8(game::rewind::kWorldPlayback), f(kEvent + 2772),
            f(kWorld + 0x47068), PPC_LOAD_U8(kWorld + 0x4708D),
            PPC_LOAD_U8(kWorld + 44), PPC_LOAD_U32(game::rewind::kGame + 0x33088),
            PPC_LOAD_U32(game::rewind::kGame + 0x3308C));
        for (uint32_t offset : {2396u, 2420u, 2832u, 2880u, 2890u, 2891u, 3480u})
            Trace( " e%u=%08X", offset, PPC_LOAD_U32(kEvent + offset));
        Trace( " accel=%.6f steer=%.6f car_mode=%u car_id=%d",
            f(kEvent + 2856), f(kEvent + 2864), PPC_LOAD_U32(kParticipant + 13520),
            int(int8_t(PPC_LOAD_U8(kParticipant + 15480))));
        Trace( " bar=%.6f p15219=%u physical=%08X hold_active=%u held_steps=%u",
            f(kEvent + 2416), PPC_LOAD_U8(kParticipant + 15219),
            PPC_LOAD_U32(kParticipant + 18240), hold.active, hold.heldSteps);
        Trace( " vtable=%08X controller=%08X special=%u hold_seconds=%.3f",
            PPC_LOAD_U32(kEvent), PPC_LOAD_U32(game::codes::kSoloSession + 8),
            PPC_LOAD_U8(game::codes::kSpecialMode), HoldSeconds());
        // The fixed local participant is valid throughout the solo event.
        for (uint32_t offset : {64u, 68u, 72u, 80u, 84u, 88u})
            Trace( " p%u=%.7g", offset, f(kParticipant + offset));
        Trace( "\n");
        // USA sub_82340378 constructs 254 traffic records, stride 512, at
        // traffic+0x131D0; sub_8232C958 initializes their matrix at +112.
        // Do not dereference streamed vehicle/body pointers while loading.
        // traffic+0x42078 and +0x4209D alias world+0x47068/+0x4708D.
        constexpr uint32_t traffic = kWorld + 20464;
        uint64_t positions = 14695981039346656037ULL;
        uint32_t nonzero = 0;
        for (uint32_t i = 0; i < 254; ++i)
        {
            const uint32_t car = traffic + 0x131D0 + i * 512;
            for (uint32_t offset : {160u, 164u, 168u})
                positions = (positions ^ PPC_LOAD_U32(car + offset)) * 1099511628211ULL;
            const float x = f(car + 160), y = f(car + 164), z = f(car + 168);
            if (x == 0 && y == 0 && z == 0) continue;
            if (++nonzero <= 8)
                Trace( "[crash-traffic] wall=%.6f world=%.6f state=%u slot=%u xyz=%.7g,%.7g,%.7g speed=%.7g flags=%02X\n",
                    report::SessionSeconds(), f(kWorld + 36), PPC_LOAD_U32(kEvent + 2384),
                    i, x, y, z, f(car + 196), PPC_LOAD_U8(car + 492));
        }
        Trace( "[crash-traffic] wall=%.6f world=%.6f state=%u nonzero=%u position_hash=%016llX seed=%08X:%08X active=%u enabled=%u\n",
            report::SessionSeconds(), f(kWorld + 36), PPC_LOAD_U32(kEvent + 2384), nonzero,
            (unsigned long long)positions, PPC_LOAD_U32(traffic + 0x3EC48),
            PPC_LOAD_U32(traffic + 0x3EC4C), PPC_LOAD_U8(traffic + 0x3EC94),
            PPC_LOAD_U8(traffic + 0x4209D));
    }
}

extern "C" PPC_FUNC(__imp__sub_821398C0);
PPC_FUNC(sub_821398C0)
{
    const bool observe = ctx.r3.u32 == kEvent && Enabled(base);
    const uint32_t caller = uint32_t(ctx.lr);
    const uint32_t before = observe ? PPC_LOAD_U32(kEvent + 2384) : 0;
    if (Diagnostics() && observe && MeterEnabled() && before == 18 && (hold.meter || recording.complete))
    {
        const uint32_t physical = PPC_LOAD_U32(kParticipant + 18240);
        Trace( "[crash-meter-path] playback=%u world=%.9g xyz=%.9g,%.9g,%.9g direction=%.9g,%.9g,%.9g acceleration=%.9g steering=%.9g crashed=%u\n",
            PPC_LOAD_U8(game::rewind::kWorldPlayback), ReadFloat(base, kWorld + 36),
            ReadFloat(base, kParticipant + 64), ReadFloat(base, kParticipant + 68), ReadFloat(base, kParticipant + 72),
            ReadFloat(base, kParticipant + 80), ReadFloat(base, kParticipant + 84), ReadFloat(base, kParticipant + 88),
            ReadFloat(base, physical + 6544), ReadFloat(base, physical + 6552), PPC_LOAD_U8(physical + 599));
    }
    if (observe && MeterEnabled()) UpdateReplayLaunch(ctx, base);
    if (hold.active)
    {
        if (!HoldScope(base) || before != 18 || PPC_LOAD_U32(kEvent + 2396) != 6) hold = {};
        else if (hold.meter)
        {
            using namespace game::crash_launch;
            const float world = ReadFloat(base, kWorld + 36);
            // Sample the native simulation delta, not the subtraction of two
            // accumulated float clocks. The latter changes zone boundaries
            // through rounding drift. A paused/repeated world tick does no work.
            const float dt = world > hold.lastWorld ? ReadFloat(base, kWorld + 32) : 0.f;
            hold.lastWorld = world;
            if (!hold.failed)
            {
                const bool pressed = LaunchPressed(ctx, base);
                hold.model.Step(dt, pressed);
                // Sound consumes the transmission during the native event
                // update below, before the next driving-controls callback.
                ApplyMeterEngine(base, PPC_LOAD_U32(kParticipant + 18240));
                WriteFloat(base, kEvent + 2416, hold.model.level);
                if (recording.frames.size() < 4096 && dt > 0)
                    recording.frames.push_back({world, hold.model});
                Trace( "[crash-meter] world=%.6f dt=%.6f phase=%u level=%.6f upper=%.6f stopped=%u pressed=%u result=%u fraction=%.6f steering=%.6f\n",
                    world, dt, unsigned(hold.model.phase), hold.model.level, hold.model.upper,
                    hold.model.upperStopped, pressed, unsigned(hold.model.result), hold.model.fraction, hold.model.steering);
                if (hold.model.phase == Phase::Done)
                {
                    hold.resultAt = world;
                    recording.releaseAt = world;
                    recording.complete = recording.frames.size() < 4096;
                    Log(base, "meter-outcome", caller);
                    const auto outcome = hold.model.result;
                    ReleaseMeterCar(ctx, base);
                    hold.active = false;
                    if (outcome == Result::Stall || outcome == Result::Blown)
                    {
                        // Retail failures still finalize the car and give the
                        // resulting failure five seconds of active world time.
                        hold.failed = true;
                        WriteFloat(base, kEvent + 2772, world + 5.f);
                        if (outcome == Result::Blown) BlowOutEffects(ctx, base);
                    }
                    hold.nativeMessage = ShowLaunchMessage(ctx, base);
                    Log(base, "meter-release-post", caller);
                }
            }
        }
        else if (std::bit_cast<float>(PPC_LOAD_U32(kWorld + 36)) >= hold.releaseAt)
        {
            // Native finalization changes the car only; it does not reset the
            // world, deadline or traffic. Use this live stack, not a saved one.
            hold.active = false;
            Log(base, "hold-release-pre", caller);
            if (hold.deferred)
            {
                PPCContext invoke = ctx;
                invoke.r3.u64 = kEvent;
                invoke.r4.u64 = 0;
                invoke.r5.u64 = 1;
                __imp__sub_82132AD0(invoke, base);
            }
            Log(base, "hold-release-post", caller);
        }
    }
    if (observe)
    {
        static thread_local double next = 0;
        static thread_local uint32_t last = UINT32_MAX;
        const double now = report::SessionSeconds();
        if (last != before || now >= next)
        {
            Log(base, "update-pre", caller);
            next = now + .25;
            last = before;
        }
    }
    __imp__sub_821398C0(ctx, base);
    if (observe && MeterEnabled() && before != 16 && PPC_LOAD_U32(kEvent + 2384) == 16)
        hid::ScriptEvent("crashwaiting");
    game::crash_launch::rewindBlocked.store(hold.active && hold.meter && HoldScope(base), std::memory_order_release);
    if (observe && before != PPC_LOAD_U32(kEvent + 2384)) Log(base, "update-post", caller);
    if (observe && MeterEnabled())
    {
        const auto state = PPC_LOAD_U32(kEvent + 2384);
        const float time = ReadFloat(base, kWorld + 36);
        const bool waiting = state == 16;
        const bool done = hold.model.phase == game::crash_launch::Phase::Done;
        const float age = time - hold.resultAt;
        game::crash_launch::Status status;
        status.visible = HoldScope(base) &&
            (waiting || game::rewind::IsLiveClock(PPC_LOAD_U32(game::rewind::kClockFlags))) &&
            (waiting || (hold.meter && state == 18 && (!done || age < 2.5f)));
        status.waiting = waiting;
        status.nativeMessage = !waiting && hold.nativeMessage;
        status.time = waiting ? 0 : time;
        status.alpha = done && !waiting ? std::clamp(1.f - (age - .5f) * .5f, 0.f, 1.f) : 1;
        status.model = waiting ? game::crash_launch::Model{} : hold.model;
        if (waiting)
            for (unsigned i = 0; i < 6; ++i)
                status.model.parameters.limits[i] = ReadFloat(base, kParticipant + 13424 + i * 4);
        game::crash_launch::hudStatus.Publish(status);
    }
}

extern "C" PPC_FUNC(__imp__sub_821320A8);
PPC_FUNC(sub_821320A8)
{
    const bool observe = ctx.r3.u32 == kEvent && Enabled(base);
    const uint32_t caller = uint32_t(ctx.lr);
    if (observe) Log(base, "launch-pre", caller);
    __imp__sub_821320A8(ctx, base);
    if (observe) Log(base, "launch-post", caller);
    if (observe && HoldScope(base) && caller == 0x8213A77C &&
        PPC_LOAD_U32(kEvent + 2384) == 17 && PPC_LOAD_U32(kEvent + 2396) == 6)
    {
        hold = {true, false, std::bit_cast<float>(PPC_LOAD_U32(kWorld + 36)) + HoldSeconds(), 0};
        if (MeterEnabled())
        {
            auto& parameters = hold.model.parameters;
            constexpr uint32_t burn = kParticipant + 13280;
            for (uint32_t i = 0; i < 6; ++i) parameters.limits[i] = ReadFloat(base, burn + 144 + i * 4);
            parameters.multiplier = ReadFloat(base, burn + 168);
            parameters.minimumSpeed = ReadFloat(base, burn + 172);
            if (!parameters.Valid())
            {
                hold = {};
                fprintf(stderr, "[crash-meter] abort=invalid-meter-parameters\n");
            }
            else
            {
                hold.meter = true;
                game::crash_launch::rewindBlocked.store(true, std::memory_order_release);
                recording = {};
                recording.frames.push_back({0, hold.model});
                hid::ScriptEvent("crashlaunch");
                if (Diagnostics()) LogHudComponents(base);
                Trace( "[crash-meter-parameters] limits=%.7g,%.7g,%.7g,%.7g,%.7g,%.7g multiplier=%.7g minimum_speed=%.7g\n",
                    parameters.limits[0], parameters.limits[1], parameters.limits[2],
                    parameters.limits[3], parameters.limits[4], parameters.limits[5],
                    parameters.multiplier, parameters.minimumSpeed);
            }
        }
        Log(base, "hold-armed", caller);
    }
}

PPC_FUNC(sub_82132AD0)
{
    if (NetworkFinalize(ctx, base)) return;
    static const bool logNetwork = std::getenv("BURNOUT_LOG_NETWORK_CRASH") != nullptr;
    if (logNetwork && (ctx.r3.u32 == 0x82888190 || ctx.r3.u32 == 0x82889780 || ctx.r3.u32 == 0x8288AC70))
        fprintf(stderr,"[network-crash-finalize] event=%08X driver=%u caller=%08X state=%u\n",
            ctx.r3.u32,ctx.r4.u32,uint32_t(ctx.lr),PPC_LOAD_U32(ctx.r3.u32+3488));
    if (ctx.r3.u32 == kEvent && ctx.r4.u32 == 0 && ReplayingLaunch(base))
    {
        // The ordinary 360 countdown must not grant a different launch in
        // Replay. UpdateReplayLaunch finalizes the recorded outcome once.
        Trace( "[crash-meter-replay] suppress_native_finalize=1 world=%.6f\n", ReadFloat(base, kWorld + 36));
        return;
    }
    const bool observe = ctx.r3.u32 == kEvent && ctx.r4.u32 == 0 && Enabled(base);
    if (observe) Log(base, "finalize-pre", uint32_t(ctx.lr));
    if (observe && hold.meter && hold.finalized && HoldScope(base) &&
        PPC_LOAD_U32(kEvent + 2384) == 18 && PPC_LOAD_U32(kEvent + 2396) == 7 &&
        uint32_t(ctx.lr) == 0x820EBF38)
    {
        // A quick meter can finish before the participant's countdown callback.
        // Its pending callback must not grant power or rescale the gears again.
        Log(base, "finalize-duplicate-suppressed", uint32_t(ctx.lr));
        return;
    }
    if (observe && hold.active && HoldScope(base) &&
        PPC_LOAD_U32(kEvent + 2384) == 18 &&
        (hold.meter || std::bit_cast<float>(PPC_LOAD_U32(kWorld + 36)) < hold.releaseAt))
    {
        hold.deferred = true;
        Log(base, "finalize-deferred", uint32_t(ctx.lr));
        return;
    }
    if (observe) hold.active = false;
    __imp__sub_82132AD0(ctx, base);
    if (observe) Log(base, "finalize-post", uint32_t(ctx.lr));
}

extern "C" PPC_FUNC(__imp__sub_8229A0F8);
PPC_FUNC(sub_8229A0F8)
{
    if (NetworkHolding(base) && ctx.r3.u32 == PPC_LOAD_U32(kParticipant + 18240))
        PPC_STORE_U8(ctx.r3.u32 + 14654, 1);
    if (replayHold.active && ReplayingLaunch(base) &&
        ctx.r3.u32 == PPC_LOAD_U32(kParticipant + 18240))
        PPC_STORE_U8(ctx.r3.u32 + 14654, 1);
    if (hold.active && HoldScope(base) && PPC_LOAD_U32(kEvent + 2384) == 18 &&
        PPC_LOAD_U32(kEvent + 2396) == 6 &&
        ctx.r3.u32 == PPC_LOAD_U32(kParticipant + 18240))
    {
        // sub_822426D0 sets this same one-step native hold request. The
        // original integration consumes and clears it; other cars still run.
        PPC_STORE_U8(ctx.r3.u32 + 14654, 1);
        ++hold.heldSteps;
    }
    __imp__sub_8229A0F8(ctx, base);
}

extern "C" PPC_FUNC(__imp__sub_822817C0);
PPC_FUNC(sub_822817C0)
{
    const uint32_t self = ctx.r3.u32;
    const bool observe = Enabled(base) && PPC_LOAD_U32(game::rewind::kPhase) == 5 &&
        self == PPC_LOAD_U32(kParticipant + 18240);
    __imp__sub_822817C0(ctx, base);
    if (NetworkHolding(base) && self == PPC_LOAD_U32(kParticipant + 18240))
        ApplyMeterEngine(base, self, network.model);
    if (!observe) return;
    if (hold.active && hold.meter && HoldScope(base) && PPC_LOAD_U32(kEvent + 2384) == 18)
        ApplyMeterEngine(base, self);
    const float time = ReadFloat(base, kWorld + 36);
    if (ReplayingLaunch(base))
    {
        auto frame = std::upper_bound(recording.controls.begin(), recording.controls.end(), time,
            [](float t, const ControlFrame& value) { return t < value.time; });
        if (frame != recording.controls.begin())
            for (unsigned i = 0; i < 9; ++i) PPC_STORE_U32(self + 6536 + i * 4, std::prev(frame)->words[i]);
        if (replayHold.active) ApplyMeterEngine(base, self, replayHold.model);
    }
    else if (hold.meter && HoldScope(base) && PPC_LOAD_U32(kEvent + 2384) == 18 && recording.controls.size() < 8192)
    {
        // A native rewind after launch starts a new branch of the same
        // recording. Drop future controls before appending that branch.
        while (!recording.controls.empty() && recording.controls.back().time >= time)
            recording.controls.pop_back();
        ControlFrame frame{time, {}};
        for (unsigned i = 0; i < 9; ++i) frame.words[i] = PPC_LOAD_U32(self + 6536 + i * 4);
        recording.controls.push_back(frame);
    }
    static thread_local double next = 0;
    const double now = report::SessionSeconds();
    if (!Diagnostics() || now < next) return;
    next = now + .25;
    auto f = [base](uint32_t a) { return std::bit_cast<float>(PPC_LOAD_U32(a)); };
    Trace( "[crash-controls] wall=%.6f world=%.6f physical=%08X acceleration=%.7g steering=%.7g max_speed=%.7g gear_count=%u launch_flag=%u hold_request=%u gear0=%.7g boost_request=%u\n",
        now, f(kWorld + 36), self, f(self + 6544), f(self + 6552), f(self + 6484),
        PPC_LOAD_U32(self + 6736), PPC_LOAD_U8(self + 37328), PPC_LOAD_U8(self + 14654),
        f(self + 6620), PPC_LOAD_U8(kEvent + 2880));
    if (hold.active && hold.meter)
        Trace( "[crash-meter-engine] world=%.6f level=%.7g acceleration=%.7g max_rpm=%.7g angular_velocity=%.7g rear_wheels=%.7g:%.7g effects=%u:%u\n",
            f(kWorld + 36), hold.model.level, f(self + 6544), f(self + 6672), f(self + 6708),
            f(self + 1948), f(self + 2140), PPC_LOAD_U32(self + 1980), PPC_LOAD_U32(self + 2172));
}

extern "C" PPC_FUNC(__imp__sub_82128308);
PPC_FUNC(sub_82128308)
{
    if (ReplayingLaunch(base) && ctx.r3.u32 == kEvent)
    {
        if (replayHold.active)
            ApplyMeterEngine(base, PPC_LOAD_U32(kParticipant + 18240), replayHold.model);
        ctx.r3.u64 = 1;
        return;
    }
    if (hold.active && hold.meter && HoldScope(base) && ctx.r3.u32 == kEvent &&
        PPC_LOAD_U32(kEvent + 2384) == 18)
    {
        // Replace the countdown's time-based revs only while the meter owns
        // this solo car. Finalized launches retain the native downstream path.
        ApplyMeterEngine(base, PPC_LOAD_U32(kParticipant + 18240));
        ctx.r3.u64 = 1;
        return;
    }
    __imp__sub_82128308(ctx, base);
}

extern "C" PPC_FUNC(__imp__sub_822B9750);
PPC_FUNC(sub_822B9750)
{
    if (Diagnostics() && hold.active && hold.meter && HoldScope(base) &&
        ctx.r4.u32 == PPC_LOAD_U32(kParticipant + 18240))
    {
        static thread_local double next = 0;
        const double now = report::SessionSeconds();
        if (now >= next)
        {
            next = now + .25;
            const uint32_t physical = ctx.r4.u32;
            Trace( "[crash-meter-sound-input] world=%.6f level=%.7g acceleration=%.7g angular_velocity=%.7g expected=%.7g\n",
                ReadFloat(base, kWorld + 36), hold.model.level, ReadFloat(base, physical + 6544),
                ReadFloat(base, physical + 6708), hold.model.EngineAngularVelocity(ReadFloat(base, physical + 6672)));
        }
    }
    __imp__sub_822B9750(ctx, base);
}

extern "C" PPC_FUNC(__imp__sub_82128270);
extern "C" PPC_FUNC(__imp__sub_82111A08);
PPC_FUNC(sub_82111A08)
{
    if (hold.meter && HoldScope(base) && PPC_LOAD_U32(kEvent + 2384) == 18 && ctx.r3.u32 == kParticipant)
    {
        if (hold.active || hold.model.phase != game::crash_launch::Phase::Done) return;
        const float age = ReadFloat(base, kWorld + 36) - hold.resultAt;
        if (age < 0 || age >= 1.f) return;
        // ShowCountdown renders GO for the first second after this timestamp.
        // Give only that call a release-relative clock; do not reset the world,
        // traffic or the participant's persistent launch schedule.
        const uint32_t saved = PPC_LOAD_U32(kParticipant + 13524);
        WriteFloat(base, kParticipant + 13524, hold.resultAt);
        __imp__sub_82111A08(ctx, base);
        PPC_STORE_U32(kParticipant + 13524, saved);
        return;
    }
    __imp__sub_82111A08(ctx, base);
}

extern "C" PPC_FUNC(__imp__sub_8219C750);
extern "C" PPC_FUNC(__imp__sub_821A06F8);
PPC_FUNC(sub_821A06F8)
{
    if (Diagnostics() && MeterEnabled() && Enabled(base) && PPC_LOAD_U32(game::rewind::kPhase) == 5 &&
        PPC_LOAD_U32(kEvent + 2384) >= 16 && PPC_LOAD_U32(kEvent + 2384) <= 18)
        Trace( "[crash-intro-message] object=%08X world=%.6f state=%u id=%u caller=%08X held=%u\n",
            ctx.r3.u32, ReadFloat(base, kWorld + 36), PPC_LOAD_U32(kEvent + 2384), ctx.r4.u32, uint32_t(ctx.lr), hold.active);
    __imp__sub_821A06F8(ctx, base);
}

PPC_FUNC(sub_8219C750)
{
    if (Diagnostics() && MeterEnabled() && Enabled(base) && PPC_LOAD_U32(game::rewind::kPhase) == 5 &&
        PPC_LOAD_U32(kEvent + 2384) >= 16 && PPC_LOAD_U32(kEvent + 2384) <= 18)
        Trace( "[crash-hud-message] world=%.6f state=%u id=%u caller=%08X\n",
            ReadFloat(base, kWorld + 36), PPC_LOAD_U32(kEvent + 2384), ctx.r4.u32, uint32_t(ctx.lr));
    __imp__sub_8219C750(ctx, base);
}

PPC_FUNC(sub_82128270)
{
    const bool observe = ctx.r3.u32 == kEvent && Enabled(base);
    const uint32_t caller = uint32_t(ctx.lr);
    if (observe) Log(base, "deadline-pre", caller);
    __imp__sub_82128270(ctx, base);
    if (observe) Log(base, "deadline-post", caller);
}

extern "C" PPC_FUNC(__imp__sub_82363C80);
PPC_FUNC(sub_82363C80)
{
    const bool observe = ctx.r3.u32 == kWorld && Enabled(base);
    const uint32_t caller = uint32_t(ctx.lr);
    if (observe) Log(base, "clock-reset-pre", caller);
    __imp__sub_82363C80(ctx, base);
    if (observe) Log(base, "clock-reset-post", caller);
}

// The native initializer runs for a fresh Crash event and Retry, including
// network variants through their common base. Keep the current event stable
// when the player changes the setting while paused or during a launch.
extern "C" PPC_FUNC(__imp__sub_821394F8);
PPC_FUNC(sub_821394F8)
{
    const uint32_t event=ctx.r3.u32;
    if (event==kEvent || game::crash_launch::KnownNetworkEvent(event))
    {
        eventMeterChoice=RequestedMeter();
        hold={}; replayHold={}; recording={}; network={};
        game::crash_launch::rewindBlocked.store(false,std::memory_order_release);
        game::crash_launch::hudStatus.Publish({});
        fprintf(stderr,"[crash-meter] event initialized=%08X enabled=%u\n",event,*eventMeterChoice);
    }
    __imp__sub_821394F8(ctx,base);
}
