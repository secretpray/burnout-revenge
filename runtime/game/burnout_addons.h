// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace game
{
    struct BurnoutAddon { std::string id, name; };
    void InitializeBurnoutAddons();  // before guest threads: import, extract, register
    const std::vector<BurnoutAddon>& BurnoutAddons(); // immutable after initialization
    std::filesystem::path BurnoutAddonFolder();
    std::string ImportBurnoutAddons(const std::filesystem::path& source); // next launch, no registry mutation
}
