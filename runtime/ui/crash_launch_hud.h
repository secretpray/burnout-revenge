// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <vulkan/vulkan.h>
namespace game::crash_launch { struct Status; }
namespace ui
{
    void DrawCrashLaunch(const game::crash_launch::Status& status);
    void ReleaseCrashLaunchRenderer(VkDevice device, bool releaseArt);
}
