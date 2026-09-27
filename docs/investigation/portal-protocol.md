# Portal protocol as the game uses it (milestone 4 spike)

Findings from replacing the game's two portal wrappers with throwaway hooks that answered commands, then playing through the start of Story mode by hand. Everything here was *observed* in traces of `default.xex` (Title ID 415608DA) unless marked *inferred*. The spike code was never committed. The Cemu and RPCS3 portal code was used as a reference for the command letters and reply shapes only.

## Wrapper calling convention

The game reads and writes through two recompiled wrappers found in milestone 3 (`docs/investigation/portal-api.md`):

- **Read**, `sub_82403BB8(r3, r4, r5)`:
  - `r3` points to a 32-bit big-endian **bytes-read** value. It is `0` on entry and **must be set** (the spike used `32`) or the game ignores the reply.
  - `r4` points to a 32-bit value holding the buffer size, `0x20`.
  - `r5` is the 32-byte buffer to fill.
  - The return value is `1` for success and `0` for failure. A failure is what the game shows as "Can't find the Portal of Power".
- **Write**, `sub_82403C28(r3, r4)`: `r4` points to a 32-byte buffer holding one command frame. `r3` is the frame's meaningful length (*inferred*: `R` 4, `A` 4, `S` 3, `Q` 5, `C` 6, which fits 2 header bytes + the command byte + its arguments).
- Both wrappers call the initializer `sub_82403B18` first. A replacement wrapper must call it too, otherwise the "portal API available" flag at `0x8265BA9C` is never set. The spike called it on a copy of the register context so the hook's own registers were not clobbered.

## Frames

Every frame, in both directions, is 32 bytes: the header `0B 14`, then a 30-byte payload whose first byte is the command letter. Unused bytes are zero.

## Commands seen, and the replies the game accepted

| Command (payload) | Meaning | Reply payload the spike sent |
|---|---|---|
| `52 <x>` (`R`) | ready | `52 02 1B` |
| `41 <n>` (`A`), sent with `n` = 0 then 1 | activate | `41 <n> FF 77` |
| `53` (`S`) | status poll | a status frame (below) |
| `43 R G B` (`C`) | set LED colour | none. Sent roughly 900 times in 90 s, with changing values. |
| `51 <seq/slot> <block>` (`Q`) | read one 16-byte block of a figure | `51 <0x10 or 0x00 | slot> <block>` + 16 data bytes |

- The game repeats `R` about every 300 ms until it gets an answer, then moves on to `A`.
- `Q`'s second byte has a rolling high nibble (`0x10`, `0x20`, ... `0xF0`) with the slot in the low nibble (always 0 in the spike, *inferred* from the pattern). The spike replied with the low nibble only, and the game accepted that.
- `M` (version) and `W` (write block) never appeared. With invalid figure data the game did not write.

## Status frames and idle reads

A read with no queued command reply must return a **status frame**, not repeat the last reply:

`0B 14 53 <4-byte little-endian slot states> <counter> <active>`

- Slot states are 2 bits per slot, slot 0 lowest: 0 empty, 1 present, 2 removing, 3 added (*from Cemu*; the spike used 3 for about 30 polls, then 1).
- The counter increments on every status frame. The active flag is 1 after `A`.
- The game polls the read about 56 times a second all the time.

## Figures

- About 5 seconds after the portal was active, the spike showed slot 0 as added. The game then read **all 64 blocks** (`Q` for blocks `0x00` to `0x3F`) within roughly 4 seconds, and did nothing else with the figure.
- The spike returned **zeroed** data for every block. In gameplay the game then showed **"A toy on the Portal of Power has a problem"**. So the game validates the figure data; a valid figure needs real, correctly checksummed tag data (milestone 5).
- With no figure attached, the game reaches the title, the Story slot picker, the intro cutscene and the chapter load.

## Not yet known

- What a *valid* figure's blocks must contain (layout, checksums, encryption): milestone 5.
- What `W` looks like and whether the game writes progress to the figure.
- What the game does on removal, and the exact meaning of `Q`'s high nibble.
- Whether `M` (version) or the LED commands `J` and `L` are ever used.

## Verified with the real SoftwarePortal (milestone 4)

The throwaway spike was replaced by `SoftwarePortal` and the hooks in `src/hooks/portal_hook.cpp`. Four runs against the real game, with a person pressing A at the title screen:

- **Default (`software`), no figure:** the game gets past the portal check, into Story mode, and shows "Player 1: Please put a Skylander on the Portal of Power." So an empty, working portal is accepted.
- **`--portal_mode none`:** the game's own "Can't find the Portal of Power" screen appears, so the original code path is intact when no portal is installed.
- **`--portal_mode banana`:** a warning is logged (`Unknown portal_mode 'banana'; running with no portal`) and the game behaves as with `none`.
- **`--portal_test_figure`:** the game reads the figure and shows "A toy on the Portal of Power has a problem." The all-zero data is rejected, so playing needs valid figure data (milestone 5).

No crashes or `FATAL` lines in any of the four logs.

## Real figure dumps, and the activate command (milestone 5 proof of concept)

- A raw 1024-byte dump of a real figure, served to the game unchanged through `Q` replies, is accepted. Tree Rex (Giants) loaded into Story mode and the first level. No tag crypto or checksum code was needed for this. The dump files have a valid block-0 checksum and a plausible figure ID at `0x10` (little-endian).
- The game sends `A 01` (activate) about **every 10 seconds** while the portal is already active. The first version of `SoftwarePortal` announced every present figure as "added" on each `A`, so the game re-read all 64 blocks after each one and the figure looked taken off and put straight back. A figure must be announced only when the portal goes from inactive to active. `A 00` deactivates. With that change the flicker is gone.
- The game **writes** to the figure: `W` for block 8 (`57 10 08 ...`) followed by a `Q` read of the same block. The portal stores it in memory; nothing is saved back to the dump file yet.

## Saving figure writes (milestone 5, part 2)

`SoftwarePortal::SetWriteCallback` fires after every successful `W` (a present slot, a valid block), with the slot index and the figure's full 1024-byte data. `--portal_figure` wires this to `SaveFigureFileAtomic`, which writes to a `.tmp` file next to the original and renames it over the original, so a crash mid-save cannot corrupt the figure. The save happens synchronously on whichever thread calls `Write()`, since the game writes to a figure only occasionally (once per affecting event, not every frame), not on a timer and not only at shutdown; the process log showed a `Title terminated; hard-exiting process` line on quit in an earlier run, which suggests a clean C++ shutdown is not guaranteed, so a save tied to `OnShutdown` would not be trustworthy on its own.

Not yet re-verified in gameplay: whether the resulting file is accepted correctly on the next load (the `W` block 8 write observed on 2026-09-27 has not yet been confirmed to round-trip through a real play session).

## Confirmed by the author (2026-09-28)

Played into chapter 2 on the Release build with `--gpu_allow_invalid_fetch_constants`: no blanking. Figure progress (Tree Rex's level, upgrades, gold) persisted correctly across quitting and relaunching. Cutscenes run well on Release. Both fixes from this session hold up in real play.

## Real USB portal (Wii U, `1430:0150`), first hardware test (2026-09-27, milestone 7)

*Observed*, with `portal_mode=usb` (`UsbPortal`, `src/portal/usb/usb_portal.cpp`) against the
author's real Wii U Traptanium portal, driver unchanged (`HidUsb`, confirmed via
`Get-PnpDevice`, `Class = HIDClass`), a temporary diagnostic build logging every `hid_write`/
`hid_read_timeout` call:

- `hid_init()` and `hid_open(0x1430, 0x0150, nullptr)` both succeed immediately. The log shows
  `Portal: usb` (not the no-device warning), and the process runs with no crash and no `FATAL`
  line for the whole session.
- The real device's raw HID report is **32 bytes**, not 64. Every `hid_write` of our
  65-byte `EncodeOutputReport` buffer (1 report-ID byte + 64, the last 32 of which are our
  zero-padding) returns `33` bytes written — i.e. exactly `1 (report ID) + 32 (the device's real
  report size)`, with the extra padding silently dropped. Every `hid_read_timeout` call returns
  exactly `32` bytes. `kDeviceReportSize = 64` (`usb_report_codec.h`) was carried over from Cemu's
  `SkylanderUSB` class (`Skylander.h`, `std::array<uint8, 64>`), which turns out to describe
  Cemu's own **virtual/emulated** portal's internal buffer size, not the real Wii U hardware's
  actual USB report size. *Inferred*: for this device, 32 in and 32 out is correct; the
  32-to-64-and-back adaptation this milestone built is unnecessary for it (may still matter for
  the untested Xbox 360 portal, `1430:1F17`, which is a different physical device on the
  whitelist).
- Over 400 consecutive read/write cycles (several seconds, well past the title screen), **every**
  write was `52` (`'R'`, ready poll) and **every** read returned a frame starting with `53`
  (`'S'`, status), with bytes 1-4 zero (no figure present, as expected) and byte 5 a steadily
  incrementing counter — a correctly-shaped, live status stream from the real firmware. The game
  never sent `41` (`'A'`, activate): per the milestone-4 findings above ("The game repeats `R`
  about every 300ms until it gets an answer, then moves on to `A`"), the game is waiting for a
  reply shaped like `52 02 1B` to its `R`, and in 400 samples that reply never appeared — only the
  continuous, unsolicited status stream did.
- End result: "Can't find the Portal of Power" at the title screen, indefinitely.

**Not yet known**: why the real device never appears to answer `R` directly, when the milestone-4
spike (built from a console-side capture plus RPCS3/Cemu reference reading, not a real-hardware
USB capture) assumed a request/reply model where each written command gets one queued reply
drained by the next read — matching how `SoftwarePortal` is built today. The real device instead
looks like it just streams unsolicited status input reports regardless of what's written to it.

*Inferred*, not yet tested: Cemu's own real-hardware backend (`BackendLibusb.cpp`) sends outgoing
commands two different ways depending on call site — plain interrupt OUT transfers
(`DeviceLibusb::Write`, what `hid_write` also does) for some paths, but a HID class `SET_REPORT`
**control** transfer (`DeviceLibusb::SetReport`, endpoint 0, not the interrupt OUT endpoint) for
others. If this device's command report is defined as a Feature report rather than an Output
report in its HID report descriptor, `hid_write()` (Output report semantics) would be the wrong
call entirely, and hidapi's `hid_send_feature_report()` (control-transfer, Feature report
semantics) would be the one to try instead — cheap to test, since it only changes one call site in
`UsbPortal::Write`. Not attempted in this pass; flagged here for the next one rather than guessed
at inline.

## Both follow-up hypotheses tried, both disproven (2026-09-27, same session)

*Observed*, both against the real Wii U portal, throwaway diagnostic builds (not committed):

- **Feature report instead of Output report**: swapped `hid_write()` for
  `hid_send_feature_report()` in `UsbPortal::Write`, same buffer. Every call returned `-1`
  (error) — the device does not implement a Feature report at all. This rules out the Feature-
  report hypothesis above; the device's command report genuinely is an Output report, and
  `hid_write()` is the right call.
- **Reply buried behind stale status frames**: after every `hid_write`, drained up to 5 more
  reports back-to-back with a 5ms timeout each (`hid_read_timeout`), instead of waiting for the
  next normal poll cycle. Across dozens of write/drain cycles, every single drained report —
  buffered or not — was `53` (status). Never once saw a `52`-prefixed reply, buried or otherwise.
  This rules out a queuing/staleness explanation: the device is not withholding a reply behind a
  backlog, it simply never sends one.

**Conclusion so far**: the real device does not implement the request/reply model the milestone-4
spike assumed (one queued reply per written command) at all. It looks like a pure autonomous
status-stream device: it reports its own state continuously on the IN endpoint, and whatever
effect a written command has (activating, LEDs, block reads) happens silently, reflected only in
the next status frame's state bits, if at all — never as a distinct reply frame keyed to the
command that was sent. `SoftwarePortal`'s reply-queue model (`replies_` in
`software_portal.cpp`) and the whole framing this feature was built against may not describe how
real hardware behaves; the milestone-4 spike's reply shapes (`R`→`52 02 1B`, `A`→`41 <n> FF 77`,
etc.) were the *spike's own* chosen replies when it stood in for the portal, not necessarily
verified against real firmware.

**Not yet tried**: a real packet capture (USBPcap + Wireshark, or Cemu's own logging with
`BackendLibusb` against this exact device) to see what a real console/Cemu session actually
exchanges with this hardware, since both cheap code-level hypotheses are now exhausted and further
guessing from this side isn't warranted without new ground truth.
