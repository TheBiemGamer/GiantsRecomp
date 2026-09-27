#pragma once

#include <filesystem>
#include <optional>

#include "portal/portal_device.h"

namespace giantrecomp::portal {

// Reads a raw figure dump: exactly kFigureSize (1024) bytes, 64 blocks of 16. Returns nullopt if the
// path is not a regular file or its size is anything else. The file is only read, never modified.
std::optional<FigureData> LoadFigureFile(const std::filesystem::path& path);

// Atomically writes `data` to `path`: writes to a temporary file next to it, then renames the
// temporary file over `path`. A crash or power loss mid-save leaves the original file untouched
// rather than a half-written one. Returns false, and leaves `path` unchanged, if either step fails
// (for example the parent directory does not exist); the temporary file is cleaned up either way.
bool SaveFigureFileAtomic(const std::filesystem::path& path, const FigureData& data);

}  // namespace giantrecomp::portal
