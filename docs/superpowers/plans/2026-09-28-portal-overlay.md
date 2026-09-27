# Portal Overlay (Milestone 6) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An in-game figure picker, opened with a hotkey, that browses the user's `.dump` files and places or removes a figure on the software portal without restarting the game or typing a command line.

**Architecture:** A pure, testable `FigureCatalog` scans a directory tree for `.dump` files (no ReXGlue or ImGui dependency). `portal_hook.cpp` gains a small slot-to-source-file registry so any slot's figure — whether loaded at startup via `--portal_figure` or placed later from the overlay — saves its writes back to the right file, plus an accessor the overlay uses to reach the active `SoftwarePortal`. `PortalOverlayDialog` is a `rex::ui::ImGuiDialog` (the SDK's existing overlay pattern, same as its F3/F4/F7 dialogs) that lists the catalog, filters by name, and calls the existing `PlaceFigure`/`RemoveFigure` control API.

**Tech Stack:** C++23, clang, CMake/Ninja, ReXGlue `rex::ui::ImGuiDialog` / `RegisterBind` / `REX_HOOK_RAW`, Dear ImGui, plain-`main` tests via CTest.

**Spec:** `docs/superpowers/specs/2026-09-27-giantrecomp-design.md` (section 4.5, "Overlay"). Protocol/runtime facts: `docs/investigation/portal-protocol.md`.

## Global Constraints

- Windows only for v1; clang 19+, C++23.
- `src/portal/` (including the new catalog) stays free of any ReXGlue dependency and is unit-tested; `src/hooks/` and `src/overlay/` are the only places that depend on the game or the SDK's UI.
- No game assets, figure dumps, or generated code are committed. The catalog is built at runtime from the user's own folder and never written to the repo.
- Every figure a slot holds — however it got there — saves its writes back to the exact file it was loaded from (the milestone-5 behavior), including figures placed from the overlay.
- The overlay only calls `SoftwarePortal`'s existing control API (`PlaceFigure`, `RemoveFigure`, `HasFigure`, `Figure`); it never touches protocol bytes.

## Review Focus

1. `portal_figures_dir` unset, pointing at a missing folder, or pointing at a file: the overlay says so plainly and does not crash or show a stale list. (Task 3)
2. `portal_mode none` (or an unrecognized mode): the overlay explains no software portal is active, instead of a blank list or a crash on a null pointer. (Task 3)
3. A `.dump` file that fails to load (wrong size, deleted between scan and click): placing it fails cleanly, with a message, and does not change what was already on the portal. (Task 3)
4. Placing a second figure on a slot that already holds one (swap without an explicit remove) leaves the portal in a correct, announced state, and the old file's write-save mapping is replaced, not left dangling. (Task 2)
5. Rapid repeated Place/Remove clicks (faster than the human tester will realistically go, but the overlay thread and the game's read/write thread still touch the same portal) never corrupt state, matching the existing threading test's guarantee. (Task 2, backed by the existing `SoftwarePortal` locking)

---

## File Structure

- `src/portal/figure_catalog.h`, `src/portal/figure_catalog.cpp` — pure catalog scan, no ReXGlue/ImGui dependency.
- `tests/figure_catalog_test.cpp` — its tests.
- `src/hooks/portal_hook.h`, `src/hooks/portal_hook.cpp` — modified: slot→source-file registry, `PlaceFigureFromFile`/`RemoveFigureFromSlot`, `GetSoftwarePortal()` accessor, `--portal_figures_dir` cvar.
- `src/overlay/portal_overlay_dialog.h`, `src/overlay/portal_overlay_dialog.cpp` — new.
- Modify: `CMakeLists.txt`, `src/giantrecomp_app.h`, `README.md`, `docs/superpowers/specs/2026-09-27-giantrecomp-design.md`.

Run every command from the repository root in an **x64 Native Tools** prompt. Build: `cmake --build --preset win-amd64-debug --target <name>`. Tests: `ctest --test-dir out/build/win-amd64-debug`.

---

### Task 1: FigureCatalog (pure, testable)

**Files:**
- Create: `src/portal/figure_catalog.h`, `src/portal/figure_catalog.cpp`, `tests/figure_catalog_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from earlier tasks (this is the first task).
- Produces:
  ```cpp
  namespace giantrecomp::portal {
  struct FigureCatalogEntry {
    std::string name;              // file stem, e.g. "Tree Rex"
    std::string game;              // top-level folder directly under the scan root, e.g. "2. Giants"
    std::filesystem::path path;    // full path to the .dump file
  };
  // Recursively finds every *.dump file under `root`. Entries are sorted by game, then by name
  // (case-insensitive). A missing root, a root that is a file, or a root with nothing found all
  // give an empty (not an error) result. A .dump directly under `root` (no game subfolder) gets
  // game = "" and sorts first.
  std::vector<FigureCatalogEntry> ScanFigureCatalog(const std::filesystem::path& root);
  }
  ```
  and the CMake target/library addition of `src/portal/figure_catalog.cpp` to `giantrecomp_portal`, and the test executable `figure_catalog_test`.

- [ ] **Step 1: Write the failing tests**

Create `tests/figure_catalog_test.cpp`:

```cpp
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
```

- [ ] **Step 2: Add the CMake target and confirm the tests fail**

In `CMakeLists.txt`:
- Add `src/portal/figure_catalog.cpp` to the `giantrecomp_portal` source list.
- Add `figure_catalog_test` to the `foreach(portal_test ...)` list.

Run: `cmake --preset win-amd64-debug && cmake --build --preset win-amd64-debug --target figure_catalog_test`
Expected: FAIL — `src/portal/figure_catalog.cpp` does not exist.

- [ ] **Step 3: Write the implementation**

Create `src/portal/figure_catalog.h`:

```cpp
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
```

Create `src/portal/figure_catalog.cpp`:

```cpp
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
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset win-amd64-debug --target figure_catalog_test && out\build\win-amd64-debug\figure_catalog_test.exe`
Expected: `all figure_catalog tests passed`.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/portal/figure_catalog.h src/portal/figure_catalog.cpp tests/figure_catalog_test.cpp
git commit -m "feat: FigureCatalog scans a folder tree for .dump files"
```

---

### Task 2: Slot-to-file tracking, PlaceFigureFromFile/RemoveFigureFromSlot, GetSoftwarePortal

**Files:**
- Modify: `src/hooks/portal_hook.h`, `src/hooks/portal_hook.cpp`

**Interfaces:**
- Consumes: `portal::FigureData`, `portal::LoadFigureFile`, `portal::SaveFigureFileAtomic` (existing), `portal::SoftwarePortal` (existing, including `SetWriteCallback` from milestone 5).
- Produces:
  ```cpp
  namespace giantrecomp {
  void InstallConfiguredPortal();  // existing, refactored internally

  // Loads the figure at `path` and places it in `slot` on the active software portal; future
  // writes to that slot save back to `path` (replacing any earlier file that slot saved to).
  // Returns false, and changes nothing, if there is no active software portal or `path` cannot be
  // loaded as a figure.
  bool PlaceFigureFromFile(int slot, const std::filesystem::path& path);

  // Removes the figure from `slot`, if any, and forgets what file it was saving to. Returns false
  // if there is no active software portal or the slot was already empty.
  bool RemoveFigureFromSlot(int slot);

  // The active software portal, for read-only status queries (HasFigure/Figure) from the overlay.
  // nullptr if portal_mode is not "software".
  portal::SoftwarePortal* GetSoftwarePortal();
  }
  ```

This task has no dedicated unit tests: it is thin glue over already-tested primitives (`LoadFigureFile`, `SaveFigureFileAtomic`, `SoftwarePortal`) and, like the rest of `portal_hook.cpp`, depends on the full ReXGlue runtime (cvars, `REX_HOOK_RAW`) which the standalone test executables do not link. It is verified by Task 4's end-to-end check.

- [ ] **Step 1: Add the slot-path registry and the new functions**

In `portal_hook.cpp`, replace the anonymous-namespace state and rewrite the figure-loading part of `InstallConfiguredPortal` as follows.

Replace:
```cpp
namespace {

std::atomic<giantrecomp::portal::PortalDevice*> g_portal{nullptr};

// portal_figure/portal_mode arrive as UTF-8; convert explicitly so non-ANSI characters survive
// (path::string() would throw for characters outside the ANSI code page).
std::filesystem::path Utf8ToPath(const std::string& utf8) {
  const std::u8string u8(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
  return std::filesystem::path(u8);
}

}  // namespace
```

with:
```cpp
namespace {

std::atomic<giantrecomp::portal::PortalDevice*> g_portal{nullptr};
std::atomic<giantrecomp::portal::SoftwarePortal*> g_software_portal{nullptr};

std::mutex g_slot_paths_mu;
std::array<std::optional<std::filesystem::path>, giantrecomp::portal::kMaxFigures> g_slot_paths;

// portal_figure/portal_figures_dir arrive as UTF-8; convert explicitly so non-ANSI characters
// survive (path::string() would throw for characters outside the ANSI code page).
std::filesystem::path Utf8ToPath(const std::string& utf8) {
  const std::u8string u8(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
  return std::filesystem::path(u8);
}

}  // namespace
```

Add near the top-level includes: `#include <array>`, `#include <mutex>`, `#include <optional>`.

- [ ] **Step 2: Register one write callback for all slots, keyed by the registry**

Replace the per-figure `SetWriteCallback` lambda set inside the `--portal_figure` branch of `InstallConfiguredPortal` with a single callback set once, right after `software` is constructed and before any figure is placed:

```cpp
  auto* software = new portal::SoftwarePortal();  // intentionally never freed, see the header
  software->SetWriteCallback([](int slot, const portal::FigureData& data) {
    std::optional<std::filesystem::path> path;
    {
      std::lock_guard<std::mutex> lock(g_slot_paths_mu);
      if (slot >= 0 && slot < portal::kMaxFigures) path = g_slot_paths[slot];
    }
    if (!path) return;  // this slot's figure did not come from a file (e.g. --portal_test_figure)
    if (portal::SaveFigureFileAtomic(*path, data)) {
      REXLOG_INFO("Portal: saved changes back to slot {}'s figure file", slot);
    } else {
      REXLOG_WARN("Portal: could not save changes back to slot {}'s figure file", slot);
    }
  });
```

Then simplify the `--portal_figure` handling to use the new `PlaceFigureFromFile` (defined in the next step) instead of calling `LoadFigureFile`/`PlaceFigure`/`SetWriteCallback` directly:

```cpp
  g_software_portal.store(software);
  g_portal.store(software);

  const std::string figure_path = REXCVAR_GET(portal_figure);
  if (!figure_path.empty()) {
    if (!PlaceFigureFromFile(0, Utf8ToPath(figure_path))) {
      REXLOG_WARN("Portal: cannot load '{}' (it must be a regular file of exactly {} bytes); "
                  "running with an empty portal",
                  figure_path, portal::kFigureSize);
    }
  } else if (REXCVAR_GET(portal_test_figure)) {
    software->PlaceFigure(0, portal::FigureData{});
    REXLOG_WARN("Portal: placed an all-zero test figure in slot 0");
  }
  REXLOG_INFO("Portal: software");
```

Note the store of both atomics now happens *before* placing the startup figure, so `PlaceFigureFromFile`/`GetSoftwarePortal` (called from inside this same function) see a consistent, already-published portal — this is safe because `InstallConfiguredPortal` runs on the app's setup thread, before the game's guest threads exist to call the hooks concurrently.

- [ ] **Step 3: Implement the new public functions**

Add, in the `namespace giantrecomp { ... }` block (after `InstallConfiguredPortal`):

```cpp
bool PlaceFigureFromFile(int slot, const std::filesystem::path& path) {
  portal::SoftwarePortal* software = g_software_portal.load();
  if (!software) return false;
  auto figure = portal::LoadFigureFile(path);
  if (!figure) return false;
  if (!software->PlaceFigure(slot, *figure)) return false;
  {
    std::lock_guard<std::mutex> lock(g_slot_paths_mu);
    if (slot >= 0 && slot < portal::kMaxFigures) g_slot_paths[slot] = path;
  }
  return true;
}

bool RemoveFigureFromSlot(int slot) {
  portal::SoftwarePortal* software = g_software_portal.load();
  if (!software) return false;
  const bool removed = software->RemoveFigure(slot);
  {
    std::lock_guard<std::mutex> lock(g_slot_paths_mu);
    if (slot >= 0 && slot < portal::kMaxFigures) g_slot_paths[slot].reset();
  }
  return removed;
}

portal::SoftwarePortal* GetSoftwarePortal() { return g_software_portal.load(); }
```

- [ ] **Step 4: Declare the new functions and the `portal_figures_dir` cvar**

In `portal_hook.h`, add forward declarations and the new function signatures:

```cpp
#pragma once

#include <filesystem>

namespace giantrecomp::portal {
class SoftwarePortal;
}  // namespace giantrecomp::portal

namespace giantrecomp {

// Creates the portal selected by the `portal_mode` cvar and routes the game's portal reads and
// writes to it. With no portal (mode `none` or an unknown value) the game keeps its own path and
// shows "Can't find the Portal of Power". The portal lives for the whole process, because game
// threads may still call into it while the app shuts down.
void InstallConfiguredPortal();

// Loads the figure at `path` and places it in `slot`; future writes to that slot save back to
// `path`. Returns false, and changes nothing, if there is no active software portal or `path`
// cannot be loaded as a figure.
bool PlaceFigureFromFile(int slot, const std::filesystem::path& path);

// Removes the figure from `slot`, if any, and forgets what file it was saving to.
bool RemoveFigureFromSlot(int slot);

// The active software portal, or nullptr if portal_mode is not "software".
portal::SoftwarePortal* GetSoftwarePortal();

}  // namespace giantrecomp
```

In `portal_hook.cpp`, add the new cvar next to the existing ones:

```cpp
REXCVAR_DEFINE_STRING(portal_figures_dir, "", "Portal",
                      "Folder to search for .dump figure files for the in-game figure picker "
                      "(F6). Searched recursively; only used by the overlay.");
```

- [ ] **Step 5: Build**

Run: `cmake --build --preset win-amd64-debug --target giantrecomp`
Expected: builds and links with no errors. If `portal::kMaxFigures` or `portal::FigureData` are reported as undeclared in `portal_hook.cpp`, confirm `#include "portal/portal_device.h"` is present (it already is, from milestone 4).

- [ ] **Step 6: Run the full suite (regression check)**

Run: `ctest --test-dir out/build/win-amd64-debug`
Expected: `100% tests passed out of 6` (the five from before, plus `figure_catalog_test` from Task 1).

- [ ] **Step 7: Smoke-check the refactor did not change behavior**

Run the game briefly with the existing `--portal_figure` option, exactly as before:

```
out\build\win-amd64-debug\giantrecomp.exe --game_data_root rom --portal_figure "<a .dump path>" --log_file logs\m6-smoke.log
```

Expected in the log: `Portal: placed the figure from` is gone (the message moved into `PlaceFigureFromFile`'s caller, which does not log a placement message itself) — instead confirm no `FATAL`/`critical` lines and that `Portal: software` appears. This is a smoke check, not a full replay of milestone 5; Task 4's end-to-end check covers the user-visible behavior.

- [ ] **Step 8: Commit**

```bash
git add src/hooks/portal_hook.h src/hooks/portal_hook.cpp
git commit -m "refactor: track each slot's source file so any placed figure saves correctly

Adds PlaceFigureFromFile/RemoveFigureFromSlot/GetSoftwarePortal for the
overlay (milestone 6) and the portal_figures_dir cvar it will read."
```

---

### Task 3: PortalOverlayDialog

**Files:**
- Create: `src/overlay/portal_overlay_dialog.h`, `src/overlay/portal_overlay_dialog.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `portal::FigureCatalogEntry`, `portal::ScanFigureCatalog` (Task 1); `giantrecomp::PlaceFigureFromFile`, `RemoveFigureFromSlot`, `GetSoftwarePortal` (Task 2); `rex::ui::ImGuiDialog`, `rex::ui::ImGuiDrawer`, `rex::cvar::REXCVAR_GET` (SDK).
- Produces:
  ```cpp
  namespace giantrecomp {
  class PortalOverlayDialog : public rex::ui::ImGuiDialog {
   public:
    explicit PortalOverlayDialog(rex::ui::ImGuiDrawer* drawer);
   protected:
    void OnDraw(ImGuiIO& io) override;
   private:
    // rescan the catalog from the portal_figures_dir cvar; called on construction and on demand
    void Rescan();
    std::vector<portal::FigureCatalogEntry> entries_;
    std::string figures_dir_at_last_scan_;
    char filter_[128] = {};
  };
  }
  ```
  Later tasks (Task 4) construct this from `GiantrecompApp::OnCreateDialogs`.

No dedicated unit tests: this class only calls ImGui and the already-tested functions from Tasks 1 and 2, matching how the SDK's own overlay dialogs (`DebugOverlayDialog`, etc.) are not unit tested either. It is verified visually in Task 4's end-to-end check.

- [ ] **Step 1: Write the dialog header**

Create `src/overlay/portal_overlay_dialog.h`:

```cpp
#pragma once

#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>

#include "portal/figure_catalog.h"

struct ImGuiIO;

namespace rex::ui {
class ImGuiDrawer;
}  // namespace rex::ui

namespace giantrecomp {

// The in-game figure picker (F6). Browses .dump files under the portal_figures_dir cvar and
// places or removes the figure in slot 0 on the active software portal. Talks only to
// PlaceFigureFromFile/RemoveFigureFromSlot/GetSoftwarePortal (hooks/portal_hook.h) and
// ScanFigureCatalog (portal/figure_catalog.h); it never touches portal protocol bytes.
class PortalOverlayDialog : public rex::ui::ImGuiDialog {
 public:
  explicit PortalOverlayDialog(rex::ui::ImGuiDrawer* drawer);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void Rescan();

  std::vector<portal::FigureCatalogEntry> entries_;
  std::string figures_dir_at_last_scan_;
  char filter_[128] = {};
};

}  // namespace giantrecomp
```

- [ ] **Step 2: Write the dialog implementation**

Create `src/overlay/portal_overlay_dialog.cpp`:

```cpp
#include "overlay/portal_overlay_dialog.h"

#include <algorithm>
#include <cctype>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/ui/imgui_drawer.h>

#include "hooks/portal_hook.h"

namespace giantrecomp {

namespace {

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::filesystem::path Utf8ToPath(const std::string& utf8) {
  const std::u8string u8(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
  return std::filesystem::path(u8);
}

}  // namespace

PortalOverlayDialog::PortalOverlayDialog(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {
  Rescan();
}

void PortalOverlayDialog::Rescan() {
  figures_dir_at_last_scan_ = REXCVAR_GET(portal_figures_dir);
  entries_ = figures_dir_at_last_scan_.empty()
                 ? std::vector<portal::FigureCatalogEntry>{}
                 : portal::ScanFigureCatalog(Utf8ToPath(figures_dir_at_last_scan_));
}

void PortalOverlayDialog::OnDraw(ImGuiIO& io) {
  (void)io;
  ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Portal of Power (F6)", nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  portal::SoftwarePortal* software = GetSoftwarePortal();
  if (!software) {
    ImGui::TextWrapped(
        "No software portal is active (portal_mode is not 'software'). Start the game with "
        "--portal_mode software to use the figure picker.");
    ImGui::End();
    return;
  }

  if (auto current = software->Figure(0)) {
    ImGui::TextWrapped("Slot 0: a figure is on the portal.");
    ImGui::SameLine();
    if (ImGui::Button("Remove")) RemoveFigureFromSlot(0);
  } else {
    ImGui::TextWrapped("Slot 0: empty.");
  }

  ImGui::Separator();

  const std::string current_dir = REXCVAR_GET(portal_figures_dir);
  if (current_dir.empty()) {
    ImGui::TextWrapped(
        "No figures folder is set. Start the game with --portal_figures_dir \"<folder>\" to "
        "browse your .dump files here.");
    ImGui::End();
    return;
  }
  if (current_dir != figures_dir_at_last_scan_) Rescan();  // the cvar can change via the console

  ImGui::InputTextWithHint("Filter", "figure name", filter_, sizeof(filter_));
  ImGui::SameLine();
  if (ImGui::Button("Rescan")) Rescan();

  if (entries_.empty()) {
    ImGui::TextWrapped("No .dump files found under '%s'.", figures_dir_at_last_scan_.c_str());
    ImGui::End();
    return;
  }

  const std::string filter = Lower(filter_);
  ImGui::BeginChild("figure_list", ImVec2(0, 0), true);
  std::string last_game;
  for (const auto& entry : entries_) {
    if (!filter.empty() && Lower(entry.name).find(filter) == std::string::npos) continue;
    if (entry.game != last_game) {
      ImGui::SeparatorText(entry.game.empty() ? "(no game folder)" : entry.game.c_str());
      last_game = entry.game;
    }
    ImGui::PushID(entry.path.string().c_str());
    ImGui::TextUnformatted(entry.name.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - 80);
    if (ImGui::Button("Place")) {
      if (!PlaceFigureFromFile(0, entry.path)) {
        REXLOG_WARN("Portal overlay: could not place '{}'", entry.path.string());
      }
    }
    ImGui::PopID();
  }
  ImGui::EndChild();

  ImGui::End();
}

}  // namespace giantrecomp
```

Add `#include <rex/logging.h>` to the includes for `REXLOG_WARN`.

- [ ] **Step 3: Add the CMake source**

In `CMakeLists.txt`, add `src/overlay/portal_overlay_dialog.cpp` to `GIANTRECOMP_SOURCES` (not to `giantrecomp_portal` — this file depends on the SDK's UI headers and is only ever compiled into the game executable).

- [ ] **Step 4: Build**

Run: `cmake --preset win-amd64-debug && cmake --build --preset win-amd64-debug --target giantrecomp`
Expected: a link error naming `PortalOverlayDialog` as unused-but-defined is fine (it is not constructed anywhere yet); any *compile* error must be fixed here. If `imgui.h` is not found, confirm the target already has the imgui include path from milestone 4's `if(REXSDK_DIR) target_include_directories(... imgui)` block.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/overlay
git commit -m "feat: PortalOverlayDialog lists and places figures from a folder"
```

---

### Task 4: Wire up the hotkey, build, and end-to-end check

**Files:**
- Modify: `src/giantrecomp_app.h`, `README.md`, `docs/superpowers/specs/2026-09-27-giantrecomp-design.md`

**Interfaces:**
- Consumes: `giantrecomp::PortalOverlayDialog` (Task 3).

- [ ] **Step 1: Register the hotkey and the dialog**

In `src/giantrecomp_app.h`, add `#include "overlay/portal_overlay_dialog.h"` and `#include <rex/ui/keybinds.h>` next to the other includes, add a member `std::unique_ptr<giantrecomp::PortalOverlayDialog> portal_overlay_;`, and add:

```cpp
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    rex::ui::RegisterBind("bind_portal_overlay", "F6", "Toggle the portal figure picker",
                          [this, drawer] {
                            if (portal_overlay_) {
                              portal_overlay_.reset();
                            } else {
                              portal_overlay_ = std::make_unique<giantrecomp::PortalOverlayDialog>(drawer);
                            }
                          });
  }
```

Remove the commented-out `// void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}` placeholder line if it is still present.

- [ ] **Step 2: Build**

Run: `cmake --build --preset win-amd64-debug --target giantrecomp`
Expected: links successfully.

- [ ] **Step 3: Run the full suite**

Run: `ctest --test-dir out/build/win-amd64-debug`
Expected: `100% tests passed out of 6`.

- [ ] **Step 4: End-to-end check with the real game (needs a person at the controller and a mouse)**

Ask the human to run:

```
out\build\win-amd64-release\giantrecomp.exe --game_data_root rom --portal_mode software --portal_figures_dir "C:\Users\Noah\Documents\giantsrecomp\Dumps" --gpu_allow_invalid_fetch_constants --log_file logs\m6-e2e.log
```

(Rebuild Release first if Tasks 1-4's changes have not been built there yet: repeat the four `win-amd64-release` commands from the README.)

Ask them to:
1. Press **A** at the title, get to the "please put a Skylander on the Portal" screen.
2. Press **F6**. Expect the "Portal of Power (F6)" window, with games listed and a filter box.
3. Type part of a figure's name into the filter and confirm the list narrows.
4. Click **Place** on a figure. Expect the game to pick it up within a couple of seconds, the same as `--portal_figure` did in milestone 5, and the dialog's "Slot 0" line to say a figure is present.
5. Press F6 again to close the overlay, confirm the game is still fully playable underneath (mouse clicks on the list must not have reached the game).
6. Press F6, click **Remove**. Expect the game to show its "please put a Skylander" prompt again.
7. Place a *different* figure than in step 4, play briefly (enough to change something — defeating an enemy or picking up gold), quit, and reopen that figure's `.dump` file's timestamp/hash to confirm it changed (the milestone-5 save behavior, now exercised through the overlay instead of `--portal_figure`).

Record the result (and any log `FATAL` lines) in `docs/investigation/portal-protocol.md` under a new "Overlay (milestone 6)" heading, the same way milestones 4 and 5 were recorded. If a step fails, use `systematic-debugging` before changing code — read the log first.

- [ ] **Step 5: Update the docs**

In `README.md`, add to the "Playing" section's option list: `--portal_figures_dir <folder>` enables the in-game figure picker (press **F6**), and mention F6 alongside the existing bullet points. Update both launcher `.cmd` files (`Play Giants (Tree Rex).cmd`, `Play Giants Release (Tree Rex).cmd`) to add `--portal_figures_dir "C:\Users\Noah\Documents\giantsrecomp\Dumps"` so F6 works out of the box; keep `--portal_figure` in them too, so a figure is still preloaded at startup.

In `docs/superpowers/specs/2026-09-27-giantrecomp-design.md` section 4.5, replace the description with what was actually built: a mouse-driven ImGui window (not a fully controller-navigable one — the SDK's ImGui integration does not wire up gamepad navigation, so full controller support is future work), opened with F6 (not colliding with F3/F4/backtick/F7), filtering by name, grouped by game folder. Note this as a deliberate scope reduction from the original "controller-navigable" wording, since a PC always has a mouse even when the game itself is controller-only.

- [ ] **Step 6: Commit**

```bash
git add src/giantrecomp_app.h README.md docs
git commit -m "feat: open the portal overlay with F6"
```
