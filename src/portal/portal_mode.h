#pragma once

#include <optional>
#include <string_view>

namespace giantrecomp::portal {

enum class PortalMode { kNone, kSoftware };

// Accepts "none" or "software" (any case, surrounding whitespace ignored). Anything else, including
// modes that do not exist yet, gives nullopt.
std::optional<PortalMode> ParsePortalMode(std::string_view text);

}  // namespace giantrecomp::portal
