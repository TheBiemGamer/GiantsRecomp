# Build and test notes

Codegen fixes needed to recompile `default.xex`, and how the project is tested.

## Codegen fixes (`config/default.toml`)

Recompiling `default.xex` needs a handful of manual corrections, recorded as `[functions]` and
`[switch_tables]` entries in `config/default.toml`:

- **`[functions]`**: tiny functions reached only through a virtual-table call (a `bctr` indirect
  branch) aren't split from the function before them by ReXGlue's codegen, since it only splits on
  `blr` and tail-call `b`. Each entry pins one such function's start address and size so it's
  registered on its own instead of folded into its neighbor.
- **`[switch_tables]`**: a few jump tables (in the game's bit-stream decoder) have no bounds check
  before the indirect branch, so the analyzer under-counts their entries from the branch pattern
  alone. Each entry gives the real entry count for one table's address.

Both are keyed by guest address and are specific to this build of `default.xex`, which is why the
SHA-256 check at startup (`src/xex_verify.cpp`) exists.

## Testing

`src/portal/` has no dependency on ReXGlue or the game, so it's covered by plain-`main` unit tests
under `tests/` (run with `ctest --test-dir out/build/<preset>` or `just test-debug`/`test-release`):
figure encode/decode round-trips, the catalog scanner, the `SoftwarePortal` protocol state machine
driven by scripted report sequences, and Xbox frame conversion. Anything needing a real game dump,
a real figure, or real USB hardware is exercised by hand and is not part of the automated suite.
