# Figure Creation & Picker v3 Design

Date: 2026-09-27
Status: Draft, pending review

## 1. Goal

Let the user create a new Skylander figure file from just a name — no real dump needed — and browse/place created figures from the overlay, everything grouped by game. Extends the v2 figure picker (milestone 6/6.5, `src/overlay/portal_overlay_dialog.*`) which already scans a folder of `.dump` files into slots.

### Success criteria

- With no `--portal_figure`/`--portal_figures_dir` flag, the game still has a working figures folder: `<user_data_root>/figures`, created if missing (already true since milestone "figure picker v2").
- From the overlay, the user can pick a game, then a Skylander name, and create a new blank figure file for it without owning a real tag or dump.
- The created file loads correctly in-game (game accepts it as a real figure — same acceptance path as a real dump, since the software portal cannot distinguish the two once loaded).
- Both the creation list and the existing browse list are grouped by game, matching the user's own dump collection's organization.
- The picker shows each figure's real Skylander name (e.g. "Tree Rex"), not the slot index or, where avoidable, the raw filename.

### Non-goals for this iteration

- Editing an existing figure's stats/inventory beyond what already exists (out of scope; that's game-write-driven, already handled by milestone 5's save-back).
- A full icon/portrait catalog — text names only, matching the existing picker's style.
- Supporting Skylanders outside the 648-entry table (see §2) — if the user's install ever needs an entry the table lacks, it can be added by re-running the extraction script in §2 against a new dump.

## 2. Skylander catalog data

The game's own byte format (§3) does not carry a name — only numeric `id` and `variant`. A name needs a lookup table. Two sources were considered:

- Cemu's `s_listSkylanders` table (`Skylander.cpp`): 481 `{id,variant} -> name` entries, whole franchise, but **no game field**, and copying it would mean re-implementing this feature against MPL-2.0-derived data (this repo already decided, in the original design spec §3, to re-derive facts from reference emulators rather than copy from them, to keep this repo MIT-clean).
- The user's own real dumps (`Dumps Clean`, per [[reference-figure-dumps]]): 1128 files, organized in one top-level folder per game.

**Decision:** extract the table directly from the user's own dumps. Verified 2026-09-27 by scanning all 1128 files (`struct.unpack("<H", data, 0x10)` for id, `0x1C` for variant): 648 unique `(id, variant)` pairs, zero pairs spanning more than one game folder (game is a clean, stable property of the key). 480 of the 1128 files are cosmetic reissues (e.g. "Chrome Spyro", "Gold Chop Chop") that share the exact same `(id, variant)` as their base figure — the tag format cannot distinguish them, so they collapse to one catalog entry; the shortest of the colliding names is kept (empirically always the plain/base name).

The result — 648 rows of `{id: uint16, variant: uint16, name: string, game: string}`, `game` one of `Spyro's Adventure`, `Giants`, `Swap Force`, `Trap Team`, `SuperChargers`, `Imaginators` — is committed as generated C++ data: `src/portal/skylander_catalog_data.h`, a `constexpr std::array` of a small POD struct. No dump bytes, only names/ids/game strings (pure fact, not the emulator's copyrighted table). Generation is a one-time offline script (not part of the build); regenerating it later (e.g. if new figures are dumped) means re-running that script and recommitting the header.

Rationale for a compiled-in header over a runtime data file (JSON/TOML): the project has no JSON/TOML parsing library wired into the app itself (`config/default.toml` is consumed by the build's own tooling, not app runtime code), the table is static (648 rows, ~20KB of string data), and a `constexpr` array needs no parser, no missing-file case, and no new dependency.

## 3. Blank figure format

Verified 2026-09-27 directly from Cemu's `SkylanderUSB::CreateSkylander` and `SkylanderCRC16` (`src/Cafe/OS/libs/nsyshid/Skylander.cpp`), full source read, not summarized. Total size and block/sector geometry match this project's own `kBlockSize=16`, `kBlockCount=64` (`src/portal/portal_device.h`) exactly — both describe the same physical format, Mifare Classic 1K.

Absolute byte offsets in the 1024-byte figure:

| Offset | Content |
|---|---|
| `0x00-0x03` | random 4-byte serial |
| `0x04` | BCC = byte0 ^ byte1 ^ byte2 ^ byte3 |
| `0x05` | `0x81` |
| `0x06` | `0x01` |
| `0x07` | `0x0F` |
| `0x08-0x0F` | zero |
| `0x10-0x11` | Skylander ID, uint16 little-endian |
| `0x12-0x1B` | zero |
| `0x1C-0x1D` | Skylander variant, uint16 little-endian |
| `0x1E-0x1F` | CRC16-CCITT (poly `0x1021`, init `0xFFFF`) over bytes `0x00-0x1D`, little-endian |
| `0x20-0x2F` | zero |
| sector trailers (byte offsets `0x36`, `0x76`, `0xB6`, ... every `0x40`, i.e. bytes 6-9 of block index `sector*4+3`) | access bits: `0F 0F 0F 69` for sector 0, `7F 0F 08 69` for sectors 1-15 (little-endian bytes of Cemu's `0x690F0F0F`/`0x69080F7F` `uint32_t` constants, as its native `memcpy` actually writes them -- confirmed 2026-09-27 against a real dump's sector-1 trailer, which reads exactly `7F 0F 08 69`) |
| everything else | zero |

This is re-derived as a set of facts about the on-tag data format (which this project's own dumps already independently confirm for the ID/variant/checksum-adjacent bytes — see [[reference-figure-dumps]]), not copied code, consistent with the licensing approach in the original design spec §3.

New function `CreateBlankFigure(uint16 id, uint16 variant) -> portal::FigureData` in `src/portal/figure_file.{h,cpp}` (alongside the existing `LoadFigureFile`/`SaveFigureFileAtomic`, which it composes with — callers save it with the existing `SaveFigureFileAtomic`). Pure, no file I/O, unit-testable: given fixed random bytes (inject a `std::mt19937&` or seed for the test; production uses `std::random_device`), the CRC and access-bit bytes are fully deterministic and checkable byte-for-byte against the table in this section.

## 4. Catalog module changes

`src/portal/figure_catalog.h` gains:

```cpp
struct SkylanderInfo {
  uint16_t id;
  uint16_t variant;
  std::string_view name;
  std::string_view game;
};

// The full built-in catalog (skylander_catalog_data.h), already sorted by game then name —
// the order the creation UI and any other consumer should present it in.
std::span<const SkylanderInfo> AllSkylanders();

// Looks up a figure's real name from the id/variant bytes read out of `data`. Returns nullptr
// if the id/variant pair isn't in the built-in catalog (e.g. a homebrew or unrecognized figure).
const SkylanderInfo* FindSkylander(const portal::FigureData& data);
```

`FigureCatalogEntry` (existing, used by `ScanFigureCatalog`) gains a `display_name` field: `FindSkylander`'s name if the file's bytes resolve to a known entry, else falls back to the current behavior (filename stem). `ScanFigureCatalog` already opens nothing but paths today — it will need to peek each file's id/variant bytes (16 bytes: `LoadFigureFile` then read offsets `0x10`/`0x1C`), which is an added disk read per catalog entry; acceptable since the picker's "Rescan" is already a full directory walk, not a hot path.

## 5. Creation flow

New function in `src/hooks/portal_hook.{h,cpp}`, alongside `PlaceFigureFromFile`/`RemoveFigureFromSlot`:

```cpp
// Creates a new blank figure for `sky` under portal_figures_dir/<game>/<name>.dump (creating the
// game subfolder if needed), then places it into `slot` exactly like PlaceFigureFromFile. If a
// file of that name already exists, appends " (2)", " (3)", ... until a free name is found — the
// existing file is never overwritten. Returns false, and creates nothing, if there is no active
// software portal, portal_figures_dir is unset, or the folder can't be created.
bool CreateAndPlaceFigure(int slot, const portal::SkylanderInfo& sky);
```

This keeps the created file's folder structure mirroring the user's own dumps (one folder per game), so the existing `ScanFigureCatalog`'s `game` grouping already picks it up on the next scan with zero changes to that scanner.

## 6. Overlay UI

`src/overlay/portal_overlay_dialog.*` gains a second mode alongside the existing browse list, switched by a tab or toggle button ("Browse" / "New Figure"):

- **New Figure** tab: the filter box (reused) narrows by name across the full 648-entry catalog; results are grouped under a `SeparatorText(game)` exactly like the browse list already does (`portal_overlay_dialog.cpp:122-124`), so the "sort by game" requirement is satisfied identically in both tabs by construction, not by two separate implementations. Selecting an entry and pressing "Create" calls `CreateAndPlaceFigure(selected_slot_, entry)`.
- **Browse** tab: unchanged except `SlotLabel` (currently `source->stem()`, `portal_overlay_dialog.cpp:36`) and the list's displayed name (currently `entry.name`, the filename) switch to `entry.display_name` from §4.

The slot selector, filter box, and per-game grouping are the only pieces of state/logic touched; `Rescan()`, the empty/missing-folder messages, and the Remove button are unchanged.

## 7. Testing

- `CreateBlankFigure`: unit test with an injected fixed serial, asserting every byte in the table in §3 (including both sector-trailer access-bit patterns and the CRC), following the existing test style in `tests/software_portal_test.cpp`.
- `FindSkylander`/`AllSkylanders`: unit tests against a handful of known id/variant pairs from the catalog (e.g. Tree Rex `{112, 0}`) and one unrecognized pair (expect `nullptr`).
- `CreateAndPlaceFigure`: integration-style test using a temp directory, asserting the file lands at `<dir>/<game>/<name>.dump`, is a valid `LoadFigureFile`-readable figure, and that a second call for the same Skylander produces a `" (2)"`-suffixed file rather than overwriting.
- No UI test (ImGui dialogs are not unit-tested elsewhere in this repo either).

## 8. Out of scope / open follow-ups

- What happens to `portal_figures_dir` on a second run where the user also passes `--portal_figure` (single-slot startup figure) is unchanged from today's behavior; this spec does not touch startup-figure handling.
- Renaming or deleting a created figure from the overlay is not part of this iteration (the OS file browser still works for that, same as today).
