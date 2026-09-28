# Reverse engineering

How to identify functions in `default.xex` and get their names into the recompiled C++ — fully
headless, no GUI, drivable by an agent with no developer in the loop.

## Why this exists

Giants Recompiled is a *static* recompile (ReXGlue): every function starts out named
`sub_ADDRESS`, in both `config/default.toml` and the generated C++, because nothing has looked
at what the function actually does yet. This workflow identifies a function's purpose and gets
a real name flowing into the next `generated/` rebuild automatically — no ReXGlue changes
needed, no GUI, nothing manual beyond deciding what a function should be called.

`rom/default.xex` is pinned by SHA-256 (`src/xex_verify.cpp`) — the build only runs against
this exact executable, so addresses found today stay valid for as long as this project targets
this build.

## One-time setup

```
pwsh tools/ghidra_re.ps1 setup    # downloads a JDK, Ghidra, XEXLoaderWV, and PyGhidra into logs/toolchain/
pwsh tools/ghidra_re.ps1 import   # imports + auto-analyzes rom/default.xex into logs/ghidra_project/
```

Both are idempotent — safe to re-run; `setup` skips anything already downloaded, `import`
re-imports into the same project.

Ghidra's `.py` GhidraScript provider (as of Ghidra 12) requires PyGhidra, a standalone Python
library that starts its own embedded JVM — there's no more Jython fallback for headless `.py`
scripts. Because of that, `dump` and `rename` below don't go through `analyzeHeadless
-postScript` at all: they're plain `python3` scripts that call PyGhidra's API directly
(`pyghidra.open_project()` / `pyghidra.program_context()`), which is both simpler and avoids an
extra subprocess. Only `import` still uses `analyzeHeadless`, since that's a one-shot
import-and-analyze operation PyGhidra doesn't need to wrap.

## Workflow

1. `pwsh tools/ghidra_re.ps1 dump <address>` — prints the function's decompiled C, its
   callers/callees, and any referenced string literals. Read this to figure out what the
   function does.
2. `pwsh tools/ghidra_re.ps1 rename <address> <name> [<address> <name> ...]` — renames the
   function(s) in Ghidra's database and syncs `config/default.toml`'s `[functions]` section in
   the same call. Prints how many entries were updated/added and any name conflicts.
3. `git diff config/default.toml` — should show only the entries for functions just renamed,
   nothing else touched.
4. Re-run codegen and rebuild to confirm the new names show up in `generated/` and the project
   still builds: `just regen-debug` (see `docs/build.md` for what codegen fixes are and how
   they're tested).
5. Add a one-line comment above any newly-added entry explaining what the function is and how
   it was identified, matching the convention already used throughout
   `config/default.toml`'s `[functions]` section (the exporter itself only merges `name`/`size`,
   it doesn't write comments — that's a manual step after the fact).
6. Commit `config/default.toml` (metadata only — never commit `rom/`, `generated/`, or anything
   under `logs/`; see the "What gets committed" section of
   `docs/superpowers/specs/2026-09-28-re-tooling-design.md`).

Not every address is auto-discovered as a function by Ghidra's default analysis — the same way
`config/default.toml`'s existing `[functions]` entries exist because ReXGlue's own analyzer
misses functions reached only through indirect/vtable calls. `dump` on an address Ghidra hasn't
disassembled will fail with "No function at ...". Those need a manual disassemble/create-function
step in Ghidra first; not covered by this workflow.

## Naming convention

Use the function's actual purpose in plain PascalCase or snake_case (whichever reads more
naturally for that function) — no required prefix. If a name collision comes up between two
genuinely different functions, disambiguate by module/subsystem (e.g. `Camera_UpdateFov` vs.
`Hud_UpdateFov`) rather than adding a blanket prefix to everything.

## Troubleshooting

- **`setup` fails to find a release asset**: GitHub's release asset naming for Ghidra or
  XEXLoaderWV changed. Check the actual latest release on GitHub and adjust the `-match` pattern
  in `Invoke-Setup` in `tools/ghidra_re.ps1`.
- **`import` doesn't pick up the XEX loader automatically**: add `-loader <LoaderClassName>`
  (see XEXLoaderWV's README for the exact class name) to the `analyzeHeadless` call in
  `Invoke-Import`.
- **An extension you add later doesn't seem to load**: Ghidra's *active* module path is
  `<ghidra_install>\Ghidra\Extensions\<name>\` — not the top-level `<ghidra_install>\Extensions\
  Ghidra\`, which only holds installable-but-inactive zips (confirmed the hard way while building
  this workflow: a module placed in the top-level folder never reached the loader). Also check
  the extension's `extension.properties` `version=` line matches the installed Ghidra version
  exactly, or it's silently excluded.
- **Conflicts reported by `rename`**: two different addresses were given the same name. Ghidra
  function names are unique per-program anyway, so this usually means a stale name on the
  *other* address — check it with `dump` and rename it to something else, or re-run `rename`
  with a different name.
- **`cmake --preset ...` (step 4 above) fails to find `clang`/`clang++`**: pre-existing,
  intermittent flakiness in this project's build environment, unrelated to this workflow. Retry
  through `just` (`just clean-debug` then `just configure-debug`/`just regen-debug`) rather than
  calling `cmake` directly — `just`'s recipes export the VS Clang tools directory onto `PATH`,
  which a direct `cmake` call skips.
