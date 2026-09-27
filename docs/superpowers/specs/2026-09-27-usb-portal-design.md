# Real USB Portal Design

Date: 2026-09-27
Status: Draft, pending review

## 1. Goal

Let the game talk to a real, physically-plugged-in Portal of Power over USB instead of (or as an
alternative to) the existing `SoftwarePortal`. This is milestone 7, already anticipated by a `TODO`
in `src/hooks/portal_hook.cpp`'s `InstallConfiguredPortal`. The author's hardware for this
iteration is a Wii U Traptanium portal (USB `1430:0150`), currently bound to Windows' stock HidUsb
driver, no WinUSB/Zadig replacement done.

### Success criteria

- `portal_mode = "usb"` in `giantsrecomp.toml` (or `--portal_mode usb`) opens the real portal and
  routes the game's existing read/write hooks to it, with no changes to the hook layer, the
  `PortalDevice` interface, or `xbox_frame.cpp`.
- With the portal plugged in and this mode selected, the game reaches Story mode and recognizes a
  real figure placed on the real portal (parity with what `SoftwarePortal` already does for a
  loaded `.dump` file, per `docs/investigation/portal-protocol.md`'s "Real figure dumps" findings).
- Device not present/wrong VID-PID at startup degrades to the same "Can't find the Portal of Power"
  behavior `portal_mode = "none"` already produces today — no new error UI.
- No Zadig/driver replacement required for the user.

### Non-goals for this iteration

- Xbox 360 physical portal (USB `1430:1F17`) — same wire protocol family, but no hardware to test
  against yet. The whitelist includes it (§3) so it works if plugged in, but it is not verified.
- Hot-plug (attaching the portal after the game has started). Out of scope; matches how
  `portal_mode` is already a `kRequiresRestart`-style startup choice for every other mode.
- Writing a real NFC tag back (the figure's on-portal memory is written by the portal's own
  firmware over the same USB link already covered by this design; this refers to anything beyond
  that, e.g. bulk-cloning a tag) — not a thing this feature needs to do.
- LED control beyond passthrough — the game's own `C`/`J`/`L` commands (per §2) reach the real
  portal like any other command; no new UI or cvar to control LEDs independently.

## 2. What the wire protocol already is

Two prior investigation docs already establish this, independently, from two different angles:

- `docs/investigation/portal-protocol.md` (milestone 4 spike): observed, from the real Xbox 360
  game's own console-side hook, that every frame in both directions is 32 bytes — header `0B 14`,
  then a 30-byte payload whose first byte is an ASCII command letter (`R` ready, `A` activate, `S`
  status poll, `C` set LED, `Q` read block, `W` write block), with exact reply shapes for each,
  verified against real gameplay (Tree Rex loaded and played into a level).
- Cemu's `SkylanderUSB::ControlTransfer` (`src/Cafe/OS/libs/nsyshid/Skylander.cpp`, read for facts
  only, not copied) confirms the same command letters at `buf[0]` with no leading `0B 14`, over
  **64-byte** reports — the portal-family firmware protocol, shared across every console SKU that
  has shipped this hardware.

Conclusion: the `0B 14` header is Xbox-360-XAM-specific framing, already stripped/added at the
hook boundary by `xbox_frame.cpp`'s `ReportFromFrame`/`FrameFromReport`. `PortalDevice::Report` —
already documented as "no console framing" — is already the bare-opcode-first-byte form the real
USB device speaks. The only remaining gap between `Report` (32 bytes) and what the real device
expects is **size**, not protocol: the device's real reports are 64 bytes.

This also means `UsbPortalDevice` does not reimplement any portal logic (status frame contents,
activate-on-transition-only, block read/write). The real portal's firmware already does all of
that; the device just needs its raw bytes relayed.

## 3. Components

New `src/portal/usb/usb_portal.h/.cpp`, parallel to `src/portal/software/software_portal.h/.cpp`,
implementing `PortalDevice`:

```cpp
class UsbPortal final : public PortalDevice {
 public:
  // Opens the first whitelisted portal found. Empty on failure (device absent, open failed);
  // check IsOpen() before installing this as the active portal.
  UsbPortal();
  ~UsbPortal() override;

  bool IsOpen() const;

  void Write(const Report& report) override;  // zero-pads to 64 bytes, hid_write
  Report Read() override;                     // hid_read, truncates 64 bytes to 32
};
```

A small whitelist, mirroring Cemu's `Whitelist.cpp` restricted to this project's actual targets:

```cpp
constexpr std::pair<uint16_t, uint16_t> kKnownPortals[] = {
    {0x1430, 0x0150},  // Wii U Skylanders portal
    {0x1430, 0x1F17},  // Xbox 360 Skylanders portal
};
```

`portal_mode.h`/`.cpp` gains `PortalMode::kUsb` / `"usb"`. `portal_hook.cpp`'s
`InstallConfiguredPortal` gains a third branch: construct `UsbPortal`, and if `IsOpen()` install it
into `g_portal` exactly like `g_software_portal` is installed today; if not open, log a warning and
fall through to the same no-portal behavior as `kNone`. Per the existing `TODO` in that function,
`g_software_portal` is left null in this branch, so `GetSoftwarePortal()` (used by the overlay for
figure-picker status queries) correctly reports no software portal active — the overlay's figure
picker is a `SoftwarePortal`-only feature today and stays that way; it is simply unavailable in
`usb` mode, same as it already is in `none` mode.

## 4. New dependency: hidapi

Vendored as a git submodule under `thirdparty/hidapi`, consistent with how `rexglue-sdk` is
already vendored. `hidapi`'s own CMake produces a `hidapi::hidapi` (or platform-specific, e.g.
`hidapi::winapi` on Windows) target; `usb_portal.cpp` links against it. No Zadig/WinUSB step:
`hidapi`'s Windows backend uses the native HID API against the device's existing HidUsb binding.

## 5. Error handling

- **Device not found / open fails** (unplugged, wrong VID/PID, in use by another process):
  `UsbPortal::IsOpen()` returns false, `InstallConfiguredPortal` logs a warning and behaves like
  `portal_mode = "none"`. The game's own existing "Can't find the Portal of Power" screen handles
  the rest — no new error path needed.
- **`hid_write`/`hid_read` failure during play** (device unplugged mid-session): logged; `Read()`
  returns a zeroed `Report` (matches what an idle/no-reply buffer already looks like to the game
  per the frame-read hook's existing handling for a failed read).

## 6. Testing

- No unit test for `UsbPortal` itself — it is a thin, side-effecting I/O wrapper with nothing pure
  to assert on without real hardware attached (consistent with this repo's existing pattern: the
  pure `xbox_frame.cpp` conversion functions have tests today; the raw hook hot-path does not).
- Manual verification against the author's real Wii U portal is the actual test for this
  milestone: confirm the game reaches Story mode with the real portal active, and that a real
  figure placed on it is recognized (parity check against the `portal-protocol.md` findings for
  `SoftwarePortal`).
- If the manual pass surfaces a real protocol gap the two prior docs didn't anticipate (padding
  behavior, a needed init handshake, endpoint quirks), it gets written up in
  `docs/investigation/portal-protocol.md` as a new dated section, the same way milestones 4 and 5
  already did.

## 7. Open follow-ups

- Whether the real device needs any request beyond plain `hid_write`/`hid_read` (e.g. a HID
  `SET_IDLE`/`SET_PROTOCOL` control request, which Cemu's libusb backend sends explicitly) is
  unknown until tested — native HID class drivers normally issue these automatically during
  enumeration, so this is expected to be a non-issue, but is called out here as the one real risk
  in this design.
- Xbox 360 portal (`1430:1F17`) verification, once/if that hardware is available.
