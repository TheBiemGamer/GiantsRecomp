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
