# Figures

The figure file format, the built-in catalog, and the in-game picker. See
`docs/portal-protocol.md` for how a figure's bytes reach the game over the wire.

## File format

- **`thirdparty/skylanders-portal/src/portal/figure_file.h/.cpp`**: reads and writes a figure as a flat 1024-byte file
  (`LoadFigureFile`, `SaveFigureFileAtomic` — write-to-temp-then-rename, so a crash mid-save can't
  corrupt the original).
- `CreateBlankFigure(id, variant)` builds a valid, blank figure from scratch: manufacturer/serial
  bytes, the id and variant at their fixed offsets (`0x10`, `0x1C`), a CRC-16/CCITT checksum, and
  the Mifare Classic sector-trailer access bits every sector needs for the game to treat the tag as
  valid. No dump of an existing figure is required to create a new one.
- Every figure a slot holds — whether loaded at startup (`--portal_figure`), placed from the
  overlay, or freshly created — saves its writes back to the exact file it came from, via
  `SoftwarePortal`'s write callback wired up in skylanders-portal's `portal_rex/portal_rex.cpp`.

## Catalog

`thirdparty/skylanders-portal/src/portal/figure_catalog.h/.cpp`:

- `AllSkylanders()` is the built-in id/variant → name/game table used to show real names instead of
  raw ids.
- `ScanFigureCatalog(root)` recursively finds `.dump` files under a folder for the picker, grouped
  by top-level subfolder.

## Overlay

`thirdparty/skylanders-portal/src/portal_rex/portal_overlay_dialog.cpp` is a `rex::ui::ImGuiDialog`, opened with **F6**
(bound by `skylanders::RegisterPortalOverlay`, called from `OnCreateDialogs` in `src/giantsrecomp_app.h`). It has a Browse tab (filterable list
from `ScanFigureCatalog`, Place/Remove into a chosen slot 0-15) and a New Figure tab (pick a
Skylander from `AllSkylanders()`, create and place it via `CreateAndPlaceFigure`). It only calls
the control API in `portal_rex/portal_rex.h` and `portal/figure_catalog.h` — it never touches
protocol bytes directly, and works the same regardless of which `PortalDevice` backend is active.
