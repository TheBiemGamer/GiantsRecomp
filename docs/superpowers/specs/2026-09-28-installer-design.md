# Windows installer — design

## Goal

A Windows installer that gets a user from "I have the game disc/ISO" to "GiantRecomp is in my
Start Menu and configured" without them touching CMake, Visual Studio, or a terminal. Output is a
single `GiantRecompSetup.exe` a user downloads and runs.

## Policy change this depends on

Every sibling recomp project (Zelda64Recomp, UnleashedRecomp, the N64:Recomp family) ships a
prebuilt binary: the maintainer recompiles one fixed, version-pinned game build once, and that's
what the installer contains. Users only ever supply the game's *asset* files, never a compiler.

GiantRecomp already pins to one exact version (Skylanders Giants 1.0 USA/EU, checked by SHA-256 in
`src/xex_verify.cpp`), so recompilation of that version is deterministic — a maintainer-built
binary is exactly what any 1.0 owner would get building it themselves. This design adopts that
model. It is a real policy change: the current README's "no game code and no game data" claim
becomes "no game data" — the installer ships the recompiled game logic binary, same precedent as
the sibling projects above. It still ships none of the game's assets (audio/textures/video); the
user's own ROM always supplies those.

Consequence: **no local compiler, no CI.** The maintainer builds the release binary locally
(`just build-release`) against their own legally-owned ROM, exactly as today, and feeds that output
into the installer compiler by hand. No GitHub Actions pipeline needing access to the ROM.

## Scope

- **Windows only.** The Start Menu requirement is Windows-specific; Linux packaging (AppImage/deb)
  would be a separate future design with no equivalent ask.
- **Bundled binary, not downloaded.** The installer `.exe` contains the prebuilt
  `giantrecomp.exe` + runtime DLLs already; installing needs no network access.
- **ISO or pre-extracted folder**, both handled by the same installer.

## Components (new)

New top-level `installer/` directory:

- `GiantRecomp.iss` — the Inno Setup script: wizard pages, file staging, Start Menu/uninstall
  registration.
- `extract-xiso.exe` — vendored prebuilt binary of the existing open-source XISO extractor
  (already named in the README's credits) for unpacking ISO input. Not built from source as part
  of this project.
- A small standalone CLI wrapping the existing `xex_verify.cpp` fingerprint check, built once by
  the maintainer alongside the release binary, so the Inno script can validate a candidate
  `default.xex` without needing the full game runtime.

## Why Inno Setup

Needs: a ROM file/folder picker, a progress step for ISO extraction, a small settings form, and
Start Menu + uninstall registration. Inno Setup's Pascal-scripted custom wizard pages cover all of
this, compiles to one self-contained `.exe` with no runtime dependency for the user, and is the
standard tool for this exact job (small Windows game/app installers). NSIS covers the same ground
but its scripting is clunkier for a custom settings page; a fully custom native GUI would mean
reimplementing Start Menu/uninstall registration that Inno already provides.

## Install location

`{localappdata}\Programs\GiantRecomp` by default — **not** Program Files. `giantsrecomp.toml`
lives next to the executable and the in-game F4 overlay writes to it live during play; a
Program-Files install would hit UAC virtualization on those writes (silently redirecting them
somewhere else, breaking later reads). A per-user location needs no admin elevation and the F4
writes just work, matching how e.g. VS Code or Discord install by default for the same reason.
Saves/shader cache stay at the existing default (`Documents\giantsrecomp` / `user_data_root`),
untouched by the installer regardless of install location.

## Wizard flow

1. **Welcome.**
2. **Info/disclaimer** — no game data included, user needs their own legal Skylanders Giants 1.0
   USA/EU copy, the ROM is fingerprint-checked before anything is extracted.
3. **Install location** — defaults to the per-user path above; advanced users can change it.
4. **ROM source** — radio choice: "ISO file" (file picker) or "Already-extracted folder" (folder
   picker).
5. **Fast fingerprint check** — pulls just `default.xex` out first (via `extract-xiso` for ISO
   input, or read directly for a folder) and runs it through the fingerprint CLI *before*
   committing to a multi-GB extraction/copy. A version mismatch aborts here with a clear message
   naming the required version, not after a multi-minute wait.
6. **Extraction/copy progress** — ISO input runs a full `extract-xiso` pass into
   `<install>\rom\`; folder input copies into the same location. Always lands in the install's
   `rom\`, even for an already-extracted folder — one consistent on-disk layout
   (`rom\default.xex` next to the exe, matching current docs) rather than a launch flag pointing
   elsewhere. Costs extra disk only in the already-extracted case; ISO input needs a fresh
   extraction into *some* directory regardless.
7. **Essentials settings** — `portal_mode` dropdown (software/usb/none), `resolution` combo,
   `resolution_scale` spinner. Nothing else: the F4 in-game overlay already edits every setting in
   `giantsrecomp.toml` live, so duplicating the whole file into the wizard would just be a second,
   more cumbersome copy of that overlay.
8. **Ready/summary.**
9. **Install** — copies `giantrecomp.exe` + runtime DLLs, writes `giantsrecomp.toml` from
   `giantsrecomp.toml.example` with the wizard's answers substituted in, creates a Start Menu group
   ("GiantRecomp" launch shortcut + "Uninstall GiantRecomp").
10. **Finish** — optional "launch now" checkbox.

## On-disk layout

```
<install>/
  giantrecomp.exe
  *.dll                  (runtime shared libs, as today's package-release copies)
  giantsrecomp.toml      (generated from the template + wizard answers)
  rom/                   (extracted or copied game files, default.xex etc.)
  unins000.exe           (Inno's generated uninstaller)
```

Saves, shader cache, and the default figures folder remain wherever `user_data_root` already
points (`Documents\giantsrecomp` by default) — outside the install directory, unaffected by
install/update/uninstall.

## Update runs

Inno registers the install under a stable AppId, so re-running the installer over an existing
install is detected. If `<install>\rom\default.xex` already passes the fingerprint check:

- Skip wizard pages 4-6 (ROM source, fingerprint check, extraction) entirely.
- Skip page 7 (settings) — leave the existing `giantsrecomp.toml` untouched.
- Only replace `giantrecomp.exe` and the runtime DLLs.

This preserves the ROM, all user settings (including anything changed later via F4), and saves
(which were never in the install directory to begin with).

## Uninstall

Inno's generated uninstaller removes only what the installer wrote: the executable and DLLs. It
does **not** delete `rom\` (several GB of the user's own game data) or `giantsrecomp.toml`
(user-edited via F4) by default. A custom uninstall page adds an opt-in checkbox, "Also delete
extracted ROM files and settings," for a user who wants a clean removal. Saves under
`user_data_root` are never touched by the uninstaller either way, matching how the installer never
touches them.

## Error handling

- **Fingerprint mismatch**: abort at step 5, before any extraction/copy, with a message naming the
  required version (1.0 USA/EU) — reusing `DescribeXexProblem`'s existing messaging from
  `xex_verify.cpp` rather than inventing new copy.
- **Low disk space**: before extraction, compare free space at the install location against an
  estimated required size (~7-8 GB for the extracted disc plus the installer payload) and warn
  before proceeding.
- **`extract-xiso` failure** (corrupt/truncated ISO): surface its exit code/stderr as the
  installer's error text rather than a generic failure.

## Validation (no automated suite)

Matches the project's existing stance (`docs/build.md`): anything needing a real game dump or real
hardware is exercised by hand, not automated. Manual checklist before each release:

- Fresh, clean Windows VM (no VS/build tools/CMake present) — full install with an ISO, and
  separately with an already-extracted folder; confirm the game launches both ways.
- Wrong-version `default.xex` produces the early abort at step 5, not a failure after extraction.
- Re-running the installer over an existing install preserves `rom/`, `giantsrecomp.toml`, and
  saves, and only replaces the binary.
- Uninstall leaves `rom/` and the toml in place unless the opt-in checkbox is used; Documents
  saves are untouched regardless.
- Start Menu shortcut launches the game; an "Uninstall GiantRecomp" entry appears in both the
  Start Menu and Windows' Add/Remove Programs.
- Wizard's `portal_mode`/`resolution`/`resolution_scale` choices land correctly in the generated
  `giantsrecomp.toml`.

## Documentation restructure

Splitting `README.md` by audience, matching the existing `docs/` pattern
(`architecture.md`, `build.md`, `portal-protocol.md`, `figures.md`):

- **`README.md`** — player-facing only: pitch, what works/doesn't, download-and-run-the-installer
  instructions, settings summary (F4 overlay / toml), credits, license, AI-usage note. One link
  out to the dev doc for building from source instead.
- **`docs/development.md`** (new) — today's README "Setup" section (Windows/Linux from-source
  build, submodules, build tool requirements) and the "For developers" pointers section. Kept
  separate from `docs/build.md`, which stays scoped to codegen fixes and the unit test suite —
  different audience (a contributor fixing a codegen bug vs. someone building from source to play).
- **`docs/releasing.md`** (new) — `just build-release`/`package-release`, compiling
  `installer/GiantRecomp.iss`, uploading to GitHub Releases by hand, and the pre-publish checklist
  from the Validation section above.
- README's "This repository contains no game code and no game data" line becomes "no game data" —
  the installer now ships the recompiled game logic binary; only the game's own assets are excluded
  and must come from the user's own copy.

## Out of scope

- CI/automated builds of the installer (explicitly deferred — manual release process for now).
- Linux packaging.
- Code signing / SmartScreen reputation (installer will show an "unknown publisher" warning on
  first releases; not addressed here).
- Full settings coverage in the wizard (F4 overlay already covers everything else post-install).
