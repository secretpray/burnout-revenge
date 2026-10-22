// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include "localization.h"
#include <filesystem>
#include <optional>
#include <install/result.h>

namespace locale
{
    // Imports only verified language resources from the player's own disc.
    // The USA executable and installed game are never replaced.
    install::Result ImportResources(const std::filesystem::path& source, const std::filesystem::path& data);
    // Before guest threads start. The selected pack is immutable for this run.
    bool PrepareGame(Language requested, const std::filesystem::path& game, const std::filesystem::path& data);
    // Only allowlisted disc-relative resources; no arbitrary path redirection.
    std::optional<std::filesystem::path> Resource(std::string_view relative);
}
