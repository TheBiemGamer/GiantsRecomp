#include "portal/figure_stats.h"

#include <algorithm>

namespace giantrecomp::portal {

std::optional<FigureStats> ParseFigureStats(const FigureData& data) {
  const bool all_zero = std::all_of(data.begin(), data.end(), [](uint8_t b) { return b == 0; });
  if (all_zero) return std::nullopt;

  // Real decode logic lands once the byte layout is confirmed against an actual play session
  // (see figure_stats.h's doc comment). Every figure currently falls through to here.
  return std::nullopt;
}

}  // namespace giantrecomp::portal
