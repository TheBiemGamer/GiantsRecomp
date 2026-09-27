#include "portal/figure_file.h"

#include <fstream>
#include <system_error>

namespace giantrecomp::portal {

std::optional<FigureData> LoadFigureFile(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) return std::nullopt;
  if (std::filesystem::file_size(path, ec) != kFigureSize || ec) return std::nullopt;

  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  FigureData data{};
  in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
  if (in.gcount() != static_cast<std::streamsize>(data.size())) return std::nullopt;
  return data;
}

}  // namespace giantrecomp::portal
