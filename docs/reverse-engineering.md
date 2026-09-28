# Reverse engineering

How to work out what a function in `default.xex` does, give it a real name, and get that name
into the recompiled C++. Everything runs from the command line; you don't need to open the
Ghidra GUI.

## Why this exists

Giants Recompiled is a *static* recompile (ReXGlue): every function starts out named
`sub_ADDRESS`, in both `config/default.toml` and the generated C++, because nothing has looked
at what the function actually does yet. This workflow lets you identify a function's purpose and
get a real name into the next `generated/` rebuild. No ReXGlue changes are needed; the only manual
part is deciding what to call the function.

`rom/default.xex` is pinned by SHA-256 (`src/xex_verify.cpp`) — the build only runs against
this exact executable, so addresses found today stay valid for as long as this project targets
this build.

## One-time setup

```
pwsh tools/ghidra_re.ps1 setup    # downloads a JDK, Ghidra, XEXLoaderWV, and PyGhidra into logs/toolchain/
pwsh tools/ghidra_re.ps1 import   # imports + auto-analyzes rom/default.xex into logs/ghidra_project/
```

Both are safe to re-run; `setup` skips anything already downloaded, `import`
re-imports into the same project.

Ghidra's `.py` GhidraScript provider (as of Ghidra 12) requires PyGhidra, a standalone Python
library that starts its own embedded JVM — there's no more Jython fallback for headless `.py`
scripts. Because of that, `dump` and `rename` below don't go through `analyzeHeadless
-postScript`. They're plain `python3` scripts that call PyGhidra's API directly
(`pyghidra.open_project()` / `pyghidra.program_context()`), which is simpler and avoids an
extra subprocess. Only `import` still uses `analyzeHeadless`, since that's a one-shot
import-and-analyze operation PyGhidra doesn't need to wrap.

## Workflow

1. `pwsh tools/ghidra_re.ps1 dump <address>` — prints the function's decompiled C, its
   callers/callees, and any referenced string literals. Read this to work out what the
   function does.
2. `pwsh tools/ghidra_re.ps1 rename <address> <name> [<address> <name> ...]` — renames the
   function(s) in Ghidra's database and updates `config/default.toml`'s `[functions]` section in
   the same call. It prints how many entries were updated or added, and any name conflicts.
3. `git diff config/default.toml` — should show only the entries for the functions you just
   renamed.
4. Re-run codegen and rebuild to confirm the new names show up in `generated/` and the project
   still builds: `just regen-debug` (see `docs/build.md` for what codegen fixes are and how
   they're tested).
5. Add a one-line comment above any newly-added entry explaining what the function is and how
   it was identified, matching the convention already used throughout
   `config/default.toml`'s `[functions]` section. The exporter only merges `name` and `size`, so you
   have to write this comment yourself.
6. Commit `config/default.toml` (address-to-name metadata only). Never commit `rom/`,
   `generated/` (except `generated/rexglue.cmake`), or anything under `logs/`: that includes the
   downloaded JDK and Ghidra (`logs/toolchain/`) and the Ghidra project for `default.xex`
   (`logs/ghidra_project/`). Re-running `setup` and `import` recreates them.

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
  Ghidra\`, which only holds installable-but-inactive zips (a module placed in the top-level folder
  never gets loaded). Also check
  the extension's `extension.properties` `version=` line matches the installed Ghidra version
  exactly, or it's silently excluded.
- **Conflicts reported by `rename`**: two different addresses were given the same name. Ghidra
  function names are unique per-program anyway, so this usually means a stale name on the
  *other* address — check it with `dump` and rename it to something else, or re-run `rename`
  with a different name.
- **`cmake --preset ...` (step 4 above) fails to find `clang`/`clang++`**: an
  intermittent problem in this project's build environment, unrelated to this workflow. Retry
  through `just` (`just clean-debug` then `just configure-debug`/`just regen-debug`) rather than
  calling `cmake` directly — `just`'s recipes export the VS Clang tools directory onto `PATH`,
  which a direct `cmake` call skips.
