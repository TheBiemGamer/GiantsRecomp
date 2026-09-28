# Reverse-engineering tooling setup (sub-project A)

## Purpose

Giants Recompiled is a static recompile (ReXGlue). No decompilation work has been done yet:
every function is still named `sub_ADDRESS`, both in the generated C++ and in
`config/default.toml`'s `[functions]` entries (used today only for codegen-correctness fixes,
never for naming).

This is the first of two sub-projects toward ultrawide support:

- **Sub-project A (this spec):** stand up a reverse-engineering workflow so functions can be
  identified and named, with names flowing into the recompiled C++ automatically.
- **Sub-project B (later, separate spec):** use that workflow to find and patch the camera
  FOV/aspect calculation and HUD anchoring code for ultrawide. Not designed here.

The workflow is meant to be reusable for any future patch (widescreen, HUD fixes, gameplay
tweaks), not a one-off hack for this one feature.

## Why this shape

`rom/default.xex` is a static recompile input pinned by SHA-256 (`src/xex_verify.cpp`) — the
build only runs against this exact executable. That means addresses found today stay valid for
as long as this project targets this build, with no drift to chase.

ReXGlue already has a rename pipeline, just aimed at IDA: `thirdparty/rexglue-sdk/scripts/ida/
export_named_funcs.py` reads/writes `config/default.toml`'s `[functions]` section, and
`thirdparty/rexglue-sdk/src/codegen/phase_register.cpp:552` uses a `name` field when present
(falling back to `sub_ADDRESS` only when it's empty). No code changes are needed in ReXGlue
itself — the TOML file is the only interface, and it's already tracked in git.

No Ghidra/IDA is installed on this machine. Ghidra was chosen over IDA Free specifically because
IDA Free has no PowerPC decompiler (disassembly only), while Ghidra's `PowerPC:BE:32:altivec`
processor module includes one, and Ghidra is free with no account/license step. The gap this
creates — no existing Ghidra-side export script — is closed by writing one project-side; no
ReXGlue-side changes are needed either way.

A maintained community Ghidra loader already handles Xbox 360 XEX loading (decrypt + LZX
decompress + turns `.pdata` into real Ghidra functions): XEXLoaderWV
(https://github.com/zeroKilo/XEXLoaderWV, also maintained at
https://github.com/madebr/XEXLoaderWV). No custom loader needs to be written.

## Headless / agentic operation (addendum)

Requirement added after the initial design: the whole RE loop — install, import, read a
function's decompiled code, name it, write the name back — must be runnable by an agent with no
GUI interaction and no developer in the loop for routine use. This changes the shape of
"Components" below from a GUI-driven script into a headless CLI toolchain:

- Nothing here is interactive. `askFile`/GUI dialogs are out; every script takes its arguments
  on the command line and is driven through Ghidra's `analyzeHeadless` (`analyzeHeadless.bat` on
  Windows), which supports project creation, import, and running scripts non-interactively
  (`-import`, `-process`, `-scriptPath`, `-postScript <script> <args...>`, all headless-safe).
- Setup (JDK, Ghidra, XEXLoaderWV) is also scripted end-to-end rather than documented as manual
  steps, since a dev doing it by hand once still breaks "no developer intervention" for anyone
  re-running this later (a fresh machine, CI, another agent). Ghidra extensions load from
  `<ghidra_install>/Extensions/Ghidra/<name>/` at startup with no separate "install" step beyond
  putting the unzipped extension there — both in GUI and headless mode — so this needs no
  Ghidra-side install command, just placing files correctly before the first `analyzeHeadless`
  run.
- All downloaded/generated tooling state (JDK, Ghidra, the Ghidra project/database for
  `default.xex`) lives under the already-git-ignored `logs/` folder, same convention as the
  project's existing local helper scripts — regenerable, never committed.

## Components

- **`tools/ghidra_re.ps1`** (new) — PowerShell wrapper, the single entry point for the whole
  workflow. Subcommands:
  - `setup` — downloads a JDK, Ghidra, and the XEXLoaderWV extension (latest GitHub releases,
    resolved at run time rather than pinned to a hardcoded version/URL) into `logs/toolchain/`.
  - `import` — one-time `analyzeHeadless -import rom/default.xex` into a project under
    `logs/ghidra_project/`, with full auto-analysis.
  - `dump <address>` — runs `tools/ghidra_dump_function.py` headless against the imported
    project; prints decompiled C, callers/callees, and referenced strings for one function, for
    an agent to read and reason about.
  - `rename <address> <name> [<address> <name> ...]` — runs
    `tools/ghidra_rename_and_export.py` headless: renames each function in the Ghidra database
    and syncs the result into `config/default.toml` in the same invocation.
- **`tools/ghidra_dump_function.py`** (new) — headless Ghidra post-script (no GUI API calls).
  Read-only.
- **`tools/ghidra_rename_and_export.py`** (new) — headless Ghidra post-script. Renames functions
  in the open program, then calls `tools/rexglue_toml_sync.py`'s `sync_functions` to update
  `config/default.toml`. Combines what would otherwise be a manual "rename in the GUI" step and
  a separate export step into one non-interactive call.
- **`tools/rexglue_toml_sync.py`** (new, unchanged by this addendum) — pure TOML-merge logic,
  no Ghidra dependency, unit tested. Reused by `ghidra_rename_and_export.py`.
- **`docs/reverse-engineering.md`** (new) — the headless CLI workflow: run `setup` once, `import`
  once per xex version, then `dump`/`rename` in a loop per function.

## Data flow

```
rom/default.xex (user's dump, git-ignored)
   -> tools/ghidra_re.ps1 setup   (JDK + Ghidra + XEXLoaderWV -> logs/toolchain/, one time)
   -> tools/ghidra_re.ps1 import  (-> logs/ghidra_project/, one time per xex version)
   -> tools/ghidra_re.ps1 dump <addr>      (agent reads decompiled C, decides a name)
   -> tools/ghidra_re.ps1 rename <addr> <name> [...]
        -> tools/ghidra_rename_and_export.py (headless): renames in the Ghidra DB,
           then calls tools/rexglue_toml_sync.py
   -> config/default.toml [functions]  (tracked in git — this is the deliverable)
   -> ReXGlue codegen (re-run)
   -> generated/ C++ (git-ignored, regenerated) shows real names instead of sub_ADDRESS
```

## What gets committed vs. not

Same line the project already draws for `generated/` and `rom/`:

- **Committed:** `config/default.toml` changes (address -> name/size metadata only — same
  category as the existing codegen-fix entries). `tools/ghidra_re.ps1`,
  `tools/ghidra_dump_function.py`, `tools/ghidra_rename_and_export.py`,
  `tools/rexglue_toml_sync.py` (+ its test). `docs/reverse-engineering.md`.
- **Not committed (git-ignored, all under the already-ignored `logs/`):** the downloaded JDK and
  Ghidra install (`logs/toolchain/`), the Ghidra project/database for `default.xex`
  (`logs/ghidra_project/`, `.gpr`/`.rep`) — both regenerable by re-running `setup`/`import`.
  `rom/default.xex` itself — already ignored. `generated/` — already ignored except
  `generated/rexglue.cmake`.

No disassembled/decompiled game code or binaries are ever committed — only the small metadata
(address, name, size) needed to drive ReXGlue's own codegen, which is the same thing the
existing `[functions]` entries already are.

## Testing / verification

`tools/rexglue_toml_sync.py` is pure logic and unit tested (stdlib `unittest`). The three
Ghidra-side pieces (`setup`/`import`/`dump`/`rename`) need a real Ghidra instance and are
verified by an end-to-end round-trip instead:

1. `tools/ghidra_re.ps1 setup`, then `tools/ghidra_re.ps1 import` — confirm both complete and
   `logs/ghidra_project/` contains an analyzed program.
2. `tools/ghidra_re.ps1 dump 0x82403BB8` — the game's portal-read wrapper, hooked in
   `src/hooks/portal_hook.cpp` and already fully understood per `docs/architecture.md`. Confirm
   it prints plausible decompiled C for that function.
3. `tools/ghidra_re.ps1 rename 0x82403BB8 XamInputNonControllerGetRaw_wrapper` — confirm it
   updates `config/default.toml` with the expected `name` entry and leaves all other entries
   untouched, and that running it again with the same input is a no-op (idempotent).
4. Re-run codegen, confirm the generated header uses the new name and the project still builds.

## Out of scope

- Finding or naming the ultrawide-relevant functions (camera/FOV/aspect/HUD) — sub-project B.
- Any change to ReXGlue itself (`thirdparty/rexglue-sdk/`) — vendored, not modified.
- A Ghidra loader for XEX — already solved by XEXLoaderWV.
- Switching to IDA — decided against; see "Why this shape".
