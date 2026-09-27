#pragma once

#include <filesystem>
#include <optional>

#include "portal/portal_device.h"

namespace giantrecomp::portal {

// Reads a raw figure dump: exactly kFigureSize (1024) bytes, 64 blocks of 16. Returns nullopt if the
// path is not a regular file or its size is anything else. The file is only read, never modified.
std::optional<FigureData> LoadFigureFile(const std::filesystem::path& path);

}  // namespace giantrecomp::portal
