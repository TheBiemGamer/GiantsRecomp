#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "portal/portal_device.h"

namespace giantrecomp::portal {

// A Skylander's in-game progress, decoded from its save-data bytes.
struct FigureStats {
  uint8_t level;
  uint32_t gold;
  std::string nickname;
};

// Decodes level/gold/nickname from a figure's raw bytes. Returns nullopt for a figure with no
// save data yet (freshly created, or any other all-zero save-data region) or bytes that don't
// look like valid Giants save data -- never a guessed or partial result.
//
// The real byte layout is still being determined empirically (see docs/superpowers/specs/
// 2026-09-27-figure-stats-design.md); until that lands, every non-blank figure also falls
// through to nullopt below.
std::optional<FigureStats> ParseFigureStats(const FigureData& data);

}  // namespace giantrecomp::portal
