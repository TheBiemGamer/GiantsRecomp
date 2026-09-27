# Figure stats (level/gold/nickname) — design

## Goal

Decode a Skylander's level, gold, and nickname from its save-data bytes and show them in the F6
portal overlay:

- **Virtual figures**: inline in the Browse tab's existing dump list.
- **Real figures**: for a figure on a real, physically connected Portal of Power, on demand.

No other fields (playtime, upgrades, hat, element, ...) are in scope for this pass.

## Why this needs research first

Nothing in this repo documents Giants' save-data layout. `docs/figures.md` and the figure-format
memory only cover the fields needed to *create* a blank figure (manufacturer bytes, id, variant,
CRC, Mifare sector-trailer access bits) — all zero in a blank figure. Level/gold/nickname are
written by the game during play, into bytes this project has never decoded.

The recompiled game code (`generated/default/*.cpp`) has no symbol names — static recompilation
doesn't preserve them — so there is no "find the parser function" shortcut. Instead, the format
will be reverse-engineered empirically: temporary trace logging of every `Q`/`W` block
(slot, block index, 16 bytes hex) added to `SoftwarePortal`'s existing block handlers
(`src/portal/software/software_portal.cpp:60,72`) and to `UsbPortal::ObserveReply`, then a real
play session (level up, spend gold, attempt a rename) with logging on. Diffing before/after block
dumps against the observed action isolates the exact offsets. This is a prerequisite task in the
implementation plan, not something this design can pin down in advance — `ParseFigureStats`'s
*interface* is fixed here; its internals get their real offsets from that research.

## Components

### `src/portal/figure_stats.h/.cpp` (new)

```cpp
struct FigureStats {
  uint8_t level;
  uint32_t gold;
  std::string nickname;
};

// Returns nullopt if `data` doesn't look like a valid Giants figure (blank, foreign-game,
// corrupted) rather than returning guessed/garbage values.
std::optional<FigureStats> ParseFigureStats(const FigureData& data);
```

Same shape as `figure_catalog.h`: pure data-in/data-out, no dependency on portal transport. Used
identically for a virtual dump's bytes and a real figure's freshly-read bytes.

### `UsbPortal` — thread-safe raw block read

Today `UsbPortal::Write()`/`Read()` (`src/portal/usb/usb_portal.cpp:37-65`) call
`hid_send_output_report`/`hid_read_timeout` directly, with no locking, driven only by the game's
own hook thread. Adding an on-demand full-figure read from the UI thread means a second caller can
now reach the same `hid_device*` concurrently — a real race, since the wire protocol has no
request ID to tell "this reply is ours" from "this reply belongs to the game's in-flight request."
An interleaved reply could hand the game corrupted block data mid-play.

Fix: a mutex around all raw HID I/O, covering both the existing game-driven path and the new one,
so whichever side is talking to the device owns a full request-reply round trip atomically:

```cpp
// Reads all 64 blocks of the figure in `slot` directly (bypassing the game's own polling).
// Returns nullopt on any timeout, I/O error, or if no device is open. Briefly blocks the game's
// own Write()/Read() calls for the duration (64 round trips, on-demand only -- not called from a
// per-frame path).
std::optional<FigureData> ReadAllBlocks(int slot);
```

`Write()` and `Read()`'s existing bodies move under the same mutex.

### `portal_hook.h/.cpp`

```cpp
// Reads a real figure's full data directly from the portal, independent of the game's own
// polling. Returns nullopt if there's no active USB portal, no figure in `slot`, or the read
// fails.
std::optional<portal::FigureData> ReadRealFigureBlocks(int slot);
```

Thin wrapper over `GetUsbPortal()->ReadAllBlocks(slot)`. Keeps the overlay's existing layering
(`docs/figures.md`: "It only calls the control API in `src/hooks/portal_hook.h` and
`src/portal/figure_catalog.h`") — it never touches `UsbPortal` directly.

### `portal_overlay_dialog.cpp`

- **Browse tab**: for each listed `.dump` (from `ScanFigureCatalog`), `LoadFigureFile` +
  `ParseFigureStats`, shown inline in the existing list row. Files are ~1KB; scanning and decoding
  the whole folder on each list refresh is cheap. A row that fails to parse shows blank/"—", not
  an error.
- **Real portal**: on dialog open and on an explicit Refresh action (not every frame), call
  `ReadRealFigureBlocks` for each present real-portal slot, then `ParseFigureStats`, and show the
  result. Not polled continuously, since each call briefly blocks the game on the new mutex.

## Data flow

- **Virtual**: `ScanFigureCatalog` → `LoadFigureFile` → `ParseFigureStats` → render. Triggered by
  the Browse tab's existing list refresh.
- **Real**: F6 open/Refresh → `portal_hook::ReadRealFigureBlocks(slot)` →
  `UsbPortal::ReadAllBlocks` (mutex-guarded, 64×`Q`) → `ParseFigureStats` → render.

## Error handling

- `ParseFigureStats` returns `nullopt` for anything unrecognized; callers show blank/"—", never a
  best-effort guess.
- `ReadAllBlocks` aborts the whole read on the first failed block — no partially-read `FigureData`
  is ever treated as valid.
- An empty slot is skipped before any read is attempted.

## Testing

- `ParseFigureStats`: unit tests once real offsets are known, using actual captured before/after
  dumps from the research play session (asserting exact decoded values, not synthetic guesses).
- `UsbPortal::ReadAllBlocks`: not realistically unit-testable (real hardware only, consistent with
  the rest of `UsbPortal` today) — verified manually against the real Wii U Traptanium portal.

## Out of scope

- Any field besides level, gold, nickname (playtime, upgrades, hat, element, heroic challenges).
- Editing/writing stats back (this is read/display only).
- Nickname *setting* if it turns out Giants doesn't support in-game renaming at all — the research
  task will confirm whether that field even changes; if it doesn't, nickname display is dropped
  from this pass rather than guessed at.
