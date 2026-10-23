// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>
struct PPCContext;
namespace game::rewind::ring
{
    enum class State : uint8_t { Disabled, Unsupported, Collecting, Ready, Saving, Failed, Restoring };
    struct Status { State state{}; unsigned seconds{},readyMask{}; uint32_t epoch{}; };
    // Host threads exchange requests/status only; guest memory stays on the
    // observed world boundary. A request is tied to its published timeline.
    void SetEnabled(bool enabled);
    void SetWorldOptions(uint32_t options);
    Status GetStatus();
    bool Request(unsigned seconds);
    int TakeOutcome(); // positive: milliseconds rewound, -1: refused, 0: no new outcome
    // Developer audit: publish the camera observed at its native FOV update.
    void AuditCamera(uint32_t camera);
    bool Preparing(uint32_t recorder);
    void AtBoundary(PPCContext& ctx, uint8_t* base, uint32_t recorder);
    void AtWorldBoundary(PPCContext& ctx,uint8_t* base);
}
