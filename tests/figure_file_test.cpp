#include <filesystem>
#include <fstream>
#include <string>

#include "portal/figure_file.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace giantrecomp::portal;

static fs::path WriteTemp(const std::wstring& name, size_t size, uint8_t seed) {
  fs::path p = fs::temp_directory_path() / name;
  std::ofstream out(p, std::ios::binary);
  for (size_t i = 0; i < size; ++i) out.put(static_cast<char>(seed + i * 3));
  return p;
}

int main() {
  // A file of exactly 1024 bytes loads unchanged.
  {
    fs::path p = WriteTemp(L"gr_figure_ok.dump", kFigureSize, 5);
    auto data = LoadFigureFile(p);
    CHECK(data.has_value());
    if (data) {
      CHECK((*data)[0] == 5);
      CHECK((*data)[1] == 8);
      CHECK((*data)[1023] == static_cast<uint8_t>(5 + 1023 * 3));
    }
  }

  // Anything that is not exactly one figure is rejected: too short, too long, empty.
  CHECK(!LoadFigureFile(WriteTemp(L"gr_figure_1023.dump", kFigureSize - 1, 1)).has_value());
  CHECK(!LoadFigureFile(WriteTemp(L"gr_figure_1025.dump", kFigureSize + 1, 1)).has_value());
  CHECK(!LoadFigureFile(WriteTemp(L"gr_figure_1088.dump", 1088, 1)).has_value());
  CHECK(!LoadFigureFile(WriteTemp(L"gr_figure_empty.dump", 0, 1)).has_value());

  // A missing file and a folder are rejected.
  CHECK(!LoadFigureFile(fs::temp_directory_path() / L"gr_figure_missing.dump").has_value());
  CHECK(!LoadFigureFile(fs::temp_directory_path()).has_value());
  CHECK(!LoadFigureFile(fs::path()).has_value());

  // Paths with spaces, parentheses and non-ANSI characters work.
  {
    fs::path dir = fs::temp_directory_path() / L"gr fig (2) 日本";
    fs::create_directories(dir);
    fs::path p = dir / L"Tree Rex.dump";
    {
      std::ofstream out(p, std::ios::binary);
      for (size_t i = 0; i < kFigureSize; ++i) out.put(static_cast<char>(i));
    }
    auto data = LoadFigureFile(p);
    CHECK(data.has_value());
    if (data) CHECK((*data)[255] == 255);
  }

  // SaveFigureFileAtomic creates a new file with exactly the given bytes.
  {
    fs::path p = fs::temp_directory_path() / L"gr_figure_save_new.dump";
    fs::remove(p);
    FigureData d{};
    for (size_t i = 0; i < d.size(); ++i) d[i] = static_cast<uint8_t>(i * 5 + 2);
    CHECK(SaveFigureFileAtomic(p, d));
    auto loaded = LoadFigureFile(p);
    CHECK(loaded.has_value());
    if (loaded) CHECK(*loaded == d);
    fs::remove(p);
  }

  // It overwrites existing content fully, and leaves no temp file behind.
  {
    fs::path p = WriteTemp(L"gr_figure_save_overwrite.dump", kFigureSize, 9);
    FigureData d{};
    for (size_t i = 0; i < d.size(); ++i) d[i] = static_cast<uint8_t>(200 - i);
    CHECK(SaveFigureFileAtomic(p, d));
    auto loaded = LoadFigureFile(p);
    CHECK(loaded.has_value());
    if (loaded) CHECK(*loaded == d);
    fs::path tmp = p;
    tmp += L".tmp";
    CHECK(!fs::exists(tmp));
  }

  // A location that cannot be written (parent directory missing) fails cleanly and creates nothing.
  {
    fs::path p = fs::temp_directory_path() / L"gr_missing_dir_xyz" / L"figure.dump";
    FigureData d{};
    CHECK(!SaveFigureFileAtomic(p, d));
    CHECK(!fs::exists(p));
  }

  // Paths with spaces, parentheses and non-ANSI characters work for saving too.
  {
    fs::path dir = fs::temp_directory_path() / L"gr save (2) 日本";
    fs::create_directories(dir);
    fs::path p = dir / L"Tree Rex.dump";
    FigureData d{};
    for (size_t i = 0; i < d.size(); ++i) d[i] = static_cast<uint8_t>(i);
    CHECK(SaveFigureFileAtomic(p, d));
    auto loaded = LoadFigureFile(p);
    CHECK(loaded.has_value());
    if (loaded) CHECK((*loaded)[255] == 255);
  }

  return Finish("figure_file");
}
