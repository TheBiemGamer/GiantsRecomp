# Real USB Portal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let `portal_mode = "usb"` route the game's portal reads/writes to a real, physically
plugged-in Portal of Power over USB HID, via `hidapi`, with no Zadig/driver replacement.

**Architecture:** A new `UsbPortal : PortalDevice` (matching the existing `SoftwarePortal`'s
interface exactly) opens the device with `hidapi` and relays raw bytes. No protocol logic is
reimplemented — the real portal's firmware already speaks the same command bytes the game itself
uses (`docs/investigation/portal-protocol.md`); the only real work is a 32-byte (`Report`) ↔
64-byte (real device report) size adapter, plus wiring a third `portal_mode` value through the
existing dispatch in `src/hooks/portal_hook.cpp`.

**Tech Stack:** C++23, `hidapi` (new submodule, vendored under `thirdparty/hidapi`), the project's
existing minimal test harness (`tests/test_util.h`, `CHECK`/`Finish`, no external test framework).

**Spec:** `docs/superpowers/specs/2026-09-27-usb-portal-design.md`

## Global Constraints

- C++23 (`CMAKE_CXX_STANDARD 23`, already project-wide).
- No Zadig/WinUSB driver replacement — `hidapi` must use its native/HID backend, not libusb, on
  Windows (spec §4).
- `hidapi` vendored as a git submodule under `thirdparty/hidapi`, consistent with how
  `thirdparty/rexglue-sdk` is already vendored (spec §4).
- Device whitelist restricted to exactly `1430:0150` (Wii U) and `1430:1F17` (Xbox 360) — no
  broader auto-detection (spec §3).
- No hot-plug, no new error UI, no LED-control UI, no real-tag-write beyond passthrough (spec,
  Non-goals).
- Facts taken from Cemu's `nsyshid/Skylander.cpp` are re-derived facts (command letters, report
  size), never copied code — matches this project's existing MIT-clean approach (already used for
  the figure-creation milestone).
- `giantrecomp_portal` (the existing dependency-free static library, `CMakeLists.txt`) stays free
  of `hidapi`/OS dependencies so its test executables keep building without hardware or drivers on
  every CI platform (Linux/Mac/Windows workflows already exist under `.github/workflows/`). Only
  the pure size-adapter goes in that library; the real `hidapi`-linked class lives directly in the
  `giantrecomp` executable's own sources, matching how `src/hooks/portal_hook.cpp` (which also has
  real dependencies) is already structured outside that library.

## Review Focus

- **No device attached / wrong VID-PID at startup.** `InstallConfiguredPortal` must fall back to
  the same behavior as `portal_mode = "none"` (game shows its own "Can't find the Portal of
  Power"), not crash or leave a half-installed portal. Covered by Task 5's manual run.
- **Device unplugged mid-session.** `hid_write`/`hid_read` failing after a successful open must
  not crash the read/write hook path; `Read()` must return a zeroed `Report` (an idle/no-reply
  shape the game already tolerates per `docs/investigation/portal-protocol.md`). Handled explicitly
  in Task 4's `UsbPortal::Read()`; not independently unit-tested since it needs real hardware to
  exercise the unplug path, called out here so a reviewer knows it was a deliberate choice, not an
  oversight.
- **Partial/short device report.** If `hid_read` returns fewer than 32 bytes, the remaining
  `Report` bytes must be zero-filled, not left uninitialized or read out of bounds. Covered by
  Task 3's `DecodeInputReport` unit test.
- **Off-by-one from hidapi's mandatory report-ID prefix.** `hid_write` always requires a leading
  report-ID byte before the 64 bytes of real report data, even for a device with a single,
  unnumbered report — an easy place to shift every command byte by one and silently break the
  whole feature. Covered by Task 3's `EncodeOutputReport` unit test, which asserts the exact byte
  offsets including the prefix.
- **Existing `portal_mode_test.cpp` regression.** That test currently asserts `ParsePortalMode
  ("usb")` returns `nullopt`, with a comment explicitly calling out "usb" as an example of a mode
  "not implemented yet." Adding `PortalMode::kUsb` without updating that assertion would leave a
  real, permanently-failing test in the suite. Covered by Task 1.

---

### Task 1: `PortalMode::kUsb`

**Files:**
- Modify: `src/portal/portal_mode.h`
- Modify: `src/portal/portal_mode.cpp`
- Modify: `tests/portal_mode_test.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `giantrecomp::portal::PortalMode::kUsb`, and `ParsePortalMode("usb")` returning it.
  Later tasks (5) switch on this value.

- [ ] **Step 1: Update the failing test first**

Edit `tests/portal_mode_test.cpp`, replacing:

```cpp
  // Unknown values are rejected, including modes that are not implemented yet.
  CHECK(!ParsePortalMode("usb").has_value());
  CHECK(!ParsePortalMode("").has_value());
```

with:

```cpp
  CHECK(ParsePortalMode("usb") == PortalMode::kUsb);

  // Unknown values are rejected.
  CHECK(!ParsePortalMode("").has_value());
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset win-amd64-debug --target portal_mode_test && ctest --test-dir out/build/win-amd64-debug -R portal_mode_test`
Expected: build succeeds (the test file compiles against the not-yet-existing `kUsb` only after
Step 3), so first confirm the *old* binary still fails the *new* assertion — run the already-built
`portal_mode_test.exe` once before rebuilding if you want to see the literal failure text; otherwise
proceed straight to Step 3, since the enum doesn't exist yet and this won't compile at all, which is
itself the "fails" signal for a TDD step at the type level.

- [ ] **Step 3: Add `kUsb` to the enum and parser**

Edit `src/portal/portal_mode.h`:

```cpp
enum class PortalMode { kNone, kSoftware, kUsb };

// Accepts "none", "software", or "usb" (any case, surrounding whitespace ignored). Anything else,
// including modes that do not exist, gives nullopt.
std::optional<PortalMode> ParsePortalMode(std::string_view text);
```

Edit `src/portal/portal_mode.cpp`, adding one line after the `"software"` check:

```cpp
  if (word == "none") return PortalMode::kNone;
  if (word == "software") return PortalMode::kSoftware;
  if (word == "usb") return PortalMode::kUsb;
  return std::nullopt;
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset win-amd64-debug --target portal_mode_test && ctest --test-dir out/build/win-amd64-debug -R portal_mode_test`
Expected: PASS (`all portal_mode tests passed`)

- [ ] **Step 5: Commit**

```bash
git add src/portal/portal_mode.h src/portal/portal_mode.cpp tests/portal_mode_test.cpp
git commit -m "feat: add PortalMode::kUsb"
```

---

### Task 2: Vendor `hidapi`

**Files:**
- Modify: `.gitmodules`
- Create: `thirdparty/hidapi` (git submodule)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: CMake target `hidapi::hidapi`, linkable by later tasks.

- [ ] **Step 1: Add the submodule**

```bash
git submodule add https://github.com/libusb/hidapi.git thirdparty/hidapi
```

This appends to `.gitmodules`:

```
[submodule "thirdparty/hidapi"]
	path = thirdparty/hidapi
	url = https://github.com/libusb/hidapi.git
```

- [ ] **Step 2: Wire it into `CMakeLists.txt`**

Add this block to `CMakeLists.txt`, right after the existing `REXSDK_DIR` block (after line 16,
before `include(generated/rexglue.cmake)`):

```cmake
# hidapi provides USB HID access for the real portal backend (portal_mode = "usb"). Its own
# CMakeLists picks the right per-OS backend automatically -- on Windows this is the native HID
# API, not libusb, so no WinUSB/Zadig driver replacement is needed.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/hidapi/CMakeLists.txt")
    message(FATAL_ERROR "thirdparty/hidapi is missing. Run: git submodule update --init --recursive")
endif()
set(HIDAPI_WITH_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/hidapi)
```

- [ ] **Step 3: Verify the configure step succeeds and nothing else regresses**

Run: `cmake --preset win-amd64-debug`
Expected: configure succeeds, no errors, `hidapi::hidapi` target available (no direct way to print
targets from the CLI here; Task 4 links against it as the real proof).

Run: `cmake --build --preset win-amd64-debug && ctest --test-dir out/build/win-amd64-debug`
Expected: everything that built and passed before (including Task 1's `portal_mode_test`) still
builds and passes — this step only adds a new submodule and a new `add_subdirectory`, nothing in
the existing target graph changes yet.

- [ ] **Step 4: Commit**

```bash
git add .gitmodules thirdparty/hidapi CMakeLists.txt
git commit -m "build: vendor hidapi for the real USB portal backend"
```

---

### Task 3: 32↔64-byte report size adapter (pure, unit-tested)

**Files:**
- Create: `src/portal/usb/usb_report_codec.h`
- Create: `src/portal/usb/usb_report_codec.cpp`
- Create: `tests/usb_report_codec_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `giantrecomp::portal::Report`, `kReportSize` (`src/portal/portal_device.h`).
- Produces: `giantrecomp::portal::kDeviceReportSize` (64), `kDeviceWriteBufferSize` (65),
  `EncodeOutputReport(const Report&) -> std::array<uint8_t, kDeviceWriteBufferSize>`,
  `DecodeInputReport(const uint8_t* data, size_t length) -> Report`. Task 4's `UsbPortal` calls
  both.

- [ ] **Step 1: Write the failing test**

Create `tests/usb_report_codec_test.cpp`:

```cpp
#include <cstring>

#include "portal/usb/usb_report_codec.h"
#include "test_util.h"

using namespace giantrecomp::portal;

int main() {
  // Encode: byte 0 is the mandatory HID report ID (0), then the 32 report bytes, then 32 bytes of
  // zero padding out to the device's real 64-byte report size.
  Report r{};
  r[0] = 0x51;
  r[1] = 0x02;
  r[31] = 0xEE;
  auto buf = EncodeOutputReport(r);
  CHECK(buf.size() == kDeviceWriteBufferSize);
  CHECK(buf[0] == 0x00);   // report ID
  CHECK(buf[1] == 0x51);   // report byte 0
  CHECK(buf[2] == 0x02);   // report byte 1
  CHECK(buf[32] == 0xEE);  // report byte 31 (last), at offset 1 (ID) + 31
  for (size_t i = 33; i < buf.size(); ++i) CHECK(buf[i] == 0x00);

  // Decode: a full 64-byte device report truncates to the first 32 bytes; anything past byte 32
  // is dropped, not carried into the 32-byte Report.
  uint8_t device_report[kDeviceReportSize] = {};
  device_report[0] = 0x53;
  device_report[40] = 0xAB;
  Report decoded = DecodeInputReport(device_report, sizeof(device_report));
  CHECK(decoded[0] == 0x53);
  CHECK(decoded[31] == 0x00);

  // Decode: a short read (fewer than 32 bytes available) zero-fills the rest of the Report.
  uint8_t short_report[5] = {0x52, 0x01, 0x02, 0x03, 0x04};
  Report short_decoded = DecodeInputReport(short_report, sizeof(short_report));
  CHECK(short_decoded[0] == 0x52);
  CHECK(short_decoded[4] == 0x04);
  CHECK(short_decoded[5] == 0x00);
  CHECK(short_decoded[31] == 0x00);

  // Decode: null/zero-length input is an all-zero Report, not a crash.
  Report empty_decoded = DecodeInputReport(nullptr, 0);
  for (uint8_t b : empty_decoded) CHECK(b == 0x00);

  return Finish("usb_report_codec");
}
```

- [ ] **Step 2: Add build wiring so the test can fail to compile (and then run)**

Edit `CMakeLists.txt`: add `src/portal/usb/usb_report_codec.cpp` to the `giantrecomp_portal`
sources list:

```cmake
add_library(giantrecomp_portal STATIC
    src/portal/figure_catalog.cpp
    src/portal/figure_file.cpp
    src/portal/portal_mode.cpp
    src/portal/xbox_frame.cpp
    src/portal/software/software_portal.cpp
    src/portal/usb/usb_report_codec.cpp
)
```

Add `usb_report_codec_test` to the test `foreach`:

```cmake
foreach(portal_test portal_framing_test portal_mode_test software_portal_test figure_file_test figure_catalog_test usb_report_codec_test)
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --preset win-amd64-debug && cmake --build --preset win-amd64-debug --target usb_report_codec_test`
Expected: FAIL to compile — `usb_report_codec.h` does not exist yet.

- [ ] **Step 4: Write the implementation**

Create `src/portal/usb/usb_report_codec.h`:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "portal/portal_device.h"

namespace giantrecomp::portal {

// The real portal's USB HID reports are 64 bytes; PortalDevice::Report is 32 (matching the
// Xbox 360 console-side buffer the game itself uses -- see xbox_frame.h). The command protocol is
// identical either way (docs/investigation/portal-protocol.md); only the size differs.
constexpr size_t kDeviceReportSize = 64;

// hid_write() always requires a leading HID report-ID byte before the report data, even for a
// device with a single, unnumbered report (ID 0) -- see hidapi's hid_write() documentation.
constexpr size_t kDeviceWriteBufferSize = kDeviceReportSize + 1;

// Builds the buffer to pass to hid_write(): byte 0 is the report ID (0), followed by `report`
// zero-padded from 32 to 64 bytes.
std::array<uint8_t, kDeviceWriteBufferSize> EncodeOutputReport(const Report& report);

// Converts a device input report (as returned by hid_read(), with no report-ID prefix for a
// single-report device) back to a 32-byte Report, truncating anything past the first kReportSize
// bytes. `data` may be null only if `length` is 0. Bytes beyond `length` (if length < kReportSize)
// are zero.
Report DecodeInputReport(const uint8_t* data, size_t length);

}  // namespace giantrecomp::portal
```

Create `src/portal/usb/usb_report_codec.cpp`:

```cpp
#include "portal/usb/usb_report_codec.h"

#include <algorithm>
#include <cstring>

namespace giantrecomp::portal {

std::array<uint8_t, kDeviceWriteBufferSize> EncodeOutputReport(const Report& report) {
  std::array<uint8_t, kDeviceWriteBufferSize> buffer{};  // buffer[0] = report ID 0, rest zero
  std::copy(report.begin(), report.end(), buffer.begin() + 1);
  return buffer;
}

Report DecodeInputReport(const uint8_t* data, size_t length) {
  Report report{};
  const size_t to_copy = std::min(length, report.size());
  if (data != nullptr && to_copy > 0) {
    std::memcpy(report.data(), data, to_copy);
  }
  return report;
}

}  // namespace giantrecomp::portal
```

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build --preset win-amd64-debug --target usb_report_codec_test && ctest --test-dir out/build/win-amd64-debug -R usb_report_codec_test`
Expected: PASS (`all usb_report_codec tests passed`)

- [ ] **Step 6: Run the full test suite to confirm no regressions**

Run: `ctest --test-dir out/build/win-amd64-debug`
Expected: all tests pass, including the ones from Tasks 1 and 2.

- [ ] **Step 7: Commit**

```bash
git add src/portal/usb/usb_report_codec.h src/portal/usb/usb_report_codec.cpp tests/usb_report_codec_test.cpp CMakeLists.txt
git commit -m "feat: 32/64-byte report size adapter for the real USB portal"
```

---

### Task 4: `UsbPortal`

**Files:**
- Create: `src/portal/usb/usb_portal.h`
- Create: `src/portal/usb/usb_portal.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `giantrecomp::portal::PortalDevice` (`src/portal/portal_device.h`),
  `EncodeOutputReport`/`DecodeInputReport`/`kDeviceReportSize` (Task 3), `hid_init`/`hid_open`/
  `hid_close`/`hid_write`/`hid_read_timeout` (`hidapi.h`).
- Produces: `giantrecomp::portal::UsbPortal`, with `bool IsOpen() const`, constructible with no
  arguments. Task 5 constructs one and installs it as the active `PortalDevice` when `IsOpen()`.

No automated test for this task (spec §6: side-effecting real I/O, no unit-testable behavior
without real hardware attached — the one piece of logic worth testing in isolation, the byte
size-adapter, is already covered by Task 3). This task's deliverable is that it compiles and links
cleanly; Task 5 and Task 6 exercise it for real.

- [ ] **Step 1: Write the class**

Create `src/portal/usb/usb_portal.h`:

```cpp
#pragma once

#include <cstdint>
#include <utility>

#include <hidapi.h>

#include "portal/portal_device.h"

namespace giantrecomp::portal {

// A small set of VID/PID pairs known to be a real Skylanders Portal of Power, matching Cemu's own
// nsyshid whitelist for this device family (read for this fact only, not copied code).
inline constexpr std::pair<uint16_t, uint16_t> kKnownPortals[] = {
    {0x1430, 0x0150},  // Wii U Skylanders portal
    {0x1430, 0x1F17},  // Xbox 360 Skylanders portal
};

// A real, physical Portal of Power, opened over USB HID via hidapi. Relays the game's 32-byte
// reports to and from the device's real 64-byte reports (see usb_report_codec.h) -- the command
// protocol itself needs no translation (docs/investigation/portal-protocol.md).
class UsbPortal final : public PortalDevice {
 public:
  // Opens the first connected device matching kKnownPortals, in the order listed. Check IsOpen()
  // before installing this as the active portal -- construction never throws or logs; the caller
  // decides what to do if no matching device was found.
  UsbPortal();
  ~UsbPortal() override;

  UsbPortal(const UsbPortal&) = delete;
  UsbPortal& operator=(const UsbPortal&) = delete;

  bool IsOpen() const { return device_ != nullptr; }

  void Write(const Report& report) override;
  Report Read() override;

 private:
  hid_device* device_ = nullptr;
};

}  // namespace giantrecomp::portal
```

Create `src/portal/usb/usb_portal.cpp`:

```cpp
#include "portal/usb/usb_portal.h"

#include "portal/usb/usb_report_codec.h"

namespace giantrecomp::portal {

UsbPortal::UsbPortal() {
  if (hid_init() != 0) return;
  for (const auto& [vendor_id, product_id] : kKnownPortals) {
    device_ = hid_open(vendor_id, product_id, nullptr);
    if (device_ != nullptr) break;
  }
  // Deliberately never call hid_exit(): it finalizes the whole hidapi library, not just this
  // device, and this portal lives for the whole process (see portal_hook.cpp's InstallConfiguredPortal,
  // which never frees its SoftwarePortal either, for the same reason). Process exit cleans this up.
}

UsbPortal::~UsbPortal() {
  if (device_ != nullptr) hid_close(device_);
}

void UsbPortal::Write(const Report& report) {
  if (device_ == nullptr) return;
  const auto buffer = EncodeOutputReport(report);
  hid_write(device_, buffer.data(), buffer.size());
}

Report UsbPortal::Read() {
  if (device_ == nullptr) return Report{};
  uint8_t buffer[kDeviceReportSize] = {};
  // A short timeout keeps this from blocking the game's polling thread indefinitely if the device
  // stops responding; an all-zero Report on timeout/failure is a shape the game already tolerates
  // (see docs/investigation/portal-protocol.md's status-frame/idle-read notes).
  const int bytes_read = hid_read_timeout(device_, buffer, sizeof(buffer), 50);
  if (bytes_read <= 0) return Report{};
  return DecodeInputReport(buffer, static_cast<size_t>(bytes_read));
}

}  // namespace giantrecomp::portal
```

- [ ] **Step 2: Wire it into the executable (not the dependency-free library)**

Edit `CMakeLists.txt`: add the new file to `GIANTRECOMP_SOURCES`:

```cmake
set(GIANTRECOMP_SOURCES
    src/main.cpp
    src/game/kernel_stubs.cpp
    src/hooks/portal_hook.cpp
    src/localization.cpp
    src/overlay/portal_overlay_dialog.cpp
    src/portal/usb/usb_portal.cpp
)
```

Link `hidapi::hidapi` privately on the `giantrecomp` target (edit the existing
`target_link_libraries(giantrecomp ...)` line):

```cmake
target_link_libraries(giantrecomp PRIVATE giantrecomp_verify giantrecomp_portal hidapi::hidapi)
```

- [ ] **Step 3: Build to verify it compiles and links**

Run: `cmake --preset win-amd64-debug && cmake --build --preset win-amd64-debug`
Expected: full project builds with no errors (this also requires `rom/default.xex` and generated
code to already be present from prior setup, per the project's existing build instructions — this
is unchanged by this plan).

Run: `ctest --test-dir out/build/win-amd64-debug`
Expected: all tests still pass — this task adds no new automated test.

- [ ] **Step 4: Commit**

```bash
git add src/portal/usb/usb_portal.h src/portal/usb/usb_portal.cpp CMakeLists.txt
git commit -m "feat: add UsbPortal (real hardware not yet wired into portal_mode dispatch)"
```

---

### Task 5: Wire `usb` into `InstallConfiguredPortal`

**Files:**
- Modify: `src/hooks/portal_hook.cpp`

**Interfaces:**
- Consumes: `giantrecomp::portal::UsbPortal` (Task 4), `PortalMode::kUsb` (Task 1), the existing
  `g_portal`/`g_software_portal` globals and `PortalDevice*` machinery already in this file.
- Produces: `portal_mode = "usb"` end-to-end behavior. Nothing later in this plan consumes new
  symbols from this task — it is the integration point.

- [ ] **Step 1: Add the include and the cvar description update**

Edit `src/hooks/portal_hook.cpp`, add the include near the other `portal/` includes:

```cpp
#include "portal/usb/usb_portal.h"
```

Update the `portal_mode` cvar's description:

```cpp
REXCVAR_DEFINE_STRING(portal_mode, "software", "Portal",
                      "Portal backend: 'software', 'usb', or 'none'");
```

- [ ] **Step 2: Add the `kUsb` branch in `InstallConfiguredPortal`**

This function currently (see `src/hooks/portal_hook.cpp`) parses the mode, returns early for
unknown values and for `kNone`, then unconditionally proceeds as if `kSoftware`. Insert a new
branch for `kUsb` between the `kNone` check and the unconditional `SoftwarePortal` creation:

```cpp
  if (*mode == portal::PortalMode::kNone) {
    REXLOG_INFO("Portal: none");
    return;
  }
  if (*mode == portal::PortalMode::kUsb) {
    auto* usb = new portal::UsbPortal();  // intentionally never freed, matching the software path
    if (!usb->IsOpen()) {
      REXLOG_WARN("Portal: no USB portal found (checked known Skylanders portal VID/PIDs); "
                  "running with no portal");
      delete usb;
      return;
    }
    // g_software_portal is intentionally left null here: it is a SoftwarePortal-only status
    // handle (used by GetSoftwarePortal() for the figure-picker overlay), and there is no
    // software portal active in this mode.
    g_portal.store(usb);
    REXLOG_INFO("Portal: usb");
    return;
  }

  auto* software = new portal::SoftwarePortal();  // intentionally never freed, see the header
```

Also delete the now-resolved `TODO(milestone 7)` comment a few lines below (the one that says
"when a USB PortalDevice can also be installed here, clear g_software_portal in that branch") —
this task is that milestone; the comment's job is done and `g_software_portal` is never set by the
new branch in the first place, so there is nothing left to clear.

- [ ] **Step 3: Build**

Run: `cmake --build --preset win-amd64-debug`
Expected: builds cleanly.

- [ ] **Step 4: Manual smoke test — no device attached (or with the Wii U portal unplugged)**

Run: `out\build\win-amd64-debug\giantrecomp.exe --portal_mode usb`
Expected: the log (`out\build\win-amd64-debug\logs\giantsrecomp_*.log`, newest file) contains
`Portal: no USB portal found...`, and the game reaches its title screen exactly like `portal_mode
= "none"` does — press A, and it should show "Can't find the Portal of Power" once you reach the
point that check happens, exactly as today with `--portal_mode none`. No crash, no `FATAL` line.

- [ ] **Step 5: Run the full test suite to confirm no regressions**

Run: `ctest --test-dir out/build/win-amd64-debug`
Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/hooks/portal_hook.cpp
git commit -m "feat: wire portal_mode=usb into InstallConfiguredPortal"
```

---

### Task 6: Manual verification against the real Wii U portal

**Files:** none (verification only; may produce a new dated section in
`docs/investigation/portal-protocol.md` if something unexpected is found — see Step 3).

**Interfaces:**
- Consumes: everything from Tasks 1-5.
- Produces: go/no-go confirmation that this feature actually works, which Task 7's documentation
  update depends on.

- [ ] **Step 1: Plug in the Wii U Traptanium portal, confirm it still shows as HidUsb**

In Windows Device Manager or Zadig (do not click "Replace Driver"), confirm the device at
`1430:0150` is still bound to the stock HID driver, matching the state confirmed during
brainstorming.

- [ ] **Step 2: Run with the real device attached**

Run: `out\build\win-amd64-debug\giantrecomp.exe --portal_mode usb`

Expected, matching the parity bar from spec §1 (same as what `SoftwarePortal` with an empty
portal already does per `docs/investigation/portal-protocol.md`'s "Verified with the real
SoftwarePortal" section):

- The log's newest file shows `Portal: usb` (not the "no USB portal found" warning).
- Press A at the title screen, choose Story, a `NEW` slot: the game should get past the portal
  check into Story mode and show "Player 1: Please put a Skylander on the Portal of Power" (an
  empty-but-working portal), not "Can't find the Portal of Power".
- Place a real figure on the real portal: the game should recognize it (matching what
  `--portal_figure <dump>` already does with `SoftwarePortal`) — same acceptance screen, the
  figure's name/stats show correctly, and it can be taken into a level.
- No crash, no `FATAL` line, in the log for the whole run.

- [ ] **Step 3: If anything above does not match, write it up before changing code**

If a step in Step 2 fails or behaves differently than expected (e.g. the game never sees the
figure as present, or the real device needs something `hid_write`/`hid_read_timeout` alone
doesn't provide), add a new dated section to `docs/investigation/portal-protocol.md` describing
exactly what was observed, the same way the milestone 4/5 sections already in that file were
written — concrete, dated, marked *observed* vs *inferred*. Do not guess a fix without that
write-up; the spec's §7 already flags the HID `SET_IDLE`/`SET_PROTOCOL` control requests as the
one known area a native HID class driver might not auto-negotiate identically to what Cemu's
explicit libusb calls do, so that is the first thing to check if this step fails.

This step has no fixed code changes to give in advance — what to do next depends entirely on what
is actually observed. If new code is needed, it is a follow-up to this plan (a new, small task
handled the normal way: failing repro, minimal fix, verify), not something to improvise inline.

- [ ] **Step 4: Commit any write-up**

Only if Step 3 produced one:

```bash
git add docs/investigation/portal-protocol.md
git commit -m "docs: real USB portal verification findings"
```

---

### Task 7: Documentation

**Files:**
- Modify: `README.md`

**Interfaces:** none — documentation only, gated on Task 6 actually succeeding.

- [ ] **Step 1: Move "Real portals over USB" from "Not yet" to "Works today"**

In `README.md`'s "What works, and what doesn't" section, remove this line from **Not yet**:

```
- Real portals over USB (Wii U and Xbox 360 Traptanium portals are the targets).
```

Add to **Works today**:

```
- A real, physical Portal of Power over USB (`portal_mode = "usb"`) — tested with a Wii U
  Traptanium portal. No driver replacement needed.
```

- [ ] **Step 2: Update the settings-file documentation**

In the "Settings file" section, update the `portal_mode` bullet:

```
- `portal_mode`: `"software"` (default) uses the virtual portal; `"usb"` uses a real, physically
  connected Portal of Power (tested with a Wii U Traptanium portal, USB ID `1430:0150`); `"none"`
  disables the portal and the game says it can't find one.
```

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: document the real USB portal (portal_mode=usb)"
```
