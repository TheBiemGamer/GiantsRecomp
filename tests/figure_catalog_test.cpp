#include <filesystem>
#include <fstream>

#include "portal/figure_catalog.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace giantrecomp::portal;

static void Touch(const fs::path& p) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << "x";
}

int main() {
  fs::path root = fs::temp_directory_path() / L"gr_catalog_root";
  fs::remove_all(root);

  // A missing root gives an empty catalog, not an error.
  CHECK(ScanFigureCatalog(root).empty());

  // A root that is a plain file (not a directory) also gives an empty catalog.
  fs::create_directories(root.parent_path());
  {
    std::ofstream(root, std::ios::binary) << "not a directory";
  }
  CHECK(ScanFigureCatalog(root).empty());
  fs::remove(root);

  // An empty, existing directory gives an empty catalog.
  fs::create_directories(root);
  CHECK(ScanFigureCatalog(root).empty());

  // Files with other extensions are ignored; only *.dump counts.
  Touch(root / L"1. Spyro's Adventure" / L"Spyro.dump");
  Touch(root / L"1. Spyro's Adventure" / L"notes.txt");
  Touch(root / L"1. Spyro's Adventure" / L"readme.dump.bak");
  Touch(root / L"2. Giants" / L"1) Giants" / L"Tree Rex.dump");
  Touch(root / L"2. Giants" / L"1) Giants" / L"Bouncer.dump");
  Touch(root / L"2. Giants" / L"2) New (Series 1)" / L"Chill.dump");
  Touch(root / L"loose.dump");  // directly under root, no game subfolder

  auto entries = ScanFigureCatalog(root);
  CHECK(entries.size() == 5);

  // Sorted by game then name (case-insensitive); entries with no game (game == "") sort first.
  CHECK(entries[0].game.empty());
  CHECK(entries[0].name == "loose");
  CHECK(entries[1].game == "1. Spyro's Adventure");
  CHECK(entries[1].name == "Spyro");
  CHECK(entries[2].game == "2. Giants");
  CHECK(entries[2].name == "Bouncer");
  CHECK(entries[3].game == "2. Giants");
  CHECK(entries[3].name == "Chill");
  CHECK(entries[4].game == "2. Giants");
  CHECK(entries[4].name == "Tree Rex");

  // The game field is the *top-level* folder under root, even for a .dump nested deeper.
  CHECK(entries[3].game == "2. Giants");  // Chill is two levels deep, under "2) New (Series 1)"

  // A path can be opened and matches what was created.
  CHECK(fs::equivalent(entries[4].path, root / L"2. Giants" / L"1) Giants" / L"Tree Rex.dump"));

  // Extension matching is case-insensitive (".DUMP" counts too).
  Touch(root / L"3. Swap Force" / L"Wash Buckler.DUMP");
  auto entries2 = ScanFigureCatalog(root);
  CHECK(entries2.size() == 6);

  fs::remove_all(root);
  return Finish("figure_catalog");
}
