# Software Portal (Milestone 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the throwaway spike with a real, tested `SoftwarePortal` that answers the game's portal commands, hooked into the game's two portal wrappers, so Skylanders Giants gets past "Can't find the Portal of Power" and can show a (test) figure.

**Architecture:** A ReXGlue-free library `giantrecomp_portal` holds the `PortalDevice` interface, the Xbox `0B 14` framing helpers, the mode parser and `SoftwarePortal` (command state machine plus 16 figure slots). A thin `hooks/portal_hook.cpp` overrides the game's read and write wrappers (`sub_82403BB8`, `sub_82403C28`), converts frames to raw reports, and forwards to whichever `PortalDevice` is installed. Everything the game requires is taken from `docs/investigation/portal-protocol.md`.

**Tech Stack:** C++23, clang, CMake/Ninja, ReXGlue `REX_HOOK_RAW` and cvars, plain-`main` tests run through CTest.

**Spec:** `docs/superpowers/specs/2026-09-27-giantrecomp-design.md` (sections 4.2 and 4.4). Protocol facts: `docs/investigation/portal-protocol.md` and `docs/investigation/portal-api.md`.

## Global Constraints

- Windows only for v1; clang 19+, C++23.
- The repository stays MIT; Cemu and RPCS3 are references, no code is copied.
- No game assets, figure dumps or generated code are committed.
- `PortalDevice` speaks raw 32-byte reports. It knows nothing about Xbox framing or the recompiler. Only `hooks/portal_hook.cpp` depends on the game's addresses.
- The portal core (`src/portal/`) has no ReXGlue dependency, so it is unit-testable.
- `portal_mode = none` is a valid state: the game then takes its own "Can't find the Portal of Power" path.
- Valid figure data (layout, checksums, encryption) is milestone 5. This plan's only figure is an all-zero **test** figure, which the game is expected to report as a problem toy.

## Review Focus

1. Garbled or unknown command bytes, and a write frame with a wrong header, never crash or corrupt portal state; they are ignored. (Tasks 1 and 2)
2. `Q` and `W` with a block index of 64 or more, or a slot nibble of any value 0-15, never read or write out of bounds. (Task 3)
3. `PlaceFigure` and `RemoveFigure` with an out-of-range slot are rejected without changing any state. (Task 3)
4. The overlay thread (control API) and the game thread (`Read`/`Write`) using the portal at the same time. (Task 3)
5. `--portal_mode` with an unknown value falls back to no portal with a logged warning instead of crashing. (Tasks 1 and 4)

---

## File Structure

- `src/portal/portal_device.h` — `Report`, `FigureData`, size constants, the `PortalDevice` interface.
- `src/portal/xbox_frame.h/.cpp` — `0B 14` frame to and from a raw report.
- `src/portal/portal_mode.h/.cpp` — `PortalMode` and `ParsePortalMode`.
- `src/portal/software/software_portal.h/.cpp` — `SoftwarePortal`.
- `src/hooks/portal_hook.h/.cpp` — the two game hooks, the cvars, `InstallConfiguredPortal`.
- `tests/test_util.h` — `CHECK` and `Finish` for the new tests.
- `tests/portal_framing_test.cpp`, `tests/portal_mode_test.cpp`, `tests/software_portal_test.cpp`.
- Modify: `CMakeLists.txt`, `src/giantrecomp_app.h`, `README.md`, the spec.

Run every command from the repository root in an **x64 Native Tools** prompt. Build targets: `cmake --build --preset win-amd64-debug --target <name>`; run tests with `ctest --test-dir out/build/win-amd64-debug`.

---

### Task 1: Portal types, frame helpers, and mode parser

**Files:**
- Create: `src/portal/portal_device.h`, `src/portal/xbox_frame.h`, `src/portal/xbox_frame.cpp`, `src/portal/portal_mode.h`, `src/portal/portal_mode.cpp`, `tests/test_util.h`, `tests/portal_framing_test.cpp`, `tests/portal_mode_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace giantrecomp::portal {
  constexpr size_t kReportSize = 32;
  using Report = std::array<uint8_t, kReportSize>;
  constexpr size_t kBlockSize = 16, kBlockCount = 64, kFigureSize = 1024;
  using FigureData = std::array<uint8_t, kFigureSize>;
  constexpr int kMaxFigures = 16;
  class PortalDevice { public: virtual ~PortalDevice() = default;
    virtual void Write(const Report&) = 0; virtual Report Read() = 0; };

  constexpr size_t kFrameSize = 32, kFramePayload = 30;
  constexpr uint8_t kFrameHeader0 = 0x0B, kFrameHeader1 = 0x14;
  std::optional<Report> ReportFromFrame(const uint8_t* frame);   // nullopt on a wrong header
  void FrameFromReport(const Report& report, uint8_t* frame);    // writes kFrameSize bytes

  enum class PortalMode { kNone, kSoftware };
  std::optional<PortalMode> ParsePortalMode(std::string_view text);
  }
  ```
- Produces (CMake): library `giantrecomp_portal` (static, include dir `src`).

- [ ] **Step 1: Write the test helper and the failing tests**

Create `tests/test_util.h`:

```cpp
#pragma once

#include <cstdio>

inline int g_failures = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

inline int Finish(const char* name) {
  if (g_failures == 0) std::printf("all %s tests passed\n", name);
  return g_failures == 0 ? 0 : 1;
}
```

Create `tests/portal_framing_test.cpp`:

```cpp
#include <cstring>

#include "portal/xbox_frame.h"
#include "test_util.h"

using namespace giantrecomp::portal;

int main() {
  // A valid frame (0B 14 + payload) becomes a report with the payload at the front.
  uint8_t frame[kFrameSize] = {0x0B, 0x14, 0x52, 0x01, 0x02};
  auto report = ReportFromFrame(frame);
  CHECK(report.has_value());
  CHECK((*report)[0] == 0x52);
  CHECK((*report)[1] == 0x01);
  CHECK((*report)[2] == 0x02);
  CHECK((*report)[3] == 0x00);
  CHECK((*report)[30] == 0x00 && (*report)[31] == 0x00);

  // A wrong header is rejected, whichever byte is wrong.
  uint8_t bad1[kFrameSize] = {0x0B, 0x15, 0x52};
  uint8_t bad2[kFrameSize] = {0x0A, 0x14, 0x52};
  uint8_t zeros[kFrameSize] = {};
  CHECK(!ReportFromFrame(bad1).has_value());
  CHECK(!ReportFromFrame(bad2).has_value());
  CHECK(!ReportFromFrame(zeros).has_value());

  // A report becomes a frame: header, then the first 30 report bytes. The last two do not fit.
  Report r{};
  r[0] = 0x53;
  r[29] = 0xEE;
  r[30] = 0xAA;
  r[31] = 0xBB;
  uint8_t out[kFrameSize];
  std::memset(out, 0xCC, sizeof(out));
  FrameFromReport(r, out);
  CHECK(out[0] == 0x0B && out[1] == 0x14);
  CHECK(out[2] == 0x53);
  CHECK(out[31] == 0xEE);

  // Round trip: a report with 30 payload bytes survives; bytes 30 and 31 come back as zero.
  auto back = ReportFromFrame(out);
  CHECK(back.has_value());
  CHECK((*back)[0] == 0x53 && (*back)[29] == 0xEE);
  CHECK((*back)[30] == 0x00 && (*back)[31] == 0x00);

  return Finish("portal_framing");
}
```

Create `tests/portal_mode_test.cpp`:

```cpp
#include "portal/portal_mode.h"
#include "test_util.h"

using giantrecomp::portal::ParsePortalMode;
using giantrecomp::portal::PortalMode;

int main() {
  CHECK(ParsePortalMode("software") == PortalMode::kSoftware);
  CHECK(ParsePortalMode("none") == PortalMode::kNone);
  CHECK(ParsePortalMode("  Software \t") == PortalMode::kSoftware);
  CHECK(ParsePortalMode("NONE") == PortalMode::kNone);

  // Unknown values are rejected, including modes that are not implemented yet.
  CHECK(!ParsePortalMode("usb").has_value());
  CHECK(!ParsePortalMode("").has_value());
  CHECK(!ParsePortalMode("   ").has_value());
  CHECK(!ParsePortalMode("banana").has_value());
  CHECK(!ParsePortalMode("software extra").has_value());

  return Finish("portal_mode");
}
```

- [ ] **Step 2: Add the CMake targets and confirm the tests fail**

Append to `CMakeLists.txt` (after the existing `add_test(NAME xex_verify ...)`):

```cmake
# --- Portal core (no ReXGlue dependency) ------------------------------------
add_library(giantrecomp_portal STATIC
    src/portal/portal_mode.cpp
    src/portal/xbox_frame.cpp
)
target_include_directories(giantrecomp_portal PUBLIC src)
target_compile_features(giantrecomp_portal PUBLIC cxx_std_23)

foreach(portal_test portal_framing_test portal_mode_test)
    add_executable(${portal_test} tests/${portal_test}.cpp)
    target_include_directories(${portal_test} PRIVATE tests)
    target_link_libraries(${portal_test} PRIVATE giantrecomp_portal)
    add_test(NAME ${portal_test} COMMAND ${portal_test})
endforeach()
```

Run: `cmake --preset win-amd64-debug && cmake --build --preset win-amd64-debug --target portal_framing_test portal_mode_test`
Expected: FAIL with a CMake error that `src/portal/portal_mode.cpp` (and `xbox_frame.cpp`) do not exist.

- [ ] **Step 3: Write the implementation**

Create `src/portal/portal_device.h`:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace giantrecomp::portal {

// A raw portal report: what a portal sends and receives, with no console framing.
constexpr size_t kReportSize = 32;
using Report = std::array<uint8_t, kReportSize>;

// One figure's tag data: 64 blocks of 16 bytes.
constexpr size_t kBlockSize = 16;
constexpr size_t kBlockCount = 64;
constexpr size_t kFigureSize = kBlockSize * kBlockCount;
using FigureData = std::array<uint8_t, kFigureSize>;

// A portal holds up to 16 figures.
constexpr int kMaxFigures = 16;

// The game's view of a portal. SoftwarePortal implements it now; a USB portal will later.
class PortalDevice {
 public:
  virtual ~PortalDevice() = default;

  // Game to portal: one command report.
  virtual void Write(const Report& report) = 0;

  // Portal to game: the next report. A queued command reply if there is one, otherwise a status report.
  virtual Report Read() = 0;
};

}  // namespace giantrecomp::portal
```

Create `src/portal/xbox_frame.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "portal/portal_device.h"

namespace giantrecomp::portal {

// The Xbox 360 game exchanges 32-byte frames: the header 0B 14 followed by a 30-byte payload.
constexpr size_t kFrameSize = 32;
constexpr size_t kFramePayload = kFrameSize - 2;
constexpr uint8_t kFrameHeader0 = 0x0B;
constexpr uint8_t kFrameHeader1 = 0x14;

// `frame` points at kFrameSize bytes. Returns the payload as a raw report (rest zero), or nullopt
// if the header is wrong.
std::optional<Report> ReportFromFrame(const uint8_t* frame);

// Writes kFrameSize bytes to `frame`: the header, then the first kFramePayload bytes of `report`.
void FrameFromReport(const Report& report, uint8_t* frame);

}  // namespace giantrecomp::portal
```

Create `src/portal/xbox_frame.cpp`:

```cpp
#include "portal/xbox_frame.h"

#include <cstring>

namespace giantrecomp::portal {

std::optional<Report> ReportFromFrame(const uint8_t* frame) {
  if (frame[0] != kFrameHeader0 || frame[1] != kFrameHeader1) return std::nullopt;
  Report report{};
  std::memcpy(report.data(), frame + 2, kFramePayload);
  return report;
}

void FrameFromReport(const Report& report, uint8_t* frame) {
  frame[0] = kFrameHeader0;
  frame[1] = kFrameHeader1;
  std::memcpy(frame + 2, report.data(), kFramePayload);
}

}  // namespace giantrecomp::portal
```

Create `src/portal/portal_mode.h`:

```cpp
#pragma once

#include <optional>
#include <string_view>

namespace giantrecomp::portal {

enum class PortalMode { kNone, kSoftware };

// Accepts "none" or "software" (any case, surrounding whitespace ignored). Anything else, including
// modes that do not exist yet, gives nullopt.
std::optional<PortalMode> ParsePortalMode(std::string_view text);

}  // namespace giantrecomp::portal
```

Create `src/portal/portal_mode.cpp`:

```cpp
#include "portal/portal_mode.h"

#include <cctype>
#include <string>

namespace giantrecomp::portal {

std::optional<PortalMode> ParsePortalMode(std::string_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
  std::string word;
  for (char c : text.substr(begin, end - begin)) {
    word.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  if (word == "none") return PortalMode::kNone;
  if (word == "software") return PortalMode::kSoftware;
  return std::nullopt;
}

}  // namespace giantrecomp::portal
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --preset win-amd64-debug && cmake --build --preset win-amd64-debug --target portal_framing_test portal_mode_test && out\build\win-amd64-debug\portal_framing_test.exe && out\build\win-amd64-debug\portal_mode_test.exe`
Expected: `all portal_framing tests passed` and `all portal_mode tests passed`.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/portal tests/test_util.h tests/portal_framing_test.cpp tests/portal_mode_test.cpp
git commit -m "feat: portal device interface, Xbox frame helpers and mode parser"
```

---

### Task 2: SoftwarePortal command handling

**Files:**
- Create: `src/portal/software/software_portal.h`, `src/portal/software/software_portal.cpp`, `tests/software_portal_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Report`, `FigureData`, `kMaxFigures`, `kBlockSize`, `kBlockCount`, `PortalDevice` from Task 1.
- Produces:
  ```cpp
  class SoftwarePortal : public PortalDevice {
   public:
    void Write(const Report&) override;
    Report Read() override;
    bool PlaceFigure(int slot, const FigureData&);          // false if slot out of range
    bool RemoveFigure(int slot);                            // false if out of range or empty
    bool HasFigure(int slot) const;
    std::optional<FigureData> Figure(int slot) const;
  };
  ```
  Tasks 2 implements `Write`/`Read` command handling and the status report; Task 3 adds the figure behaviour tests. The whole class is written now so its header is final; figure-specific tests come in Task 3.

- [ ] **Step 1: Write the failing tests**

Create `tests/software_portal_test.cpp`:

```cpp
#include <initializer_list>

#include "portal/software/software_portal.h"
#include "test_util.h"

using namespace giantrecomp::portal;

static Report Cmd(std::initializer_list<uint8_t> bytes) {
  Report r{};
  size_t i = 0;
  for (uint8_t b : bytes) r[i++] = b;
  return r;
}

int main() {
  // Ready ('R') is answered with 52 02 1B, then idle reads are status reports.
  {
    SoftwarePortal p;
    p.Write(Cmd({'R', 0x00}));
    Report reply = p.Read();
    CHECK(reply[0] == 0x52 && reply[1] == 0x02 && reply[2] == 0x1B);
    Report status = p.Read();
    CHECK(status[0] == 0x53);
    CHECK(status[6] == 0x00);  // the active flag is clear before 'A'
  }

  // Activate ('A') echoes its argument: 41 <arg> FF 77. The status report then shows active.
  {
    SoftwarePortal p;
    p.Write(Cmd({'A', 0x00}));
    p.Write(Cmd({'A', 0x01}));
    Report a0 = p.Read();
    Report a1 = p.Read();
    CHECK(a0[0] == 0x41 && a0[1] == 0x00 && a0[2] == 0xFF && a0[3] == 0x77);
    CHECK(a1[0] == 0x41 && a1[1] == 0x01 && a1[2] == 0xFF && a1[3] == 0x77);
    Report status = p.Read();
    CHECK(status[0] == 0x53);
    CHECK(status[6] == 0x01);
  }

  // Version ('M') echoes its argument: 4D <arg> 00 19.
  {
    SoftwarePortal p;
    p.Write(Cmd({'M', 0x01}));
    Report r = p.Read();
    CHECK(r[0] == 0x4D && r[1] == 0x01 && r[2] == 0x00 && r[3] == 0x19);
  }

  // Replies come back in the order the commands were sent.
  {
    SoftwarePortal p;
    p.Write(Cmd({'R'}));
    p.Write(Cmd({'A', 0x00}));
    p.Write(Cmd({'M', 0x01}));
    CHECK(p.Read()[0] == 0x52);
    CHECK(p.Read()[0] == 0x41);
    CHECK(p.Read()[0] == 0x4D);
    CHECK(p.Read()[0] == 0x53);  // then status
  }

  // Status ('S'), LED commands ('C', 'J', 'L') and unknown bytes queue no reply and do not crash.
  {
    SoftwarePortal p;
    p.Write(Cmd({'S'}));
    p.Write(Cmd({'V'}));
    p.Write(Cmd({'C', 0xE8, 0x10, 0x00}));
    p.Write(Cmd({'J', 0x01, 0xFF, 0x00, 0x00}));
    p.Write(Cmd({'L', 0x02, 0x00, 0xFF, 0x00}));
    p.Write(Cmd({0x00}));
    p.Write(Cmd({0xFF, 0xFF, 0xFF}));
    p.Write(Cmd({'z'}));
    CHECK(p.Read()[0] == 0x53);
    CHECK(p.Read()[0] == 0x53);
  }

  // The status counter is byte 5 and increments on every status report, wrapping at 256.
  {
    SoftwarePortal p;
    uint8_t previous = p.Read()[5];
    for (int i = 0; i < 300; ++i) {
      uint8_t next = p.Read()[5];
      CHECK(next == static_cast<uint8_t>(previous + 1));
      previous = next;
    }
  }

  return Finish("software_portal");
}
```

The status report layout is `53`, four little-endian slot-state bytes at 1-4, the counter at 5 and the active flag at 6.

- [ ] **Step 2: Add the CMake target and confirm the test fails**

In `CMakeLists.txt`, add `src/portal/software/software_portal.cpp` to the `giantrecomp_portal` source list, and add `software_portal_test` to the `foreach` list (`foreach(portal_test portal_framing_test portal_mode_test software_portal_test)`).

Run: `cmake --preset win-amd64-debug && cmake --build --preset win-amd64-debug --target software_portal_test`
Expected: FAIL: `src/portal/software/software_portal.cpp` does not exist.

- [ ] **Step 3: Write the implementation**

Create `src/portal/software/software_portal.h`:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

#include "portal/portal_device.h"

namespace giantrecomp::portal {

// A portal implemented in software. Answers the game's command reports, holds up to 16 figures,
// and can be driven from another thread through PlaceFigure / RemoveFigure.
class SoftwarePortal : public PortalDevice {
 public:
  void Write(const Report& report) override;
  Report Read() override;

  // Control API. Thread-safe.
  bool PlaceFigure(int slot, const FigureData& data);  // false if `slot` is out of range
  bool RemoveFigure(int slot);                         // false if out of range or empty
  bool HasFigure(int slot) const;
  std::optional<FigureData> Figure(int slot) const;

 private:
  enum class SlotState : uint8_t { kEmpty = 0, kReady = 1, kRemoving = 2, kAdded = 3 };
  struct Slot {
    bool present = false;
    SlotState state = SlotState::kEmpty;
    int reports_left = 0;  // status reports still to show kAdded
    FigureData data{};
  };

  Report StatusReportLocked();

  mutable std::mutex mu_;
  std::deque<Report> replies_;
  std::array<Slot, kMaxFigures> slots_{};
  bool active_ = false;
  uint8_t counter_ = 0;
};

}  // namespace giantrecomp::portal
```

Create `src/portal/software/software_portal.cpp`:

```cpp
#include "portal/software/software_portal.h"

#include <algorithm>
#include <initializer_list>

namespace giantrecomp::portal {

namespace {

// A new figure shows as "added" for this many status reports before it settles to "ready".
constexpr int kAddedReports = 8;

Report MakeReport(std::initializer_list<uint8_t> bytes) {
  Report r{};
  std::copy(bytes.begin(), bytes.end(), r.begin());
  return r;
}

}  // namespace

void SoftwarePortal::Write(const Report& in) {
  std::lock_guard<std::mutex> lock(mu_);
  switch (in[0]) {
    case 'R':  // ready
      replies_.push_back(MakeReport({0x52, 0x02, 0x1B}));
      break;
    case 'A':  // activate
      active_ = true;
      for (Slot& s : slots_) {
        if (s.present) {
          s.state = SlotState::kAdded;
          s.reports_left = kAddedReports;
        }
      }
      replies_.push_back(MakeReport({0x41, in[1], 0xFF, 0x77}));
      break;
    case 'M':  // version
      replies_.push_back(MakeReport({0x4D, in[1], 0x00, 0x19}));
      break;
    case 'Q': {  // read one block of a figure
      const uint8_t slot = in[1] & 0x0F;
      const uint8_t block = in[2];
      Report out = MakeReport({0x51, slot, block});
      const Slot& s = slots_[slot];
      if (s.present && block < kBlockCount) {
        out[1] |= 0x10;
        std::copy_n(s.data.begin() + block * kBlockSize, kBlockSize, out.begin() + 3);
      }
      replies_.push_back(out);
      break;
    }
    case 'W': {  // write one block of a figure
      const uint8_t slot = in[1] & 0x0F;
      const uint8_t block = in[2];
      Report out = MakeReport({0x57, slot, block});
      Slot& s = slots_[slot];
      if (s.present && block < kBlockCount) {
        out[1] |= 0x10;
        std::copy_n(in.begin() + 3, kBlockSize, s.data.begin() + block * kBlockSize);
      }
      replies_.push_back(out);
      break;
    }
    default:
      // 'S' and 'V' (status is answered by idle reads), the LED commands 'C', 'J' and 'L', and
      // anything unknown: no reply.
      break;
  }
}

Report SoftwarePortal::Read() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!replies_.empty()) {
    Report r = replies_.front();
    replies_.pop_front();
    return r;
  }
  return StatusReportLocked();
}

Report SoftwarePortal::StatusReportLocked() {
  uint32_t states = 0;
  if (active_) {
    for (int i = 0; i < kMaxFigures; ++i) {
      states |= static_cast<uint32_t>(slots_[i].state) << (2 * i);
    }
  }
  Report r = MakeReport({0x53, static_cast<uint8_t>(states), static_cast<uint8_t>(states >> 8),
                         static_cast<uint8_t>(states >> 16), static_cast<uint8_t>(states >> 24),
                         counter_++, static_cast<uint8_t>(active_ ? 1 : 0)});
  // Advance transitions once they have been reported.
  for (Slot& s : slots_) {
    if (s.state == SlotState::kAdded) {
      if (--s.reports_left <= 0) s.state = SlotState::kReady;
    } else if (s.state == SlotState::kRemoving) {
      s.state = SlotState::kEmpty;
    }
  }
  return r;
}

bool SoftwarePortal::PlaceFigure(int slot, const FigureData& data) {
  if (slot < 0 || slot >= kMaxFigures) return false;
  std::lock_guard<std::mutex> lock(mu_);
  Slot& s = slots_[slot];
  s.present = true;
  s.data = data;
  if (active_) {
    s.state = SlotState::kAdded;
    s.reports_left = kAddedReports;
  } else {
    s.state = SlotState::kEmpty;  // shown as added once the game activates the portal
  }
  return true;
}

bool SoftwarePortal::RemoveFigure(int slot) {
  if (slot < 0 || slot >= kMaxFigures) return false;
  std::lock_guard<std::mutex> lock(mu_);
  Slot& s = slots_[slot];
  if (!s.present) return false;
  s.present = false;
  s.data = FigureData{};
  s.state = active_ ? SlotState::kRemoving : SlotState::kEmpty;
  return true;
}

bool SoftwarePortal::HasFigure(int slot) const {
  if (slot < 0 || slot >= kMaxFigures) return false;
  std::lock_guard<std::mutex> lock(mu_);
  return slots_[slot].present;
}

std::optional<FigureData> SoftwarePortal::Figure(int slot) const {
  if (slot < 0 || slot >= kMaxFigures) return std::nullopt;
  std::lock_guard<std::mutex> lock(mu_);
  if (!slots_[slot].present) return std::nullopt;
  return slots_[slot].data;
}

}  // namespace giantrecomp::portal
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset win-amd64-debug --target software_portal_test && out\build\win-amd64-debug\software_portal_test.exe`
Expected: `all software_portal tests passed`.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/portal/software tests/software_portal_test.cpp
git commit -m "feat: SoftwarePortal answers the game's portal commands"
```

---

### Task 3: SoftwarePortal figures and thread safety

**Files:**
- Modify: `tests/software_portal_test.cpp`
- (Implementation was written in Task 2; this task pins the figure behaviour with tests and fixes anything they find.)

**Interfaces:**
- Consumes: `SoftwarePortal` from Task 2.

- [ ] **Step 1: Write the failing tests**

In `tests/software_portal_test.cpp`, add `#include <atomic>` and `#include <thread>` at the top, and add these helper and test blocks before `return Finish("software_portal");`:

```cpp
  // Slot state bits in a status report: two bits per slot, slot 0 lowest, little-endian at bytes 1-4.
  auto slot_state = [](const Report& status, int slot) {
    uint32_t bits = status[1] | (status[2] << 8) | (status[3] << 16) | (uint32_t(status[4]) << 24);
    return (bits >> (2 * slot)) & 0x3u;
  };
  auto pattern = [] {
    FigureData d{};
    for (size_t i = 0; i < d.size(); ++i) d[i] = static_cast<uint8_t>(i * 7 + 1);
    return d;
  };

  // A figure placed before activation shows as empty until 'A', then "added" (3) for 8 status
  // reports, then "ready" (1).
  {
    SoftwarePortal p;
    CHECK(p.PlaceFigure(0, pattern()));
    CHECK(slot_state(p.Read(), 0) == 0);
    p.Write(Cmd({'A', 0x00}));
    p.Read();  // the 'A' reply
    for (int i = 0; i < 8; ++i) CHECK(slot_state(p.Read(), 0) == 3);
    CHECK(slot_state(p.Read(), 0) == 1);
    CHECK(slot_state(p.Read(), 0) == 1);
  }

  // A figure placed after activation goes straight to "added". Other slots stay empty.
  {
    SoftwarePortal p;
    p.Write(Cmd({'A', 0x00}));
    p.Read();
    CHECK(p.PlaceFigure(3, pattern()));
    Report s = p.Read();
    CHECK(slot_state(s, 3) == 3);
    CHECK(slot_state(s, 0) == 0);
    CHECK(slot_state(s, 15) == 0);
  }

  // Q returns the block's 16 bytes with the "present" flag; block 0 and the last block work.
  {
    SoftwarePortal p;
    FigureData d = pattern();
    p.PlaceFigure(0, d);
    for (uint8_t block : {uint8_t(0), uint8_t(1), uint8_t(0x3F)}) {
      p.Write(Cmd({'Q', 0x00, block}));
      Report r = p.Read();
      CHECK(r[0] == 0x51 && r[1] == 0x10 && r[2] == block);
      for (size_t i = 0; i < kBlockSize; ++i) CHECK(r[3 + i] == d[block * kBlockSize + i]);
    }
  }

  // The high nibble of Q's second byte is ignored; the low nibble selects the slot.
  {
    SoftwarePortal p;
    p.PlaceFigure(2, pattern());
    p.Write(Cmd({'Q', 0xF2, 0x00}));
    Report r = p.Read();
    CHECK(r[1] == 0x12);
    p.Write(Cmd({'Q', 0xF0, 0x00}));  // slot 0 is empty
    Report empty = p.Read();
    CHECK(empty[1] == 0x00 && empty[2] == 0x00 && empty[3] == 0x00);
  }

  // Q for an empty slot has no "present" flag and zero data. Block 64 and up never reads out of bounds.
  {
    SoftwarePortal p;
    p.PlaceFigure(0, pattern());
    p.Write(Cmd({'Q', 0x05, 0x02}));  // slot 5 empty
    Report r = p.Read();
    CHECK(r[1] == 0x05 && r[2] == 0x02);
    for (size_t i = 3; i < 3 + kBlockSize; ++i) CHECK(r[i] == 0);
    for (int block : {64, 65, 127, 200, 255}) {
      p.Write(Cmd({'Q', 0x00, static_cast<uint8_t>(block)}));
      Report o = p.Read();
      CHECK(o[0] == 0x51 && o[1] == 0x00 && o[2] == block);
      for (size_t i = 3; i < 3 + kBlockSize; ++i) CHECK(o[i] == 0);
    }
  }

  // W writes 16 bytes, replies 57 <flag|slot> <block>, and Q reads them back. Out-of-range blocks
  // and empty slots are ignored without touching any figure.
  {
    SoftwarePortal p;
    FigureData d = pattern();
    p.PlaceFigure(1, d);
    Report w = Cmd({'W', 0x01, 0x05});
    for (size_t i = 0; i < kBlockSize; ++i) w[3 + i] = static_cast<uint8_t>(0xA0 + i);
    p.Write(w);
    Report reply = p.Read();
    CHECK(reply[0] == 0x57 && reply[1] == 0x11 && reply[2] == 0x05);
    p.Write(Cmd({'Q', 0x01, 0x05}));
    Report q = p.Read();
    for (size_t i = 0; i < kBlockSize; ++i) CHECK(q[3 + i] == static_cast<uint8_t>(0xA0 + i));
    auto after = p.Figure(1);
    CHECK(after.has_value());
    CHECK((*after)[4 * kBlockSize] == d[4 * kBlockSize]);  // neighbouring block untouched

    Report bad = w;
    bad[2] = 64;  // out of range
    p.Write(bad);
    CHECK(p.Read()[1] == 0x01);  // no "present" flag
    Report empty_slot = w;
    empty_slot[1] = 0x07;  // slot 7 has no figure
    p.Write(empty_slot);
    CHECK(p.Read()[1] == 0x07);
    CHECK(*p.Figure(1) == *after);
  }

  // Out-of-range slots are rejected and change nothing.
  {
    SoftwarePortal p;
    CHECK(!p.PlaceFigure(-1, pattern()));
    CHECK(!p.PlaceFigure(16, pattern()));
    CHECK(!p.PlaceFigure(1000, pattern()));
    CHECK(!p.RemoveFigure(-1));
    CHECK(!p.RemoveFigure(16));
    CHECK(!p.HasFigure(-1) && !p.HasFigure(16));
    CHECK(!p.Figure(16).has_value());
    for (int i = 0; i < kMaxFigures; ++i) CHECK(!p.HasFigure(i));
  }

  // RemoveFigure: false for an empty slot; when active the slot shows "removing" (2) once, then empty.
  {
    SoftwarePortal p;
    CHECK(!p.RemoveFigure(0));
    p.PlaceFigure(0, pattern());
    p.Write(Cmd({'A', 0x00}));
    p.Read();
    for (int i = 0; i < 10; ++i) p.Read();  // settle to ready
    CHECK(p.HasFigure(0));
    CHECK(p.RemoveFigure(0));
    CHECK(!p.HasFigure(0));
    CHECK(!p.RemoveFigure(0));
    CHECK(slot_state(p.Read(), 0) == 2);
    CHECK(slot_state(p.Read(), 0) == 0);
  }

  // Slot 3's state uses bits 6-7 of the status word.
  {
    SoftwarePortal p;
    p.Write(Cmd({'A', 0x00}));
    p.Read();
    p.PlaceFigure(3, pattern());
    Report s = p.Read();
    CHECK((s[1] & 0xC0) == 0xC0);
  }

  // The overlay thread and the game thread use the portal at the same time.
  {
    SoftwarePortal p;
    std::atomic<bool> stop{false};
    std::thread overlay([&] {
      FigureData d = pattern();
      while (!stop) {
        p.PlaceFigure(0, d);
        p.PlaceFigure(5, d);
        p.RemoveFigure(0);
        p.RemoveFigure(5);
        (void)p.HasFigure(0);
        (void)p.Figure(5);
      }
    });
    std::thread game([&] {
      p.Write(Cmd({'R'}));
      p.Write(Cmd({'A', 0x01}));
      while (!stop) {
        p.Write(Cmd({'Q', 0x00, 0x00}));
        p.Write(Cmd({'W', 0x05, 0x01}));
        (void)p.Read();
        (void)p.Read();
      }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop = true;
    overlay.join();
    game.join();
    CHECK(p.Read()[0] != 0x00);  // still answers coherently
  }
```

Also add `#include <chrono>` at the top of the file.

- [ ] **Step 2: Run the tests**

Run: `cmake --build --preset win-amd64-debug --target software_portal_test && out\build\win-amd64-debug\software_portal_test.exe`
Expected: the implementation from Task 2 already satisfies these behaviours, so this run should print `all software_portal tests passed`. If any `CHECK` fails, the output shows `FAIL <file>:<line>`; fix the implementation in `src/portal/software/software_portal.cpp`, not the test, and re-run. A test that passes on its first run only counts if you also see it fail once: temporarily change `kAddedReports` from 8 to 7 in `software_portal.cpp`, re-run, confirm `FAIL` on the "added for 8 status reports" check, then restore 8.

- [ ] **Step 3: Run the whole suite**

Run: `ctest --test-dir out/build/win-amd64-debug`
Expected: `100% tests passed out of 4` (xex_verify, portal_framing_test, portal_mode_test, software_portal_test).

- [ ] **Step 4: Commit**

```bash
git add tests/software_portal_test.cpp src/portal/software/software_portal.cpp
git commit -m "test: pin SoftwarePortal figure, bounds and threading behaviour"
```

---

### Task 4: Game hooks, cvars, app wiring, and end-to-end check

**Files:**
- Create: `src/hooks/portal_hook.h`, `src/hooks/portal_hook.cpp`
- Modify: `CMakeLists.txt`, `src/giantrecomp_app.h`, `README.md`, `docs/superpowers/specs/2026-09-27-giantrecomp-design.md`

**Interfaces:**
- Consumes: `PortalDevice`, `SoftwarePortal`, `ReportFromFrame`, `FrameFromReport`, `kFrameSize`, `ParsePortalMode`, `PortalMode` from Tasks 1-2.
- Produces:
  ```cpp
  namespace giantrecomp {
  // Creates the portal chosen by the portal_mode cvar and routes the game's portal I/O to it.
  // The portal lives for the whole process: game threads may still call it while the app shuts down.
  void InstallConfiguredPortal();
  }
  ```
  and the cvars `portal_mode` (string, default `software`) and `portal_test_figure` (bool, default `false`).

- [ ] **Step 1: Write the hook file**

Create `src/hooks/portal_hook.h`:

```cpp
#pragma once

namespace giantrecomp {

// Creates the portal selected by the `portal_mode` cvar and routes the game's portal reads and
// writes to it. With no portal (mode `none` or an unknown value) the game keeps its own path and
// shows "Can't find the Portal of Power". The portal lives for the whole process, because game
// threads may still call into it while the app shuts down.
void InstallConfiguredPortal();

}  // namespace giantrecomp
```

Create `src/hooks/portal_hook.cpp`:

```cpp
#include "hooks/portal_hook.h"

#include <atomic>
#include <cstdint>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "portal/portal_device.h"
#include "portal/portal_mode.h"
#include "portal/software/software_portal.h"
#include "portal/xbox_frame.h"

REXCVAR_DEFINE_STRING(portal_mode, "software", "Portal",
                      "Portal backend: 'software' or 'none'");
REXCVAR_DEFINE_BOOL(portal_test_figure, false, "Portal",
                    "Development: put an all-zero figure on the portal (the game reports it as a "
                    "problem toy)");

namespace {

std::atomic<giantrecomp::portal::PortalDevice*> g_portal{nullptr};

}  // namespace

namespace giantrecomp {

void InstallConfiguredPortal() {
  const std::string text = REXCVAR_GET(portal_mode);
  const auto mode = portal::ParsePortalMode(text);
  if (!mode) {
    REXLOG_WARN("Unknown portal_mode '{}'; running with no portal (use 'software' or 'none')", text);
    return;
  }
  if (*mode == portal::PortalMode::kNone) {
    REXLOG_INFO("Portal: none");
    return;
  }
  auto* software = new portal::SoftwarePortal();  // intentionally never freed, see the header
  if (REXCVAR_GET(portal_test_figure)) {
    software->PlaceFigure(0, portal::FigureData{});
    REXLOG_WARN("Portal: placed an all-zero test figure in slot 0");
  }
  g_portal.store(software);
  REXLOG_INFO("Portal: software");
}

}  // namespace giantrecomp

// The game reads and writes its portal through two small recompiled wrappers (see
// docs/investigation/portal-api.md and portal-protocol.md). Replace them when a portal is installed.
REX_EXTERN(__imp__sub_82403B18);  // initializer: sets the "portal API available" flag
REX_EXTERN(__imp__sub_82403BB8);  // read:  r3 = &bytes_read, r4 = &buffer_size, r5 = buffer
REX_EXTERN(__imp__sub_82403C28);  // write: r4 = frame buffer

REX_HOOK_RAW(sub_82403BB8) {
  using namespace giantrecomp::portal;
  PortalDevice* portal = g_portal.load();
  if (!portal) {
    __imp__sub_82403BB8(ctx, base);
    return;
  }
  const uint32_t bytes_read_ptr = ctx.r3.u32;
  const uint32_t size_ptr = ctx.r4.u32;
  const uint32_t buffer_ptr = ctx.r5.u32;

  // The game passes its buffer size (0x20) through a pointer, big-endian.
  const uint8_t* size = base + size_ptr;
  const uint32_t buffer_size = (uint32_t(size[0]) << 24) | (uint32_t(size[1]) << 16) |
                               (uint32_t(size[2]) << 8) | uint32_t(size[3]);
  if (buffer_size < kFrameSize) {
    ctx.r3.u64 = 0;  // buffer too small for a frame: report failure
    return;
  }

  PPCContext init_ctx = ctx;  // the initializer clobbers registers; run it on a copy
  __imp__sub_82403B18(init_ctx, base);

  FrameFromReport(portal->Read(), base + buffer_ptr);
  uint8_t* bytes_read = base + bytes_read_ptr;  // the game expects the byte count here, big-endian
  bytes_read[0] = 0;
  bytes_read[1] = 0;
  bytes_read[2] = 0;
  bytes_read[3] = static_cast<uint8_t>(kFrameSize);
  ctx.r3.u64 = 1;
}

REX_HOOK_RAW(sub_82403C28) {
  using namespace giantrecomp::portal;
  PortalDevice* portal = g_portal.load();
  if (!portal) {
    __imp__sub_82403C28(ctx, base);
    return;
  }
  PPCContext init_ctx = ctx;
  __imp__sub_82403B18(init_ctx, base);

  if (auto report = ReportFromFrame(base + ctx.r4.u32)) portal->Write(*report);
  ctx.r3.u64 = 1;
}
```

- [ ] **Step 2: Wire it into CMake and the app**

In `CMakeLists.txt`, add `src/hooks/portal_hook.cpp` to `GIANTRECOMP_SOURCES`, and link the exe to the portal library by adding this after the existing `target_link_libraries(giantrecomp PRIVATE giantrecomp_verify)` line:

```cmake
target_link_libraries(giantrecomp PRIVATE giantrecomp_portal)
```

In `src/giantrecomp_app.h`, add `#include "hooks/portal_hook.h"` next to the other project includes, remove the commented-out `// void OnPostSetup() override {}` placeholder line, and add this member next to `OnPreSetup`:

```cpp
  void OnPostSetup() override { giantrecomp::InstallConfiguredPortal(); }
```

- [ ] **Step 3: Build**

Run: `cmake --preset win-amd64-debug && cmake --build --preset win-amd64-debug`
Expected: builds and links `giantrecomp.exe`; no unresolved symbols. If the build reports a duplicate or missing `REXCVAR_*` symbol, the cvar macros belong in exactly one translation unit (this one).

- [ ] **Step 4: Run all tests**

Run: `ctest --test-dir out/build/win-amd64-debug`
Expected: `100% tests passed out of 4`.

- [ ] **Step 5: End-to-end check with the real game (needs a person at the controller)**

Ask the human to press A at the title screen in each run below, and to report what they see. Each run: `out\build\win-amd64-debug\giantrecomp.exe --game_data_root rom <options> --log_file logs\m4-<name>.log`.

1. **Default (software portal), no options.** Expected: after A at the title, no "Can't find the Portal of Power"; the game reaches CHOOSE PLAY. Log contains `Portal: software`.
2. **`--portal_mode none`.** Expected: the original path is intact: pressing A shows "Can't find the Portal of Power". Log contains `Portal: none`.
3. **`--portal_mode banana`.** Expected: same as `none`, with a warning in the log: `Unknown portal_mode 'banana'`.
4. **`--portal_test_figure`.** Expected: the game reads the figure (log shows the run does not crash) and, in Story mode, reports a problem toy ("A toy on the Portal of Power has a problem"). Log contains `placed an all-zero test figure`.

For every run also check the log for `FATAL`; a new `Call to invalid or unregistered function` fatal is a recompile gap, fixed by adding a `[functions]` entry as in `docs/investigation/boot-issues.md`. Record anything unexpected in `docs/investigation/portal-protocol.md`.

- [ ] **Step 6: Update the docs**

In `README.md`, replace the sentence that says the game "stops" with the portal message so it reads that the software portal lets the game through, and add under "Building" step 6 a short **Portal options** list:

```
   Options: `--portal_mode software` (default) or `--portal_mode none`; `--portal_test_figure` puts an all-zero test figure on the portal, which the game reports as a problem toy (real figure data comes in a later milestone).
```

In `docs/superpowers/specs/2026-09-27-giantrecomp-design.md` section 4.4, change the `portal_mode` line to default `software` and add a `portal_test_figure` line (bool, default `false`, development only). In section 5, change milestone 4 to: "**Minimal SoftwarePortal.** The game accepts the portal (handshake, status polling, LEDs) and reads a figure's 64 blocks. A test figure with zeroed data is reported as a problem toy; valid figure data is milestone 5."

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/hooks src/giantrecomp_app.h README.md docs/superpowers/specs/2026-09-27-giantrecomp-design.md docs/investigation/portal-protocol.md
git commit -m "feat: route the game's portal I/O to SoftwarePortal (portal_mode, portal_test_figure)"
```
