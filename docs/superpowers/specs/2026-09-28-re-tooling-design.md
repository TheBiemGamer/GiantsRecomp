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

## Components

- **Ghidra + XEXLoaderWV extension** — local install, not part of this repo. Opens
  `rom/default.xex` directly with correct base addresses and function boundaries.
- **`tools/ghidra_export_named_funcs.py`** (new) — a Ghidra script, modeled on ReXGlue's IDA
  script, that:
  - Reads `config/default.toml`, finds the `[functions]` section.
  - For each named, non-default (not `FUN_*`/thunk/etc.) function in the current Ghidra
    program, writes/updates a `0xADDRESS = { name = "...", size = 0x... }` entry.
  - Preserves entries that already exist for other reasons (the current codegen-correctness
    `parent`/`size` fixes) — merge, not replace.
  - Matches the existing TOML entry formatting so diffs stay small and reviewable.
- **`docs/reverse-engineering.md`** (new) — install steps (Ghidra, XEXLoaderWV), loading the
  xex, naming convention, running the export script, then rebuilding codegen to pick up new
  names.

## Data flow

```
rom/default.xex (user's dump, git-ignored)
   -> Ghidra (via XEXLoaderWV), local, not committed
   -> analyst renames functions as they're understood
   -> tools/ghidra_export_named_funcs.py
   -> config/default.toml [functions]  (tracked in git — this is the deliverable)
   -> ReXGlue codegen (re-run)
   -> generated/ C++ (git-ignored, regenerated) shows real names instead of sub_ADDRESS
```

## What gets committed vs. not

Same line the project already draws for `generated/` and `rom/`:

- **Committed:** `config/default.toml` changes (address -> name/size metadata only — same
  category as the existing codegen-fix entries). `tools/ghidra_export_named_funcs.py`.
  `docs/reverse-engineering.md`.
- **Not committed (git-ignored):** Ghidra project files (`.gpr`/`.rep`) — regenerable from the
  xex, purely local. `rom/default.xex` itself — already ignored. `generated/` — already ignored
  except `generated/rexglue.cmake`.

No disassembled/decompiled game code or binaries are ever committed — only the small metadata
(address, name, size) needed to drive ReXGlue's own codegen, which is the same thing the
existing `[functions]` entries already are.

## Testing / verification

This is dev tooling, not runtime code — no unit tests apply. Verification is a round-trip check:

1. In Ghidra, rename one function whose behavior is already fully understood from
   `docs/architecture.md` — `sub_82403BB8` (the game's portal-read wrapper hooked in
   `src/hooks/portal_hook.cpp`) is a good candidate: known, small, already documented.
2. Run `tools/ghidra_export_named_funcs.py`, confirm it writes the expected `name` entry into
   `config/default.toml` and leaves all other entries untouched.
3. Re-run codegen, confirm the generated header uses the new name and the project still builds
   (`just build-debug` or equivalent).

## Out of scope

- Finding or naming the ultrawide-relevant functions (camera/FOV/aspect/HUD) — sub-project B.
- Any change to ReXGlue itself (`thirdparty/rexglue-sdk/`) — vendored, not modified.
- A Ghidra loader for XEX — already solved by XEXLoaderWV.
- Switching to IDA — decided against; see "Why this shape".
