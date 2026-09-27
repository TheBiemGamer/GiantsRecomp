#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace giantrecomp::portal {

struct FigureCatalogEntry {
  std::string name;
  std::string game;
  std::filesystem::path path;
};

// Recursively finds every *.dump file (case-insensitive extension) under `root`. A missing root,
// a root that is not a directory, or a root with nothing found all give an empty result — never an
// error. `game` is the top-level folder directly under `root` the file was found in ("" if the
// file sits directly under `root`). Entries are sorted by game, then by name, case-insensitively;
// entries with no game sort first.
std::vector<FigureCatalogEntry> ScanFigureCatalog(const std::filesystem::path& root);

}  // namespace giantrecomp::portal
