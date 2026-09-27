# Figure Stats (Level/Gold/Nickname) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire a complete, end-to-end path for decoding and displaying a Skylander's level/gold/nickname in the F6 overlay, for both virtual dump files and a real figure on a real portal.

**Architecture:** A new `figure_stats` module owns the decode (`FigureData -> optional<FigureStats>`), used identically by the Browse tab (already-loaded bytes) and by a new mutex-guarded `UsbPortal::ReadAllBlocks` (fresh bytes pulled from real hardware on demand). The real byte offsets for level/gold/nickname are not yet known — this plan builds every piece *except* the decode logic itself, which currently only recognizes the one case we can prove today (no save data yet -> blank). Task 1 adds the logging needed to determine the real offsets from an actual play session; that data is a separate, human-driven follow-up this plan cannot complete on its own (see "What happens after this plan," at the end).

**Tech Stack:** C++23, this project's lightweight test harness (`tests/test_util.h`: `CHECK`/`Finish`, plain `main()`, registered via `add_test` in `CMakeLists.txt`), REXLOG (spdlog-backed) logging, hidapi for real USB portal I/O.

**Spec:** `docs/superpowers/specs/2026-09-27-figure-stats-design.md`

## Global Constraints

- Only level, gold, and nickname are in scope — no other fields (spec, "Goal").
- `ParseFigureStats` returns `nullopt` for anything unrecognized (blank, foreign-format, corrupted) — never a guessed or partial result (spec, "Error handling").
- `ReadAllBlocks` aborts the whole read on the first failed block — never returns a partially-read `FigureData` (spec, "Error handling").
- The real-portal read is on-demand only (dialog open / explicit Refresh) — never a per-frame or continuously-polled path (spec, "Components").
- All raw HID I/O on `UsbPortal` (`Write`, `Read`, and the new `ReadAllBlocks`) must go through one mutex, so the game's hook thread and the UI thread can never touch the physical device concurrently (spec, "UsbPortal — thread-safe raw block read").

## Review Focus

- **Figure removed from its slot mid-read** (Refresh pressed, then the figure comes off the portal before all 64 blocks are read): `ReadAllBlocks` must not crash or return a torn buffer as if valid — Task 2.
- **USB read/write failure or timeout partway through the 64-block sequence** (device unplugged mid-read): the whole read aborts and returns `nullopt`, not a partially-filled `FigureData` — Task 2.
- **Out-of-range or absent-device calls to `ReadAllBlocks`** (`slot < 0`, `slot >= kMaxFigures`, or no device open): defined `nullopt`, not undefined behavior, matching `FigurePresent`/`PresentSlots`'s existing range safety — Task 2.
- **Two callers of the new mutex-guarded path racing** (Refresh double-clicked, or Refresh fired while the game's hook thread is mid-poll): must serialize, never deadlock — Task 2's design (single lock per public entry point, no recursive locking) plus a manual read-the-diff check.
- **A structurally-valid-but-never-played figure** (a blank figure from `CreateAndPlaceFigure`, or any dump whose save-data region truly is zero): must show as "no stats," not zeros presented as real level-0/gold-0 data — Task 1's test, reusing `CreateBlankFigure`.

---

### Task 1: `figure_stats` module

**Files:**
- Create: `src/portal/figure_stats.h`
- Create: `src/portal/figure_stats.cpp`
- Create: `tests/figure_stats_test.cpp`
- Modify: `CMakeLists.txt:105-120` (add the new source to `giantrecomp_portal`, add the new test to the portal-test `foreach`)

**Interfaces:**
- Produces: `struct giantrecomp::portal::FigureStats { uint8_t level; uint32_t gold; std::string nickname; };` and `std::optional<FigureStats> giantrecomp::portal::ParseFigureStats(const FigureData& data);` — used by Task 4's overlay code.

- [ ] **Step 1: Write the failing test**

Create `tests/figure_stats_test.cpp`:

```cpp
#include "portal/figure_file.h"
#include "portal/figure_stats.h"
#include "test_util.h"

using namespace giantrecomp::portal;

int main() {
  // A figure with no save data at all (e.g. --portal_test_figure) has nothing to report.
  {
    FigureData d{};
    CHECK(!ParseFigureStats(d).has_value());
  }

  // A freshly created blank figure (real id/variant/CRC, but never played -- its save-data
  // region is still zero) also has no stats yet, not zeros presented as real data.
  {
    FigureData d = CreateBlankFigure(112, 0, {0x11, 0x22, 0x33, 0x44});
    CHECK(!ParseFigureStats(d).has_value());
  }

  return Finish("figure_stats");
}
```

- [ ] **Step 2: Run test to verify it fails to build**

Run: `cmake --build --preset win-amd64-debug --target figure_stats_test`
Expected: FAIL — `portal/figure_stats.h` does not exist yet.

- [ ] **Step 3: Write the module**

Create `src/portal/figure_stats.h`:

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "portal/portal_device.h"

namespace giantrecomp::portal {

// A Skylander's in-game progress, decoded from its save-data bytes.
struct FigureStats {
  uint8_t level;
  uint32_t gold;
  std::string nickname;
};

// Decodes level/gold/nickname from a figure's raw bytes. Returns nullopt for a figure with no
// save data yet (freshly created, or any other all-zero save-data region) or bytes that don't
// look like valid Giants save data -- never a guessed or partial result.
//
// The real byte layout is still being determined empirically (see docs/superpowers/specs/
// 2026-09-27-figure-stats-design.md); until that lands, every non-blank figure also falls
// through to nullopt below.
std::optional<FigureStats> ParseFigureStats(const FigureData& data);

}  // namespace giantrecomp::portal
```

Create `src/portal/figure_stats.cpp`:

```cpp
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
```

- [ ] **Step 4: Wire it into the build**

In `CMakeLists.txt`, add the new source to the `giantrecomp_portal` library (around line 105):

```cmake
add_library(giantrecomp_portal STATIC
    src/portal/figure_catalog.cpp
    src/portal/figure_file.cpp
    src/portal/figure_stats.cpp
    src/portal/portal_mode.cpp
    src/portal/xbox_frame.cpp
    src/portal/software/software_portal.cpp
)
```

Add `figure_stats_test` to the portal-test `foreach` (around line 115):

```cmake
foreach(portal_test portal_framing_test portal_mode_test software_portal_test figure_file_test figure_catalog_test figure_stats_test)
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --preset win-amd64-debug && cmake --build --preset win-amd64-debug --target figure_stats_test && ctest --test-dir out/build/win-amd64-debug -R figure_stats_test --output-on-failure`
Expected: `all figure_stats tests passed`, `100% tests passed`

- [ ] **Step 6: Commit**

```bash
git add src/portal/figure_stats.h src/portal/figure_stats.cpp tests/figure_stats_test.cpp CMakeLists.txt
git commit -m "feat: add figure_stats module (blank-figure case only, real offsets pending research)"
```

---

### Task 2: `UsbPortal` thread-safe full-figure read

**Files:**
- Modify: `src/portal/usb/usb_portal.h:37-75` (add mutex member, new public method)
- Modify: `src/portal/usb/usb_portal.cpp:37-66` (refactor `Write`/`Read` onto locked raw helpers, add `ReadAllBlocks`)

**Interfaces:**
- Consumes: `Report`, `kReportSize`, `kBlockSize`, `kBlockCount`, `kMaxFigures`, `FigureData` (`src/portal/portal_device.h`, unchanged).
- Produces: `std::optional<FigureData> UsbPortal::ReadAllBlocks(int slot)` — used by Task 3's `portal_hook.cpp` wrapper.

No automated test: this needs a real Wii U Traptanium portal, matching every other `UsbPortal` method today (none of them have unit tests — see `CMakeLists.txt`'s test list, which has no `usb_portal_test`). Verified by a build check plus a manual read-through confirming the lock covers every entry point, and (if you have the real hardware handy) a manual run.

- [ ] **Step 1: Add the mutex and new method declaration**

In `src/portal/usb/usb_portal.h`, add to the public section (after `HadError()`):

```cpp
  // Reads all 64 blocks of the figure in `slot` directly, independent of the game's own polling.
  // Returns nullopt on any timeout, I/O error, out-of-range slot, or no device open. Acquires
  // io_mutex_ for the whole sequence, so it briefly blocks the game's own Write()/Read() calls --
  // on-demand only (e.g. an overlay Refresh button), never called from a per-frame path.
  std::optional<FigureData> ReadAllBlocks(int slot);
```

Add to the private section (after `void ObserveReply(...)`):

```cpp
  void SendRaw(const Report& report);  // unlocked -- callers hold io_mutex_
  Report ReceiveRaw();                 // unlocked -- callers hold io_mutex_

  std::mutex io_mutex_;  // serializes every raw HID transfer: Write(), Read(), ReadAllBlocks()
```

- [ ] **Step 2: Run a build to confirm the header still compiles standalone**

Run: `cmake --build --preset win-amd64-debug --target giantrecomp`
Expected: FAIL — `SendRaw`/`ReceiveRaw`/`ReadAllBlocks` declared but not defined yet (link or "undefined" style error is fine here; this step only proves the header parses).

- [ ] **Step 3: Refactor `Write`/`Read` onto the raw helpers, under the lock**

In `src/portal/usb/usb_portal.cpp`, replace the existing `Write`/`Read` bodies:

```cpp
void UsbPortal::SendRaw(const Report& report) {
  if (device_ == nullptr) return;
  // hid_send_output_report() sends via a HID SET_REPORT control transfer -- unlike hid_write(),
  // which prefers this device's interrupt OUT endpoint and is accepted at the transport level but
  // silently discarded by its firmware. Needs a leading report-ID byte (0, this device has no
  // numbered reports), so the buffer is one longer than the report itself.
  std::array<uint8_t, kReportSize + 1> buffer{};  // buffer[0] = report ID 0
  std::copy(report.begin(), report.end(), buffer.begin() + 1);
  const int written = hid_send_output_report(device_, buffer.data(), buffer.size());
  if (written < 0 && !write_error_logged_.exchange(true)) {
    REXLOG_WARN("Portal (usb): hid_send_output_report failed: {}", HidErrorUtf8(device_));
  }
}

Report UsbPortal::ReceiveRaw() {
  if (device_ == nullptr) return Report{};
  Report report{};
  // A short timeout keeps this from blocking indefinitely if the device stops responding; an
  // all-zero Report on timeout/failure is a shape the game already tolerates
  // (docs/portal-protocol.md).
  const int bytes_read = hid_read_timeout(device_, report.data(), report.size(), 50);
  // 0 means "no report within the timeout", hidapi's normal outcome for an idle poll, not a
  // failure. -1 is the actual error indicator.
  if (bytes_read < 0 && !read_error_logged_.exchange(true)) {
    REXLOG_WARN("Portal (usb): hid_read failed: {}", HidErrorUtf8(device_));
  }
  return bytes_read > 0 ? report : Report{};
}

void UsbPortal::Write(const Report& report) {
  std::lock_guard<std::mutex> lock(io_mutex_);
  if (report[0] == 0x57) {  // 'W': write one figure block -- log for offset research.
    const int slot = report[1] & 0x0F;
    const int block = report[2];
    REXLOG_TRACE("Portal figure research: usb slot {} block {} write -> {}", slot, block,
                 HexBytes(&report[3], kBlockSize));
  }
  SendRaw(report);
}

Report UsbPortal::Read() {
  std::lock_guard<std::mutex> lock(io_mutex_);
  Report report = ReceiveRaw();
  if (report[0] == 0) return report;  // timeout/failure/no-device: nothing to observe
  ObserveReply(report);
  return report;
}
```

Note the `HexBytes` helper this calls does not exist yet — Step 4 adds it, since `ObserveReply`
(edited in Task's Step 5 below) needs the same helper.

- [ ] **Step 4: Add the shared hex-dump helper for the research logging**

In `src/portal/usb/usb_portal.cpp`'s anonymous namespace (next to `HidErrorUtf8`), add:

```cpp
std::string HexBytes(const uint8_t* data, size_t n) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(n * 3);
  for (size_t i = 0; i < n; ++i) {
    if (i) out += ' ';
    out += kHex[data[i] >> 4];
    out += kHex[data[i] & 0xF];
  }
  return out;
}
```

- [ ] **Step 5: Extend `ObserveReply` to log every block read, and add `ReadAllBlocks`**

In `src/portal/usb/usb_portal.cpp`, in `ObserveReply`, add a general-block trace log before the
existing block-1-specific id/variant handling (which stays as-is):

```cpp
  if (report[0] == 0x51 && (report[1] & 0x10) != 0) {
    const int slot = report[1] & 0x0F;
    const int block = report[2];
    REXLOG_TRACE("Portal figure research: usb slot {} block {} read -> {}", slot, block,
                 HexBytes(&report[3], kBlockSize));
  }
```

(This sits alongside, not instead of, the existing `if (report[0] == 0x51 && ... && report[2] == 1)` id/variant block below it.)

Add `ReadAllBlocks` at the end of the file, before the closing namespace brace:

```cpp
std::optional<FigureData> UsbPortal::ReadAllBlocks(int slot) {
  if (slot < 0 || slot >= kMaxFigures) return std::nullopt;
  std::lock_guard<std::mutex> lock(io_mutex_);
  if (device_ == nullptr) return std::nullopt;

  FigureData data{};
  for (int block = 0; block < static_cast<int>(kBlockCount); ++block) {
    Report request{};
    request[0] = 0x51;  // 'Q'
    request[1] = static_cast<uint8_t>(slot & 0x0F);
    request[2] = static_cast<uint8_t>(block);
    SendRaw(request);
    Report reply = ReceiveRaw();
    if (reply[0] != 0x51 || (reply[1] & 0x10) == 0 || reply[2] != block) return std::nullopt;
    ObserveReply(reply);
    std::copy_n(reply.begin() + 3, kBlockSize, data.begin() + block * kBlockSize);
  }
  return data;
}
```

- [ ] **Step 6: Build**

Run: `cmake --build --preset win-amd64-debug --target giantrecomp`
Expected: succeeds.

- [ ] **Step 7: Manual read-through against the Review Focus items**

Confirm by inspection (no automated test covers this):
- `ReadAllBlocks` checks `slot` range before touching `device_` or the mutex.
- `ReadAllBlocks` checks `device_ == nullptr` after acquiring the lock, before any I/O.
- Every block's reply is validated (`reply[0]`, the present bit, and the block index all match) before its bytes are trusted — a failure on block N discards the whole call via early `return std::nullopt`, not just block N.
- `Write`, `Read`, and `ReadAllBlocks` each take `io_mutex_` exactly once per call, via `std::lock_guard`, and never call each other while already holding it (no recursive lock).

- [ ] **Step 8: Commit**

```bash
git add src/portal/usb/usb_portal.h src/portal/usb/usb_portal.cpp
git commit -m "feat: add thread-safe UsbPortal::ReadAllBlocks, plus research logging for Q/W blocks"
```

---

### Task 3: `portal_hook` wrapper for the real-portal read

**Files:**
- Modify: `src/hooks/portal_hook.h:49-56` (add declaration after `GetUsbPortal()`)
- Modify: `src/hooks/portal_hook.cpp:150-153` (add definition after `GetUsbPortal()`)

**Interfaces:**
- Consumes: `portal::UsbPortal::ReadAllBlocks(int)` (Task 2), `g_usb_portal` (existing, `portal_hook.cpp:37`).
- Produces: `std::optional<portal::FigureData> giantrecomp::ReadRealFigureBlocks(int slot);` — used by Task 4's overlay code.

No automated test: this is a one-line passthrough over a function that itself has no automated test (Task 2). Verified by the build and by Task 4's manual overlay check.

- [ ] **Step 1: Add the declaration**

In `src/hooks/portal_hook.h`, after the existing `GetUsbPortal()` declaration:

```cpp
// Reads a real figure's full data directly from the portal, independent of the game's own
// polling. Returns nullopt if there's no active USB portal, no figure in `slot`, or the read
// fails.
std::optional<portal::FigureData> ReadRealFigureBlocks(int slot);
```

- [ ] **Step 2: Add the definition**

In `src/hooks/portal_hook.cpp`, after the existing `GetUsbPortal()` definition:

```cpp
std::optional<portal::FigureData> ReadRealFigureBlocks(int slot) {
  portal::UsbPortal* usb = g_usb_portal.load();
  if (!usb) return std::nullopt;
  return usb->ReadAllBlocks(slot);
}
```

- [ ] **Step 3: Build**

Run: `cmake --build --preset win-amd64-debug --target giantrecomp`
Expected: succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/hooks/portal_hook.h src/hooks/portal_hook.cpp
git commit -m "feat: add ReadRealFigureBlocks portal_hook wrapper"
```

---

### Task 4: Overlay wiring

**Files:**
- Modify: `src/overlay/portal_overlay_dialog.h:1-37` (new includes, new member, new method declaration)
- Modify: `src/overlay/portal_overlay_dialog.cpp:1-94,197-214` (new includes, real-portal Refresh + stats display, Browse-tab inline stats)

**Interfaces:**
- Consumes: `portal::ParseFigureStats` (Task 1), `ReadRealFigureBlocks` (Task 3), `portal::LoadFigureFile` (existing, `figure_file.h`).

No automated test: this is ImGui rendering code with no headless test harness in this project (matches every other method in this file, none of which are unit tested — only the non-UI modules under `src/portal/` have `tests/*.cpp`). Verified by a build and a manual run.

- [ ] **Step 1: Add includes and new members to the header**

In `src/overlay/portal_overlay_dialog.h`, add includes:

```cpp
#include <optional>
#include <unordered_map>
```

and:

```cpp
#include "portal/figure_stats.h"
```

Add to the private section:

```cpp
  void RefreshRealFigureStats();

  std::unordered_map<int, portal::FigureStats> real_figure_stats_;  // slot -> decoded stats
```

- [ ] **Step 2: Add includes to the .cpp and implement `RefreshRealFigureStats`**

In `src/overlay/portal_overlay_dialog.cpp`, add includes:

```cpp
#include "portal/figure_file.h"
#include "portal/figure_stats.h"
```

Add the method (near `Rescan()`):

```cpp
void PortalOverlayDialog::RefreshRealFigureStats() {
  real_figure_stats_.clear();
  portal::UsbPortal* usb = GetUsbPortal();
  if (!usb) return;
  for (int slot : usb->PresentSlots()) {
    if (auto blocks = ReadRealFigureBlocks(slot)) {
      if (auto stats = portal::ParseFigureStats(*blocks)) {
        real_figure_stats_[slot] = *stats;
      }
    }
  }
}
```

Call it from the constructor, alongside the existing `Rescan()`:

```cpp
PortalOverlayDialog::PortalOverlayDialog(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {
  Rescan();
  RefreshRealFigureStats();
}
```

- [ ] **Step 3: Add a Refresh button and stats display to the real-portal branch**

In `src/overlay/portal_overlay_dialog.cpp`'s `OnDraw`, in the `if (portal::UsbPortal* usb = GetUsbPortal())` branch, replace the slot-listing loop:

```cpp
    ImGui::Separator();
    if (ImGui::Button("Refresh")) RefreshRealFigureStats();
    ImGui::Separator();
    const std::vector<int> present_slots = usb->PresentSlots();
    if (present_slots.empty()) {
      ImGui::TextWrapped("No figure detected on the portal.");
    } else {
      // A real portal can hold more than one figure at once (2-player co-op, items), so every
      // occupied slot is listed, not just the first.
      for (int slot : present_slots) {
        if (auto id_variant = usb->DetectedIdVariant(slot)) {
          const auto* sky = portal::FindSkylander(id_variant->first, id_variant->second);
          if (sky) {
            ImGui::Text("Slot %d: %s", slot, std::string(sky->name).c_str());
          } else {
            ImGui::Text("Slot %d: unrecognized figure (id %u, variant %u)", slot,
                        static_cast<unsigned>(id_variant->first),
                        static_cast<unsigned>(id_variant->second));
          }
        } else {
          ImGui::Text("Slot %d: figure detected, identity not read yet", slot);
        }
        if (auto it = real_figure_stats_.find(slot); it != real_figure_stats_.end()) {
          ImGui::Text("  Level %u, %u gold, \"%s\"", static_cast<unsigned>(it->second.level),
                      static_cast<unsigned>(it->second.gold), it->second.nickname.c_str());
        } else {
          ImGui::TextDisabled("  (press Refresh to read level/gold/nickname)");
        }
      }
    }
```

(This replaces the block that previously went straight from `ImGui::Separator();` into `const std::vector<int> present_slots = usb->PresentSlots();` — the rest of that branch, and the two branches above/below it in `OnDraw`, are unchanged.)

- [ ] **Step 4: Add inline stats to the Browse tab list**

In the same file's Browse-tab loop (the `for (const auto& entry : entries_)` block), after
`ImGui::TextUnformatted(entry.display_name.c_str());`:

```cpp
    if (auto data = portal::LoadFigureFile(entry.path)) {
      if (auto stats = portal::ParseFigureStats(*data)) {
        ImGui::SameLine();
        ImGui::TextDisabled("(Lv %u, %u gold)", static_cast<unsigned>(stats->level),
                            static_cast<unsigned>(stats->gold));
      }
    }
```

- [ ] **Step 5: Build**

Run: `cmake --build --preset win-amd64-debug --target giantrecomp`
Expected: succeeds.

- [ ] **Step 6: Manual run**

Run: `out\build\win-amd64-debug\giantrecomp.exe --portal_mode software --portal_figures_dir <a folder with a .dump file>` (or `just play-debug`)
Expected: press F6 — the Browse tab still lists dumps as before (no stats line yet, since every
figure currently decodes to `nullopt` until Task 1's real offsets land — this confirms nothing
crashes or regresses, not that stats appear). If a real USB portal is connected, the Refresh
button appears and does not error.

- [ ] **Step 7: Commit**

```bash
git add src/overlay/portal_overlay_dialog.h src/overlay/portal_overlay_dialog.cpp
git commit -m "feat: wire figure stats display into the F6 overlay (Browse tab + real-portal Refresh)"
```

---

## What happens after this plan

Every piece of plumbing is now in place end-to-end, but `ParseFigureStats` only recognizes "no
save data yet" — it cannot show a real level/gold/nickname until the actual byte offsets are
known. Task 2's `--log_level trace` logging (`Portal figure research: ...` lines, from both
`SoftwarePortal`'s `Q`/`W` handlers if you're testing with `portal_mode software`, and
`UsbPortal`'s `Write`/`ObserveReply` if you're testing with a real portal) is what determines
those offsets:

1. Run with `--log_level trace` and a figure that already has some progress.
2. Take an action that should change exactly one stat (level up, spend some gold, or attempt an
   in-game rename if Giants supports one), and capture the before/after `Portal figure research`
   log lines for that figure's slot.
3. Diff the block hex dumps: the byte(s) that changed, and by how much/what pattern, is the real
   offset and encoding for that stat.

Once those offsets are confirmed, a follow-up plan replaces `ParseFigureStats`'s body with the
real decode logic and adds tests using the actual captured dumps as fixtures — no other code from
this plan needs to change.
