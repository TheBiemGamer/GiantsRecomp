#include "portal/figure_catalog.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace giantrecomp::portal {

namespace {

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string TopLevelFolder(const std::filesystem::path& root, const std::filesystem::path& file) {
  std::error_code ec;
  auto rel = std::filesystem::relative(file, root, ec);
  if (ec || rel.empty()) return {};
  auto first = rel.begin();
  if (first == rel.end()) return {};
  // If the first component is the filename itself, the file sits directly under root.
  auto next = first;
  ++next;
  if (next == rel.end()) return {};
  return first->string();
}

}  // namespace

std::vector<FigureCatalogEntry> ScanFigureCatalog(const std::filesystem::path& root) {
  std::vector<FigureCatalogEntry> entries;
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec) || ec) return entries;

  std::filesystem::recursive_directory_iterator it(
      root, std::filesystem::directory_options::skip_permission_denied, ec);
  std::filesystem::recursive_directory_iterator end;
  for (; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || ec) continue;
    const auto& path = it->path();
    if (Lower(path.extension().string()) != ".dump") continue;
    entries.push_back({path.stem().string(), TopLevelFolder(root, path), path});
  }

  std::sort(entries.begin(), entries.end(), [](const FigureCatalogEntry& a, const FigureCatalogEntry& b) {
    const auto ag = Lower(a.game), bg = Lower(b.game);
    if (ag != bg) return ag < bg;
    return Lower(a.name) < Lower(b.name);
  });
  return entries;
}

}  // namespace giantrecomp::portal
