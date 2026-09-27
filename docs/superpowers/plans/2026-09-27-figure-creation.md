# Figure Creation & Picker v3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user create a new Skylander figure from just a name (no dump/portal needed) and browse/place figures from the overlay, everything grouped by game.

**Architecture:** A generated, compiled-in 648-entry `{id, variant, name, game}` catalog (extracted from the user's own real dumps) backs two pure, unit-tested primitives — `CreateBlankFigure` (builds the on-tag bytes) and `UniqueFigurePath` (picks a non-colliding file path) — in the existing SDK-independent `giantrecomp_portal` library. A thin, untested glue function in `portal_hook.cpp` composes them with the existing `SaveFigureFileAtomic`/`PlaceFigureFromFile`. The overlay gets a second "New Figure" tab that reuses the existing filter box and per-game `SeparatorText` grouping already used by the browse tab.

**Tech Stack:** C++23, no new third-party dependency (catalog is a compiled-in `constexpr` array, not parsed at runtime).

**Spec:** `docs/superpowers/specs/2026-09-27-figure-creation-design.md`

## Global Constraints

- C++23 (`CMakeLists.txt:8-9`, `:83`) — `std::span`, `std::string_view` are available.
- No new runtime data-parsing dependency (JSON/TOML): the catalog is a compiled-in `constexpr std::array`, per spec §2.
- Never copy Cemu's (or any reference emulator's) literal code or data tables — re-derive facts and re-implement, keeping this repo MIT-clean (original design spec §3, reaffirmed in this feature's spec §2-3). The CRC16 must be implemented as the generic bitwise CCITT algorithm, not a copied lookup table.
- Figure byte offsets are exact and non-negotiable (spec §3): id at `0x10-0x11` LE, variant at `0x1C-0x1D` LE, CRC16-CCITT (poly `0x1021`, init `0xFFFF`) over `0x00-0x1D` at `0x1E-0x1F` LE, sector trailer access bits at byte 6-9 of every `0x40`-aligned block starting at `0x36`.
- Test style: no test framework: `tests/test_util.h`'s `CHECK(cond)` macro + `Finish("name")`, one `int main()` per file, registered in `CMakeLists.txt`'s `foreach(portal_test ...)` list (`CMakeLists.txt:85`). This feature only *extends* two files already in that list (`figure_file_test.cpp`, `figure_catalog_test.cpp`) — no `CMakeLists.txt` edit is needed.
- `src/hooks/portal_hook.cpp` links against the ReXGlue SDK (`rex/cvar.h`, `rex/hook.h`) and is **not** part of the unit-testable `giantrecomp_portal` library (`CMakeLists.txt:35` vs `:75-81`) — consistent with the existing untested `PlaceFigureFromFile`/`RemoveFigureFromSlot`, new glue code there is verified by compiling + a manual run, not a `CHECK`-based test.
- ImGui overlay code (`src/overlay/`) has no unit tests anywhere in this repo; this plan doesn't add the first one — verify by building and a manual F6 run instead.

## Review Focus

- **Non-ASCII or punctuation-heavy names as folder/file components.** All 648 extracted names were checked and contain no `< > : " / \ | ? *`, but a future catalog regen might not be re-checked — `UniqueFigurePath`'s test includes a name with parentheses (already common: "Bash (Series 2)") to at least confirm that common case works end to end. (Task 2)
- **Filename collision beyond the first retry.** A user creating the same Skylander three times in a row must get `.dump`, ` (2).dump`, ` (3).dump`, not an infinite loop or an overwrite. (Task 2)
- **An unrecognized `(id, variant)` pair in an existing dump file** (a real figure outside the 648-entry table, or a corrupted file) must fall back to the filename, never crash or show a blank name. (Task 3)
- **Creating a figure when `portal_figures_dir` is unset or the folder can't be created** must fail cleanly (`false`, nothing written), matching how `PlaceFigureFromFile` already fails cleanly on a bad path. In normal operation `InstallConfiguredPortal` always defaults `portal_figures_dir` before the overlay can run, so this is a defensive guard rather than a reachable UI state; verified by code reading plus a manual check (Task 4, Step 3), not a `CHECK`-based test, consistent with the rest of `portal_hook.cpp`.
- **Round-trip**: a figure created by `CreateBlankFigure` must be recognized by `FindSkylander` when read back — i.e. the write-side offsets (Task 2) and read-side offsets (Task 3) must agree, not just each independently match the spec table. (Task 3, via a test that creates then looks up)

---

## Task 1: Skylander catalog data

**Files:**
- Create: `src/portal/skylander_catalog_data.h`

**Interfaces:**
- Consumes: nothing (no code dependency — only reads the user's own dump collection, once, offline).
- Produces: `inline constexpr std::array<SkylanderInfo, 648> kSkylanderCatalog` in `namespace giantrecomp::portal`, using the `SkylanderInfo` type Task 3 declares in `figure_catalog.h`. Sorted by `game` then `name` (case-insensitive). Task 3 depends on this array existing under this exact name.

This task's "test" is the extraction script's own printed counts (648 entries, 6 distinct games, zero cross-game key collisions) — there is no C++ to unit-test here, only generated data.

- [ ] **Step 1: Run the extraction script**

Save this as a throwaway script (e.g. `scratch/extract_catalog.py`, outside the repo's tracked source — delete it after, or keep it in `docs/` as documentation of how to regenerate; it is not part of the build) and run it with `python3`:

```python
import struct, re
from pathlib import Path
from collections import defaultdict

root = Path(r"C:\Users\Noah\Documents\giantsrecomp\Dumps Clean")
games = sorted([d for d in root.iterdir() if d.is_dir()])

def clean_game(g):
    g = re.sub(r'^\d+\.\s*', '', g)
    g = g.replace('_s ', "'s ")
    return g

by_key = defaultdict(list)
for g in games:
    for f in g.rglob("*.dump"):
        data = f.read_bytes()
        sky_id = struct.unpack_from("<H", data, 0x10)[0]
        variant = struct.unpack_from("<H", data, 0x1C)[0]
        by_key[(sky_id, variant)].append((f.stem, clean_game(g.name)))

table = []
for (sky_id, variant), entries in by_key.items():
    game = entries[0][1]
    name = min((n for n, _ in entries), key=len)
    table.append((sky_id, variant, name, game))

table.sort(key=lambda e: (e[3], e[2].lower()))

assert len(table) == 648, f"expected 648 entries, got {len(table)}"
assert len({e[3] for e in table}) == 6, "expected exactly 6 games"

lines = []
lines.append("#pragma once")
lines.append("")
lines.append("#include <array>")
lines.append("")
lines.append('#include "portal/figure_catalog.h"')
lines.append("")
lines.append("namespace giantrecomp::portal {")
lines.append("")
lines.append("// Generated 2026-09-27 from the user's own dump collection (Dumps Clean); see")
lines.append("// docs/superpowers/specs/2026-09-27-figure-creation-design.md \xa72 for how this was built.")
lines.append("// Sorted by game, then name, case-insensitively.")
lines.append(f"inline constexpr std::array<SkylanderInfo, {len(table)}> kSkylanderCatalog = {{{{")
for sky_id, variant, name, game in table:
    lines.append(f'    {{{sky_id}, {variant}, "{name}", "{game}"}},')
lines.append("}};")
lines.append("")
lines.append("}  // namespace giantrecomp::portal")
lines.append("")

Path("src/portal/skylander_catalog_data.h").write_text("\n".join(lines), encoding="utf-8")
print(f"wrote {len(table)} entries across {len({e[3] for e in table})} games")
```

Run: `python3 scratch/extract_catalog.py` from the repo root.
Expected output: `wrote 648 entries across 6 games`

- [ ] **Step 2: Sanity-check the generated header**

```bash
grep -c '",' src/portal/skylander_catalog_data.h   # expect 648
head -20 src/portal/skylander_catalog_data.h
```

Confirm the file starts with `#pragma once`, includes `"portal/figure_catalog.h"`, and its rows look like `{112, 0, "Tree Rex", "Giants"},`.

Note: this header will not compile until Task 3 declares `SkylanderInfo` in `figure_catalog.h` — that's expected; it has no build target of its own yet.

- [ ] **Step 3: Commit**

```bash
git add src/portal/skylander_catalog_data.h
git commit -m "data: add generated Skylander id/variant/name/game catalog"
```

---

## Task 2: Figure creation primitives

**Files:**
- Modify: `src/portal/figure_file.h`
- Modify: `src/portal/figure_file.cpp`
- Test: `tests/figure_file_test.cpp` (extend existing file)

**Interfaces:**
- Consumes: `portal::FigureData`, `portal::kFigureSize` (`portal/portal_device.h`, already used by this file).
- Produces (for Task 3 and Task 4):
  - `uint16_t ReadFigureId(const portal::FigureData& data)`
  - `uint16_t ReadFigureVariant(const portal::FigureData& data)`
  - `portal::FigureData CreateBlankFigure(uint16_t id, uint16_t variant)`
  - `portal::FigureData CreateBlankFigure(uint16_t id, uint16_t variant, std::array<uint8_t, 4> serial)`
  - `std::filesystem::path UniqueFigurePath(const std::filesystem::path& dir, std::string_view name)`

- [ ] **Step 1: Write the failing tests**

Append to `tests/figure_file_test.cpp`, just before the final `return Finish("figure_file");`:

```cpp
  // ReadFigureId/ReadFigureVariant read the little-endian id/variant bytes.
  {
    FigureData d{};
    d[0x10] = 0x70;
    d[0x11] = 0x00;  // id 0x0070 = 112 (Tree Rex)
    d[0x1C] = 0x02;
    d[0x1D] = 0x16;  // variant 0x1602
    CHECK(ReadFigureId(d) == 112);
    CHECK(ReadFigureVariant(d) == 0x1602);
  }

  // CreateBlankFigure (fixed-serial overload) produces the exact byte layout from the design spec.
  {
    FigureData d = CreateBlankFigure(112, 0, {0x11, 0x22, 0x33, 0x44});
    CHECK(d[0] == 0x11);
    CHECK(d[1] == 0x22);
    CHECK(d[2] == 0x33);
    CHECK(d[3] == 0x44);
    CHECK(d[4] == (0x11 ^ 0x22 ^ 0x33 ^ 0x44));  // BCC
    CHECK(d[5] == 0x81);
    CHECK(d[6] == 0x01);
    CHECK(d[7] == 0x0F);
    CHECK(ReadFigureId(d) == 112);
    CHECK(ReadFigureVariant(d) == 0);
    // CRC16-CCITT(init 0xFFFF, poly 0x1021) over bytes 0x00-0x1D, computed independently here.
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < 0x1E; ++i) {
      crc ^= static_cast<uint16_t>(d[i]) << 8;
      for (int bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                              : static_cast<uint16_t>(crc << 1);
      }
    }
    CHECK(d[0x1E] == (crc & 0xFF));
    CHECK(d[0x1F] == (crc >> 8));
    // Sector 0 trailer access bits.
    CHECK(d[0x36] == 0x69);
    CHECK(d[0x37] == 0x0F);
    CHECK(d[0x38] == 0x0F);
    CHECK(d[0x39] == 0x0F);
    // Sector 1 trailer access bits (differ from sector 0).
    CHECK(d[0x76] == 0x69);
    CHECK(d[0x77] == 0x08);
    CHECK(d[0x78] == 0x0F);
    CHECK(d[0x79] == 0x7F);
    // Sector 15 (last) trailer access bits, same pattern as sector 1.
    CHECK(d[0x3F6] == 0x69);
    CHECK(d[0x3F7] == 0x08);
    CHECK(d[0x3F8] == 0x0F);
    CHECK(d[0x3F9] == 0x7F);
    // Untouched byte stays zero.
    CHECK(d[0x20] == 0);
  }

  // The random-serial overload produces a loadable, self-consistent figure (BCC/CRC correct);
  // two calls give different serials (overwhelmingly likely with 4 random bytes).
  {
    FigureData a = CreateBlankFigure(4, 0);
    FigureData b = CreateBlankFigure(4, 0);
    CHECK(a[4] == (a[0] ^ a[1] ^ a[2] ^ a[3]));
    bool any_serial_byte_differs = a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || a[3] != b[3];
    CHECK(any_serial_byte_differs);
  }

  // UniqueFigurePath: a free name comes back unchanged.
  {
    fs::path dir = fs::temp_directory_path() / L"gr_unique_path";
    fs::remove_all(dir);
    fs::create_directories(dir);
    CHECK(UniqueFigurePath(dir, "Tree Rex") == dir / L"Tree Rex.dump");

    // An existing file forces " (2)", then " (3)" on the next collision.
    { std::ofstream(dir / L"Tree Rex.dump", std::ios::binary) << "x"; }
    CHECK(UniqueFigurePath(dir, "Tree Rex") == dir / L"Tree Rex (2).dump");
    { std::ofstream(dir / L"Tree Rex (2).dump", std::ios::binary) << "x"; }
    CHECK(UniqueFigurePath(dir, "Tree Rex") == dir / L"Tree Rex (3).dump");

    // A name that already contains parentheses (a real catalog entry, e.g. "Bash (Series 2)").
    CHECK(UniqueFigurePath(dir, "Bash (Series 2)") == dir / L"Bash (Series 2).dump");

    // A directory that does not exist yet: the first name is always free, nothing is created.
    fs::path missing_dir = fs::temp_directory_path() / L"gr_unique_path_missing";
    fs::remove_all(missing_dir);
    CHECK(UniqueFigurePath(missing_dir, "Spyro") == missing_dir / L"Spyro.dump");
    CHECK(!fs::exists(missing_dir));

    fs::remove_all(dir);
  }
```

- [ ] **Step 2: Run tests to verify they fail**

Run (from the build directory, e.g. `out/build/win-amd64-release` — see `logs/vc.cmd` if not already in the VS x64 environment): `cmake --build . --target figure_file_test` then `ctest -R figure_file_test -V`.
Expected: build FAILS — `ReadFigureId`, `CreateBlankFigure`, `UniqueFigurePath` are not declared yet.

- [ ] **Step 3: Implement**

In `src/portal/figure_file.h`, add after the existing declarations (before the closing `}  // namespace giantrecomp::portal`):

```cpp
#include <array>
#include <cstdint>
#include <string_view>

// ... (existing includes stay; add the three above if not already present)

// Reads the Skylander id / variant a figure's bytes encode: little-endian uint16 at a fixed
// offset (0x10 for id, 0x1C for variant). Never fails -- any 1024-byte figure has these bytes.
uint16_t ReadFigureId(const portal::FigureData& data);
uint16_t ReadFigureVariant(const portal::FigureData& data);

// Builds a blank, valid figure for `id`/`variant`: correct manufacturer bytes, id, variant, CRC,
// and Mifare sector-trailer access bits (see
// docs/superpowers/specs/2026-09-27-figure-creation-design.md \xa73). The 4-byte serial is normally
// random; the explicit-serial overload exists so callers (tests) can get a deterministic result.
FigureData CreateBlankFigure(uint16_t id, uint16_t variant);
FigureData CreateBlankFigure(uint16_t id, uint16_t variant, std::array<uint8_t, 4> serial);

// Returns a path under `dir` for `name` that does not currently exist: "<name>.dump", or
// "<name> (2).dump", "<name> (3).dump", ... on collision. Never creates `dir`, the returned path,
// or anything else -- it only picks a name.
std::filesystem::path UniqueFigurePath(const std::filesystem::path& dir, std::string_view name);
```

(Note: `FigureData` here refers to `portal::FigureData` since these declarations live inside `namespace giantrecomp::portal` already, matching `LoadFigureFile`'s existing style in this header.)

In `src/portal/figure_file.cpp`, add `#include <random>` and `#include <string>` near the top, and add before the closing namespace brace:

```cpp
namespace {

uint16_t Crc16Ccitt(const uint8_t* data, size_t size, uint16_t crc) {
  for (size_t i = 0; i < size; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                            : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

void WriteU16LE(FigureData& data, size_t offset, uint16_t value) {
  data[offset] = static_cast<uint8_t>(value & 0xFF);
  data[offset + 1] = static_cast<uint8_t>(value >> 8);
}

}  // namespace

uint16_t ReadFigureId(const FigureData& data) {
  return static_cast<uint16_t>(data[0x10] | (static_cast<uint16_t>(data[0x11]) << 8));
}

uint16_t ReadFigureVariant(const FigureData& data) {
  return static_cast<uint16_t>(data[0x1C] | (static_cast<uint16_t>(data[0x1D]) << 8));
}

FigureData CreateBlankFigure(uint16_t id, uint16_t variant, std::array<uint8_t, 4> serial) {
  FigureData data{};
  data[0] = serial[0];
  data[1] = serial[1];
  data[2] = serial[2];
  data[3] = serial[3];
  data[4] = static_cast<uint8_t>(serial[0] ^ serial[1] ^ serial[2] ^ serial[3]);
  data[5] = 0x81;
  data[6] = 0x01;
  data[7] = 0x0F;
  WriteU16LE(data, 0x10, id);
  WriteU16LE(data, 0x1C, variant);
  const uint16_t crc = Crc16Ccitt(data.data(), 0x1E, 0xFFFF);
  WriteU16LE(data, 0x1E, crc);
  for (int sector = 0; sector < 16; ++sector) {
    const size_t offset = static_cast<size_t>(sector) * 0x40 + 0x36;
    data[offset] = 0x69;
    if (sector == 0) {
      data[offset + 1] = 0x0F;
      data[offset + 2] = 0x0F;
      data[offset + 3] = 0x0F;
    } else {
      data[offset + 1] = 0x08;
      data[offset + 2] = 0x0F;
      data[offset + 3] = 0x7F;
    }
  }
  return data;
}

FigureData CreateBlankFigure(uint16_t id, uint16_t variant) {
  std::random_device rd;
  std::array<uint8_t, 4> serial{};
  for (auto& b : serial) b = static_cast<uint8_t>(rd());
  return CreateBlankFigure(id, variant, serial);
}

std::filesystem::path UniqueFigurePath(const std::filesystem::path& dir, std::string_view name) {
  const std::u8string u8name(reinterpret_cast<const char8_t*>(name.data()), name.size());
  const std::filesystem::path base(u8name);
  std::filesystem::path candidate = dir / (base.native() + std::filesystem::path(L".dump").native());
  for (int n = 2; std::filesystem::exists(candidate); ++n) {
    candidate = dir / (base.native() + L" (" + std::to_wstring(n) + L").dump");
  }
  return candidate;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build . --target figure_file_test && ctest -R figure_file_test -V`
Expected: `all figure_file tests passed`

- [ ] **Step 5: Commit**

```bash
git add src/portal/figure_file.h src/portal/figure_file.cpp tests/figure_file_test.cpp
git commit -m "feat: add CreateBlankFigure and UniqueFigurePath"
```

---

## Task 3: Catalog lookup and display names

**Files:**
- Modify: `src/portal/figure_catalog.h`
- Modify: `src/portal/figure_catalog.cpp`
- Test: `tests/figure_catalog_test.cpp` (extend existing file)

**Interfaces:**
- Consumes: `kSkylanderCatalog` (Task 1, `skylander_catalog_data.h`), `ReadFigureId`/`ReadFigureVariant`/`LoadFigureFile` (Task 2 / existing, `figure_file.h`).
- Produces (for Task 5):
  - `struct SkylanderInfo { uint16_t id; uint16_t variant; std::string_view name; std::string_view game; };`
  - `std::span<const SkylanderInfo> AllSkylanders();`
  - `const SkylanderInfo* FindSkylander(uint16_t id, uint16_t variant);`
  - `FigureCatalogEntry` gains a `std::string display_name;` field (after `name`, before `game`).

- [ ] **Step 1: Write the failing tests**

Append to `tests/figure_catalog_test.cpp`, before `fs::remove_all(root); return Finish("figure_catalog");`:

```cpp
  // AllSkylanders/FindSkylander: the built-in catalog is non-empty, sorted by game then name, and
  // a known figure resolves; an unrecognized id/variant does not.
  {
    auto all = AllSkylanders();
    CHECK(!all.empty());
    const SkylanderInfo* tree_rex = FindSkylander(112, 0);
    CHECK(tree_rex != nullptr);
    if (tree_rex) {
      CHECK(tree_rex->name == "Tree Rex");
      CHECK(tree_rex->game == "Giants");
    }
    CHECK(FindSkylander(0xFFFF, 0xFFFF) == nullptr);
  }

  // A real .dump file (correct id/variant bytes) resolves display_name via the catalog, even
  // though its filename on disk is something else entirely.
  {
    fs::path dir = fs::temp_directory_path() / L"gr_catalog_display_name";
    fs::remove_all(dir);
    fs::create_directories(dir / L"2. Giants");
    FigureData d{};
    d[0x10] = 112 & 0xFF;
    d[0x11] = 112 >> 8;  // Tree Rex
    {
      std::ofstream out(dir / L"2. Giants" / L"my_weird_filename.dump", std::ios::binary);
      out.write(reinterpret_cast<const char*>(d.data()), d.size());
    }
    auto entries = ScanFigureCatalog(dir);
    CHECK(entries.size() == 1);
    if (!entries.empty()) {
      CHECK(entries[0].name == "my_weird_filename");    // filename is unchanged
      CHECK(entries[0].display_name == "Tree Rex");      // but the resolved name is correct
    }
    fs::remove_all(dir);
  }

  // A file with unrecognized id/variant bytes falls back to the filename for display_name.
  {
    fs::path dir = fs::temp_directory_path() / L"gr_catalog_unknown";
    fs::remove_all(dir);
    fs::create_directories(dir);
    FigureData d{};
    d[0x10] = 0xFF;
    d[0x11] = 0xFF;
    d[0x1C] = 0xFF;
    d[0x1D] = 0xFF;
    {
      std::ofstream out(dir / L"Mystery.dump", std::ios::binary);
      out.write(reinterpret_cast<const char*>(d.data()), d.size());
    }
    auto entries = ScanFigureCatalog(dir);
    CHECK(entries.size() == 1);
    if (!entries.empty()) CHECK(entries[0].display_name == "Mystery");
    fs::remove_all(dir);
  }

  // Round-trip: a figure built by CreateBlankFigure is recognized by FindSkylander when read back.
  {
    FigureData created = CreateBlankFigure(110, 0);  // Bouncer
    const SkylanderInfo* found = FindSkylander(ReadFigureId(created), ReadFigureVariant(created));
    CHECK(found != nullptr);
    if (found) CHECK(found->name == "Bouncer");
  }
```

Add `#include "portal/figure_file.h"` and `#include <fstream>` (if not already present) to the top of `tests/figure_catalog_test.cpp`.

Existing entries in this file created via the file's own `Touch()` helper write a single `'x'` byte, which is not a valid 1024-byte figure, so `LoadFigureFile` will fail for them and `display_name` will fall back to the filename stem — the existing assertions on `entries[i].name` and `.game` are unaffected by this task.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build . --target figure_catalog_test && ctest -R figure_catalog_test -V`
Expected: build FAILS — `SkylanderInfo`, `AllSkylanders`, `FindSkylander`, `display_name` don't exist yet.

- [ ] **Step 3: Implement**

In `src/portal/figure_catalog.h`, add near the top (after the includes, before `FigureCatalogEntry`):

```cpp
#include <cstdint>
#include <span>
#include <string_view>

struct SkylanderInfo {
  uint16_t id;
  uint16_t variant;
  std::string_view name;
  std::string_view game;
};

// The full built-in catalog, sorted by game then name -- the order any UI walking it should use.
std::span<const SkylanderInfo> AllSkylanders();

// Looks up a figure's real name/game from its id/variant. Returns nullptr if the pair isn't in
// the built-in catalog (e.g. a homebrew or unrecognized figure).
const SkylanderInfo* FindSkylander(uint16_t id, uint16_t variant);
```

Modify `FigureCatalogEntry` to add the new field:

```cpp
struct FigureCatalogEntry {
  std::string name;          // filename, without extension
  std::string display_name;  // resolved Skylander name if recognized, else same as `name`
  std::string game;
  std::filesystem::path path;
};
```

In `src/portal/figure_catalog.cpp`, add `#include "portal/figure_file.h"` and `#include "portal/skylander_catalog_data.h"` at the top, then add before the closing namespace brace:

```cpp
std::span<const SkylanderInfo> AllSkylanders() { return kSkylanderCatalog; }

const SkylanderInfo* FindSkylander(uint16_t id, uint16_t variant) {
  for (const auto& sky : kSkylanderCatalog) {
    if (sky.id == id && sky.variant == variant) return &sky;
  }
  return nullptr;
}
```

Add a helper in the existing anonymous namespace (alongside `Lower`/`TopLevelFolder`):

```cpp
std::string ResolveDisplayName(const std::filesystem::path& path, const std::string& fallback) {
  auto data = LoadFigureFile(path);
  if (!data) return fallback;
  if (const SkylanderInfo* sky = FindSkylander(ReadFigureId(*data), ReadFigureVariant(*data))) {
    return std::string(sky->name);
  }
  return fallback;
}
```

Update the `entries.push_back(...)` call in `ScanFigureCatalog`:

```cpp
    const std::string name = path.stem().string();
    entries.push_back({name, ResolveDisplayName(path, name), TopLevelFolder(root, path), path});
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build . --target figure_catalog_test && ctest -R figure_catalog_test -V`
Expected: `all figure_catalog tests passed`

- [ ] **Step 5: Commit**

```bash
git add src/portal/figure_catalog.h src/portal/figure_catalog.cpp tests/figure_catalog_test.cpp
git commit -m "feat: resolve figure display names from the built-in Skylander catalog"
```

---

## Task 4: Creation glue in the portal hook

**Files:**
- Modify: `src/hooks/portal_hook.h`
- Modify: `src/hooks/portal_hook.cpp`

**Interfaces:**
- Consumes: `portal::CreateBlankFigure`, `portal::UniqueFigurePath` (Task 2), `portal::SkylanderInfo` (Task 3), existing `PlaceFigureFromFile`, `portal::SaveFigureFileAtomic`, `g_software_portal`, `REXCVAR_GET(portal_figures_dir)`.
- Produces (for Task 5): `bool CreateAndPlaceFigure(int slot, const portal::SkylanderInfo& sky);`

No automated test for this task (see Global Constraints — `portal_hook.cpp` is SDK-linked, outside `giantrecomp_portal`); verified by a full build and a manual run in Step 3.

- [ ] **Step 1: Declare the function**

In `src/hooks/portal_hook.h`, add after `RemoveFigureFromSlot`'s declaration:

```cpp
#include "portal/figure_catalog.h"  // for portal::SkylanderInfo

// Creates a new blank figure for `sky` under portal_figures_dir/<game>/<name>.dump (creating the
// game subfolder if needed; an existing file of that name is never overwritten -- a " (2)", " (3)"
// suffix is added instead), then places it into `slot` exactly like PlaceFigureFromFile. Returns
// false, and creates nothing, if there is no active software portal, portal_figures_dir is unset,
// or the folder can't be created.
bool CreateAndPlaceFigure(int slot, const portal::SkylanderInfo& sky);
```

- [ ] **Step 2: Implement**

In `src/hooks/portal_hook.cpp`, add after `RemoveFigureFromSlot`'s definition:

```cpp
bool CreateAndPlaceFigure(int slot, const portal::SkylanderInfo& sky) {
  portal::SoftwarePortal* software = g_software_portal.load();
  if (!software) return false;
  const std::string dir_utf8 = REXCVAR_GET(portal_figures_dir);
  if (dir_utf8.empty()) return false;

  const std::filesystem::path game_dir = Utf8ToPath(dir_utf8) / std::string(sky.game);
  std::error_code ec;
  std::filesystem::create_directories(game_dir, ec);
  if (ec) return false;

  const std::filesystem::path path = portal::UniqueFigurePath(game_dir, sky.name);
  const portal::FigureData data = portal::CreateBlankFigure(sky.id, sky.variant);
  if (!portal::SaveFigureFileAtomic(path, data)) return false;
  return PlaceFigureFromFile(slot, path);
}
```

- [ ] **Step 3: Build and manually verify**

Run a full build (see `logs/vc.cmd` for the x64 VS environment if needed):

```bash
cmake --build out/build/win-amd64-release --target giantrecomp
```

Expected: builds with no new warnings from this file.

This step only confirms it compiles and links; the actual creation behavior (does it place a real, game-accepted figure) is verified together with the UI in Task 5's manual run, since there is no UI to trigger it yet.

Also confirm by reading the code just written: `CreateAndPlaceFigure` returns `false` immediately, before touching the filesystem, if `g_software_portal` is null (no active software portal) or `REXCVAR_GET(portal_figures_dir)` is empty -- both checked before `create_directories` is ever called. `InstallConfiguredPortal` (`portal_hook.cpp:47-53`, unchanged by this task) already guarantees `portal_figures_dir` is non-empty by the time the overlay can call this, so the empty-dir branch is a defensive guard, not a reachable UI state; there is nothing further to exercise here manually.

- [ ] **Step 4: Commit**

```bash
git add src/hooks/portal_hook.h src/hooks/portal_hook.cpp
git commit -m "feat: add CreateAndPlaceFigure to the portal hook"
```

---

## Task 5: Overlay "New Figure" tab

**Files:**
- Modify: `src/overlay/portal_overlay_dialog.h`
- Modify: `src/overlay/portal_overlay_dialog.cpp`

**Interfaces:**
- Consumes: `portal::AllSkylanders()`, `portal::SkylanderInfo` (Task 3), `CreateAndPlaceFigure` (Task 4), `FigureCatalogEntry::display_name` (Task 3), existing `SoftwarePortal::Figure`/`Source`, `RemoveFigureFromSlot`, `PlaceFigureFromFile`.
- Produces: nothing consumed elsewhere — this is the top of the call graph for this feature.

No automated test (ImGui, consistent with every other overlay file in this repo); verified by a manual F6 run in Step 3.

- [ ] **Step 1: Add the tab state and switch `SlotLabel`/browse list to `display_name`**

In `src/overlay/portal_overlay_dialog.h`, add a field:

```cpp
  bool creating_ = false;  // false: Browse tab: true: New Figure tab
```

In `src/overlay/portal_overlay_dialog.cpp`, update `SlotLabel` to prefer the resolved name over the raw filename stem. Since `SoftwarePortal::Source()` only gives a path (not the resolved name), and re-reading the figure's own bytes here is not necessary, keep `SlotLabel` as-is — the picker's *list* is where the fix belongs (`entry.name` -> `entry.display_name`), so nothing changes in `SlotLabel`, only in the browse-list render loop:

```cpp
    ImGui::TextUnformatted(entry.display_name.c_str());
```

(this replaces the existing `ImGui::TextUnformatted(entry.name.c_str());` at what was `portal_overlay_dialog.cpp:127`)

- [ ] **Step 2: Add the New Figure tab**

Add near the top of `OnDraw`, right after the `if (!software) { ... }` early-return block (i.e. once a software portal is confirmed active), a tab switcher:

```cpp
  if (ImGui::Button(creating_ ? "Browse" : "New Figure")) creating_ = !creating_;
  ImGui::SameLine();
```

Move the existing slot selector + Remove button (`ImGui::BeginCombo("Slot", ...)` through the `ImGui::EndDisabled();` after the Remove button) so it renders in both tabs -- it already does, since it's unconditional; no change needed there beyond the button/`SameLine()` added just before it.

After the existing `ImGui::Separator();` that follows the slot selector, branch:

```cpp
  if (creating_) {
    ImGui::InputTextWithHint("Filter", "Skylander name", filter_, sizeof(filter_));
    const std::string filter = Lower(filter_);
    ImGui::BeginChild("create_list", ImVec2(0, 0), true);
    std::string last_game;
    for (const auto& sky : portal::AllSkylanders()) {
      if (!filter.empty() && Lower(std::string(sky.name)).find(filter) == std::string::npos) continue;
      if (sky.game != last_game) {
        ImGui::SeparatorText(sky.game.data());
        last_game = std::string(sky.game);
      }
      ImGui::PushID(static_cast<int>(sky.id) * 100000 + sky.variant);
      ImGui::TextUnformatted(std::string(sky.name).c_str());
      ImGui::SameLine(ImGui::GetWindowWidth() - 80);
      if (ImGui::Button("Create")) {
        if (!CreateAndPlaceFigure(selected_slot_, sky)) {
          REXLOG_WARN("Portal overlay: could not create '{}'", sky.name);
        } else {
          Rescan();
          creating_ = false;
        }
      }
      ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
    return;
  }
```

placed so it returns before the existing browse-tab code (the `current_dir`/`figures_dir_at_last_scan_` checks and the existing list loop), leaving that code as the `creating_ == false` path unchanged except for the `display_name` swap from Step 1.

`portal::SkylanderInfo::game` is a `std::string_view` into a string literal in the generated header, so `sky.game.data()` is safe to pass to `ImGui::SeparatorText` (null-terminated, since it points into a `"..."` literal) without a copy; `last_game` is still a `std::string` so the not-equal comparison and reassignment across iterations are well-defined regardless of the view's lifetime.

Add `#include "portal/figure_catalog.h"` to the top of `portal_overlay_dialog.cpp` if not already pulled in transitively (it is, via `hooks/portal_hook.h`, but include it directly since this file now uses `portal::AllSkylanders`/`portal::SkylanderInfo` by name).

- [ ] **Step 3: Build and manually verify with the user**

Run a full build, then launch the game with the software portal (see `logs/m4-run.ps1` and the existing launcher `.cmd` files). Per the established testing routine for this project: launch the game, tell the user to press A at the title screen and play into a level, then press F6, switch to "New Figure", type part of a name (e.g. "tree"), confirm it's grouped under "Giants", click Create, confirm the figure appears placed in the chosen slot and the game reacts to it (e.g. shows it as a detected Skylander). Watch for their messages; capture only the game window if a screenshot is needed.

Expected: the created figure behaves like a real dump — the game accepts it as a valid Skylander with no reported errors.

- [ ] **Step 4: Commit**

```bash
git add src/overlay/portal_overlay_dialog.h src/overlay/portal_overlay_dialog.cpp
git commit -m "feat: add New Figure creation tab to the F6 overlay"
```
