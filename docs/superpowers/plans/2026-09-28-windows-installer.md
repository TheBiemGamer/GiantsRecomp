# Windows Installer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a single `GiantRecompSetup.exe` that installs GiantRecomp from a prebuilt binary, ingests the user's own ROM (ISO or already-extracted folder), writes a configured `giantsrecomp.toml`, and adds a Start Menu entry — no compiler required on the user's machine.

**Architecture:** An Inno Setup script (`installer/GiantRecomp.iss`) drives a wizard with custom Pascal-scripted pages. It stages a locally-built release (`giantrecomp.exe` + runtime DLLs, built by `just build-release` against the maintainer's own legally-owned ROM) plus two small helper tools: a new `giantrecomp_xexcheck` CLI (wraps the existing `xex_verify.cpp` fingerprint check so the wizard can validate a candidate ROM before committing to extraction) and a vendored prebuilt `extract-xiso.exe` (for ISO input). No CI: the maintainer runs `just package-installer` locally to produce the installer, and uploads it to GitHub Releases by hand.

**Tech Stack:** C++23 (xexcheck CLI, reuses `giantrecomp_verify` from `CMakeLists.txt`), Inno Setup 6 (Pascal Script) for the installer, `extract-xiso` (vendored prebuilt binary) for ISO extraction, Windows' built-in `robocopy.exe` for folder-to-folder copying.

**Spec:** `docs/superpowers/specs/2026-09-28-installer-design.md`

## Global Constraints

- Windows only — no Linux packaging in this plan (spec's Scope section).
- No CI — the release binary and the installer are both built locally by the maintainer (spec's "Policy change" and "Out of scope" sections).
- Installer bundles the prebuilt binary; installing needs no network access (spec's Scope section).
- Install location defaults to `{localappdata}\Programs\GiantRecomp`, never Program Files — the F4 overlay writes `giantsrecomp.toml` live during play and must never hit UAC virtualization (spec's "Install location" section).
- The settings wizard page exposes only `portal_mode`, `resolution`, and `resolution_scale` — every other setting stays at its default, editable later via the in-game F4 overlay (spec's wizard step 7).
- The ROM is always copied/extracted into `<install>\rom\`, never referenced in place, even for an already-extracted folder (spec's wizard step 6).
- The fingerprint check runs before any extraction/copy, against just `default.xex` pulled out first (spec's wizard step 5).
- Update runs must preserve an existing `rom\`, `giantsrecomp.toml`, and saves — only the binary/DLLs get replaced when the existing `rom\default.xex` already passes the fingerprint check (spec's "Update runs" section).
- The uninstaller never deletes `rom\` or `giantsrecomp.toml` by default; deleting them is opt-in only (spec's "Uninstall" section).
- No automated test suite for the Inno Setup script itself — compiling with `ISCC` and manual verification are this project's existing precedent for anything needing a real install/real hardware (`docs/build.md`).
- `extract-xiso.exe` is vendored prebuilt, never built from source in this repo; its license/attribution travels with it and gets a credits line in `README.md`.

## Review Focus

- **ROM folder picked as the install's own `rom\`** (e.g. re-running the installer and pointing the folder picker at itself): copying a directory into itself can corrupt or hang. Task 5 rejects this before copying.
- **Insufficient free disk space** for a ~7-8 GB extraction/copy: Task 5 checks free space against an estimate before starting and stops with a clear message instead of failing partway through.
- **Update run where the installed `giantsrecomp.toml` is missing** (user deleted it by hand): Task 7's "leave the toml untouched" logic must fall back to writing the default template instead of assuming the file exists.
- **Silent/unattended uninstall** (`unins000.exe /VERYSILENT`): Task 7's opt-in delete confirmation must never block waiting for a dialog nobody can answer — it must default to "do not delete" when running silently.
- **Non-ASCII or space-containing paths** for the ROM source or the install directory (already a documented sensitivity in this codebase — see `tests/xex_verify_test.cpp`'s explicit odd-path cases): every `Exec()` call built in Tasks 4-5 quotes its path arguments.

---

## File Structure

New files:
- `src/tools/xex_check_exit_code.h` — pure enum + mapping function, unit-testable without spawning a process.
- `src/tools/xex_check_cli.cpp` — thin `main()` wrapping the existing `giantrecomp::VerifyXex`/`DescribeXexProblem` plus the new exit-code mapping.
- `tests/xex_check_exit_code_test.cpp` — tests the pure mapping function.
- `installer/extract-xiso.exe` — vendored prebuilt binary.
- `installer/extract-xiso.LICENSE.txt` — its license text, for redistribution.
- `installer/settings_template.toml` — a copy of `giantsrecomp.toml.example` with three values replaced by installer placeholder tokens.
- `installer/disclaimer.txt` — plain-text shown as Inno's built-in pre-install info page.
- `installer/GiantRecomp.iss` — the Inno Setup script (grown incrementally across Tasks 4-7).
- `docs/development.md` — building from source (moved out of `README.md`).
- `docs/releasing.md` — cutting a release (new content).

Modified files:
- `CMakeLists.txt` — adds the `giantrecomp_xexcheck` executable target and its test.
- `justfile` — adds a `package-installer` recipe.
- `.gitignore` — ignores the installer's staging/output directories.
- `README.md` — trimmed to player-facing content, "no game code and no game data" becomes "no game data", installer download instructions added.
- `docs/build.md` — one cross-reference line added, matching `docs/architecture.md`'s existing "See also" style.

---

### Task 1: xex-check CLI exit codes

**Files:**
- Create: `src/tools/xex_check_exit_code.h`
- Create: `tests/xex_check_exit_code_test.cpp`
- Modify: `CMakeLists.txt:86-102` (add the test target near the existing `giantrecomp_verify`/`giantrecomp_tests` block)

**Interfaces:**
- Consumes: `giantrecomp::XexCheck` (enum in `src/xex_verify.h:10-15`, values `Match`/`Mismatch`/`Unreadable`/`BadExpected`).
- Produces: `giantrecomp::XexCheckExitCode` enum (`kExitMatch = 0`, `kExitMismatch = 2`, `kExitUnreadable = 3`, `kExitBadExpected = 4`) and `int giantrecomp::ExitCodeForStatus(XexCheck)`, both consumed by Task 2's `xex_check_cli.cpp` and by the Inno script's `Exec()` result-code checks in Task 4.

- [ ] **Step 1: Write the failing test**

Create `tests/xex_check_exit_code_test.cpp`:

```cpp
#include "tools/xex_check_exit_code.h"

#include <cstdio>

using giantrecomp::ExitCodeForStatus;
using giantrecomp::XexCheck;

static int g_failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

int main() {
  CHECK(ExitCodeForStatus(XexCheck::Match) == 0);
  CHECK(ExitCodeForStatus(XexCheck::Mismatch) == 2);
  CHECK(ExitCodeForStatus(XexCheck::Unreadable) == 3);
  CHECK(ExitCodeForStatus(XexCheck::BadExpected) == 4);

  if (g_failures == 0) std::puts("all xex_check_exit_code tests passed");
  return g_failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Wire a test target and confirm it fails to build**

In `CMakeLists.txt`, immediately after the existing block that ends `add_test(NAME xex_verify COMMAND giantrecomp_tests)` (around line 102), add:

```cmake
add_executable(xex_check_exit_code_test tests/xex_check_exit_code_test.cpp)
target_include_directories(xex_check_exit_code_test PRIVATE src)
add_test(NAME xex_check_exit_code COMMAND xex_check_exit_code_test)
```

Run: `cmake --preset win-amd64-debug && cmake --build --preset win-amd64-debug --target xex_check_exit_code_test`
Expected: FAIL — `tools/xex_check_exit_code.h: No such file or directory` (the header doesn't exist yet).

- [ ] **Step 3: Write the header to make it pass**

Create `src/tools/xex_check_exit_code.h`:

```cpp
#pragma once

#include "xex_verify.h"

namespace giantrecomp {

// Exit codes for the giantrecomp_xexcheck CLI. The installer's Inno Setup script reads these via
// Exec()'s ResultCode to decide whether to proceed past the fingerprint-check wizard step.
enum XexCheckExitCode {
  kExitMatch = 0,
  kExitMismatch = 2,
  kExitUnreadable = 3,
  kExitBadExpected = 4,
};

inline int ExitCodeForStatus(XexCheck status) {
  switch (status) {
    case XexCheck::Match:
      return kExitMatch;
    case XexCheck::Mismatch:
      return kExitMismatch;
    case XexCheck::Unreadable:
      return kExitUnreadable;
    case XexCheck::BadExpected:
      return kExitBadExpected;
  }
  return kExitBadExpected;
}

}  // namespace giantrecomp
```

- [ ] **Step 4: Build and run to verify it passes**

Run: `cmake --build --preset win-amd64-debug --target xex_check_exit_code_test && ctest --test-dir out/build/win-amd64-debug -R xex_check_exit_code --output-on-failure`
Expected: PASS — `all xex_check_exit_code tests passed`.

- [ ] **Step 5: Commit**

```bash
git add src/tools/xex_check_exit_code.h tests/xex_check_exit_code_test.cpp CMakeLists.txt
git commit -m "feat: add exit-code mapping for the upcoming xex-check CLI"
```

---

### Task 2: xex-check CLI binary

**Files:**
- Create: `src/tools/xex_check_cli.cpp`
- Modify: `CMakeLists.txt:96` area (add the `giantrecomp_xexcheck` executable target, after `giantrecomp_verify` is defined)

**Interfaces:**
- Consumes: `giantrecomp::VerifyXex`, `giantrecomp::DescribeXexProblem` (`src/xex_verify.h`), `giantrecomp::ExitCodeForStatus` (Task 1), `GIANTRECOMP_XEX_SHA256` (compile definition already computed in `CMakeLists.txt:77-84` from `docs/game/default.xex.sha256`).
- Produces: `giantrecomp_xexcheck.exe <path-to-default.xex>` — prints `OK` and exits 0 on a match; prints the human-readable problem to stderr and exits 2/3/4 otherwise. Consumed by the Inno script in Task 4.

- [ ] **Step 1: Write the CLI**

Create `src/tools/xex_check_cli.cpp`:

```cpp
#include "tools/xex_check_exit_code.h"
#include "xex_verify.h"

#include <cstdio>
#include <filesystem>

#ifndef GIANTRECOMP_XEX_SHA256
#error "GIANTRECOMP_XEX_SHA256 must be defined by the build (see CMakeLists.txt)"
#endif

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <path-to-default.xex>\n",
                 argc > 0 ? argv[0] : "giantrecomp_xexcheck");
    return giantrecomp::kExitBadExpected;
  }

  const std::filesystem::path xex(argv[1]);
  const auto result = giantrecomp::VerifyXex(xex, GIANTRECOMP_XEX_SHA256);
  const auto problem = giantrecomp::DescribeXexProblem(xex, result, GIANTRECOMP_XEX_SHA256);

  if (problem.empty()) {
    std::puts("OK");
    return giantrecomp::kExitMatch;
  }
  std::fputs(problem.c_str(), stderr);
  std::fputc('\n', stderr);
  return giantrecomp::ExitCodeForStatus(result.status);
}
```

- [ ] **Step 2: Wire the CMake target**

In `CMakeLists.txt`, immediately after the `add_test(NAME xex_verify COMMAND giantrecomp_tests)` line (and after the Task 1 test target you just added), add:

```cmake
add_executable(giantrecomp_xexcheck src/tools/xex_check_cli.cpp)
target_link_libraries(giantrecomp_xexcheck PRIVATE giantrecomp_verify)
target_compile_definitions(giantrecomp_xexcheck PRIVATE GIANTRECOMP_XEX_SHA256="${GIANTRECOMP_XEX_SHA256}")
```

- [ ] **Step 3: Build it**

Run: `cmake --build --preset win-amd64-release --target giantrecomp_xexcheck`
Expected: builds cleanly, produces `out/build/win-amd64-release/giantrecomp_xexcheck.exe`.

- [ ] **Step 4: Manually verify both outcomes**

Run: `out\build\win-amd64-release\giantrecomp_xexcheck.exe rom\default.xex` (assuming your `rom/` has a valid dump)
Expected: prints `OK`, exit code 0 (check with `echo %ERRORLEVEL%` in cmd, or `echo $LASTEXITCODE` in PowerShell).

Run: `out\build\win-amd64-release\giantrecomp_xexcheck.exe README.md` (any wrong file)
Expected: prints a mismatch message to stderr, exits with code 2.

Run: `out\build\win-amd64-release\giantrecomp_xexcheck.exe does-not-exist.xex`
Expected: prints "Cannot read..." to stderr, exits with code 3.

- [ ] **Step 5: Commit**

```bash
git add src/tools/xex_check_cli.cpp CMakeLists.txt
git commit -m "feat: add giantrecomp_xexcheck CLI for installer ROM validation"
```

---

### Task 3: Vendor extract-xiso and create the installer skeleton

**Files:**
- Create: `installer/extract-xiso.exe`
- Create: `installer/extract-xiso.LICENSE.txt`
- Create: `installer/settings_template.toml`
- Modify: `README.md` (credits section, one line)

**Interfaces:**
- Produces: `installer/extract-xiso.exe` (invoked by Task 5's Pascal Script via `Exec`), `installer/settings_template.toml` with placeholder tokens `__PORTAL_MODE__`, `__RESOLUTION__`, `__RESOLUTION_SCALE__` (consumed by Task 6's toml-generation code).

- [ ] **Step 1: Download and vendor extract-xiso**

Download a Windows release build of `extract-xiso` from its project releases (the same tool already named in `README.md`'s credits, under `https://github.com/XboxDev/extract-xiso` or wherever the current maintained fork's releases page is — check `README.md`'s existing credits line for the exact project link before downloading, and pick the newest tagged Windows release, not a development build). Place the executable at `installer/extract-xiso.exe` and its license file (from that project's repository) at `installer/extract-xiso.LICENSE.txt`.

Run: `installer\extract-xiso.exe -h`
Expected: prints its usage/help text. Read it now and note the exact flags for "extract to a directory" — Task 5 assumes `-x` (extract mode) and `-d <dir>` (destination directory) based on this tool's commonly documented interface, but confirm against this exact vendored build's own `-h` output before Task 5, and adjust Task 5's `Exec` parameters if this build's flags differ.

- [ ] **Step 2: Create the settings template**

Create `installer/settings_template.toml` by copying `giantsrecomp.toml.example` and replacing exactly three values with placeholder tokens (keep every comment line and every other setting untouched, so this stays easy to diff against the source file when it's updated):

```toml
# Copy next to giantrecomp.exe, rename to "giantsrecomp.toml". Full docs in README.md.
# The in-game Settings overlay (F4) reads and writes this same file.

["Portal"]
portal_mode = "__PORTAL_MODE__"           # "software", "usb" (real portal), or "none"
portal_figures_dir = ""                   # empty = "figures" next to your saves
# portal_figure = "C:\\path\\to\\Skylander.dump"  # loads this dump onto the portal at startup

["Graphics"]
resolution = "__RESOLUTION__"             # startup window size
resolution_scale = __RESOLUTION_SCALE__   # 1-8 supersample multiplier, 1 = off
frame_rate_limit = 0                      # 0 = unlimited
gpu_allow_invalid_fetch_constants = true  # works around a GPU quirk, on by default
# vulkan_device = -1                      # GPU index for multi-GPU systems, -1 = auto-pick

["UI"]
# ui_scale = 1.5                          # ImGui overlay font/widget scale

["Runtime"]
# user_data_root = "C:\\path\\to\\folder" # empty = Documents\giantsrecomp
```

Note the comment at the top of this task's file list: if `giantsrecomp.toml.example` gains or changes a setting later, update this template to match (everything except the three placeholder lines should stay identical).

- [ ] **Step 3: Add the credits line**

In `README.md`, in the Credits section (the bulleted list ending with the SkyReader/Marijn Kneppers line), add a new bullet:

```markdown
- **[extract-xiso](https://github.com/XboxDev/extract-xiso)** is vendored (prebuilt, unmodified) in `installer/` to unpack an Xbox 360 ISO during installation. It keeps its own license (`installer/extract-xiso.LICENSE.txt`).
```

(Use the exact project URL you downloaded from in Step 1 if it differs from the one above.)

- [ ] **Step 4: Commit**

```bash
git add installer/extract-xiso.exe installer/extract-xiso.LICENSE.txt installer/settings_template.toml README.md
git commit -m "feat: vendor extract-xiso and add the installer's settings template"
```

---

### Task 4: Inno Setup skeleton (no custom pages yet)

**Files:**
- Create: `installer/GiantRecomp.iss`
- Modify: `.gitignore`

**Interfaces:**
- Produces: `installer/Output/GiantRecompSetup.exe` when compiled with ISCC — an installer that places a hand-staged set of files, registers a Start Menu group, and runs, with no ROM/settings logic yet (added in Tasks 5-6).
- Consumes: files staged manually for this task's verification step (Task 8 automates the staging).

- [ ] **Step 1: Generate a stable AppId**

Run: `powershell -Command "[guid]::NewGuid()"`
Copy the output GUID. This value is used once, below, and must never change in any future version of this script (Inno uses it to recognize "this is the same app" across installer runs for update/uninstall detection).

- [ ] **Step 2: Write the disclaimer text**

Create `installer/disclaimer.txt` (shown as an automatic Inno wizard page before directory
selection — this is the spec's wizard step 2, "no game data included, need own legal copy"):

```
This installer does not include Skylanders Giants or any of its data.

You will need your own legal copy of the Xbox 360 disc, either as an ISO file or an
already-extracted folder. Only version 1.0 (USA or Europe) is supported -- the next step checks
this automatically before copying any files.
```

- [ ] **Step 3: Write the skeleton script**

Create `installer/GiantRecomp.iss` (replace `PASTE-YOUR-GUID-HERE` with the GUID from Step 1, keeping the braces):

```ini
#define MyAppName "GiantRecomp"
#define MyAppVersion "0.1.0"
#define MyAppExeName "giantrecomp.exe"

[Setup]
AppId={{PASTE-YOUR-GUID-HERE}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesInstallIn64BitMode=x64compatible
InfoBeforeFile=disclaimer.txt
OutputDir=Output
OutputBaseFilename=GiantRecompSetup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "staging\giantrecomp.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "staging\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "staging\giantrecomp_xexcheck.exe"; DestDir: "{tmp}"; Flags: dontcopy
Source: "settings_template.toml"; DestDir: "{tmp}"; Flags: dontcopy
Source: "extract-xiso.exe"; DestDir: "{tmp}"; Flags: dontcopy

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
```

A few notes on choices here, since a fresh reader won't have the context:
- `PrivilegesRequired=lowest` plus `DefaultDirName={localappdata}\...` is what makes this a no-admin, per-user install (spec's "Install location" section) — no UAC prompt, and the F4 overlay's live writes to `giantsrecomp.toml` next to the exe just work.
- `InfoBeforeFile=disclaimer.txt` is a built-in Inno mechanism: it shows the file's text as its own automatic wizard page before directory selection, no custom Pascal Script needed for this one.
- `giantrecomp_xexcheck.exe`, `settings_template.toml`, and `extract-xiso.exe` all use `Flags: dontcopy`: they're needed *during* the install (read/run from `{tmp}`) but shouldn't end up copied into `{app}` as part of the permanent install — Tasks 5-7's Pascal Script explicitly extracts each into `{tmp}` on demand via `ExtractTemporaryFile`, by exactly the filename given here.
- `skipifsourcedoesntexist` on the `*.dll` line: some release builds have no extra runtime DLLs to copy (matches the same conditional pattern already used in the justfile's `package-release` recipe).

- [ ] **Step 4: Stage a real build and compile**

You need Inno Setup 6 installed (download from jrsoftware.org, or `winget install JRSoftware.InnoSetup`). This step hand-stages what Task 8 will automate later.

```powershell
just build-release
New-Item -ItemType Directory -Force installer\staging
Copy-Item out\build\win-amd64-release\giantrecomp.exe installer\staging\
Copy-Item out\build\win-amd64-release\giantrecomp_xexcheck.exe installer\staging\
Copy-Item out\build\win-amd64-release\*.dll installer\staging\ -ErrorAction SilentlyContinue
```

Then compile: `& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\GiantRecomp.iss`
Expected: compiles with no errors, produces `installer\Output\GiantRecompSetup.exe`.

- [ ] **Step 5: Manually run and verify the skeleton install**

Run `installer\Output\GiantRecompSetup.exe`. Expected: the disclaimer page from Step 2 appears before directory selection, no UAC prompt, installs to `%LocalAppData%\Programs\GiantRecomp`, Start Menu gets a "GiantRecomp" group with both shortcuts, "Launch GiantRecomp" checkbox on finish works (it will fail to actually run correctly without a `rom\` folder yet — that's expected, later tasks add ROM handling; just confirm the exe attempts to launch).

Uninstall via the Start Menu's "Uninstall GiantRecomp" shortcut. Expected: removes the install directory cleanly.

- [ ] **Step 6: Ignore build output**

Add to `.gitignore` (after the existing `/dist` line):

```
/installer/staging/
/installer/Output/
```

- [ ] **Step 7: Commit**

```bash
git add installer/GiantRecomp.iss installer/disclaimer.txt .gitignore
git commit -m "feat: add Inno Setup skeleton for the Windows installer"
```

---

### Task 5: ROM source page, fingerprint check, and extraction/copy

**Files:**
- Modify: `installer/GiantRecomp.iss` (add `[Code]` section and a custom wizard page)

**Interfaces:**
- Consumes: `giantrecomp_xexcheck.exe` (Task 2, staged into `{tmp}` for this task's verification — Task 8 wires it permanently), `extract-xiso.exe` (Task 3/4).
- Produces: `{app}\rom\default.xex` (and the rest of the extracted/copied game files) by the time this page's `NextButtonClick` returns true. Consumed by Task 6 (settings page comes after this one) and Task 7 (update-run detection reads this same path).

- [ ] **Step 1: Add the custom page and its controls**

Add this to `installer/GiantRecomp.iss`, after the `[Run]` section:

```ini
[Code]
var
  RomPage: TWizardPage;
  RomIsIsoRadio, RomIsFolderRadio: TNewRadioButton;
  RomPathEdit: TNewEdit;
  RomBrowseButton: TNewButton;
  RomStatusLabel: TNewStaticText;
  RomProgressBar: TNewProgressBar;

procedure RomBrowseButtonClick(Sender: TObject);
var
  Path: String;
begin
  if RomIsIsoRadio.Checked then begin
    Path := '';
    if GetOpenFileName('Select the Skylanders Giants ISO', Path, '', 'ISO files|*.iso|All files|*.*', 'iso') then
      RomPathEdit.Text := Path;
  end else begin
    Path := '';
    if BrowseForFolder('Select the extracted game folder (must contain default.xex)', Path, False) then
      RomPathEdit.Text := Path;
  end;
end;

procedure InitializeWizard;
begin
  RomPage := CreateCustomPage(wpSelectDir, 'Game Files',
    'Locate your Skylanders Giants disc (version 1.0, USA or Europe)');

  RomIsIsoRadio := TNewRadioButton.Create(RomPage);
  RomIsIsoRadio.Parent := RomPage.Surface;
  RomIsIsoRadio.Caption := 'ISO file';
  RomIsIsoRadio.Checked := True;
  RomIsIsoRadio.Top := 0;
  RomIsIsoRadio.Width := RomPage.SurfaceWidth;

  RomIsFolderRadio := TNewRadioButton.Create(RomPage);
  RomIsFolderRadio.Parent := RomPage.Surface;
  RomIsFolderRadio.Caption := 'Already-extracted folder';
  RomIsFolderRadio.Top := RomIsIsoRadio.Top + RomIsIsoRadio.Height + 4;
  RomIsFolderRadio.Width := RomPage.SurfaceWidth;

  RomPathEdit := TNewEdit.Create(RomPage);
  RomPathEdit.Parent := RomPage.Surface;
  RomPathEdit.Top := RomIsFolderRadio.Top + RomIsFolderRadio.Height + 12;
  RomPathEdit.Width := RomPage.SurfaceWidth - 90;

  RomBrowseButton := TNewButton.Create(RomPage);
  RomBrowseButton.Parent := RomPage.Surface;
  RomBrowseButton.Caption := 'Browse...';
  RomBrowseButton.Left := RomPathEdit.Width + 8;
  RomBrowseButton.Top := RomPathEdit.Top - 2;
  RomBrowseButton.Width := 80;
  RomBrowseButton.OnClick := @RomBrowseButtonClick;

  RomStatusLabel := TNewStaticText.Create(RomPage);
  RomStatusLabel.Parent := RomPage.Surface;
  RomStatusLabel.Top := RomPathEdit.Top + RomPathEdit.Height + 16;
  RomStatusLabel.Width := RomPage.SurfaceWidth;
  RomStatusLabel.AutoSize := False;
  RomStatusLabel.WordWrap := True;
  RomStatusLabel.Height := 40;
  RomStatusLabel.Caption := '';

  RomProgressBar := TNewProgressBar.Create(RomPage);
  RomProgressBar.Parent := RomPage.Surface;
  RomProgressBar.Top := RomStatusLabel.Top + RomStatusLabel.Height + 8;
  RomProgressBar.Width := RomPage.SurfaceWidth;
  RomProgressBar.Visible := False;
end;
```

- [ ] **Step 2: Compile to check for syntax errors so far**

Run: `& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\GiantRecomp.iss`
Expected: compiles cleanly (the page exists but nothing calls it yet, and there's no logic behind the button — that's normal at this point). Fix any reported line/column error against Inno Setup's Pascal Scripting help (`Help > Pascal Scripting Reference` in the IDE) before moving on.

- [ ] **Step 3: Add the fingerprint check and extraction/copy logic**

Add these functions above `InitializeWizard` in the same `[Code]` section (they're called from Step 4's `NextButtonClick`, added next):

```pascal
function GetFreeSpaceMB(const Drive: String): Int64;
var
  Free, Total: Int64;
begin
  if not GetSpaceOnDisk64(Drive, Free, Total) then
    Free := 0;
  Result := Free div (1024 * 1024);
end;

// Copies SourceDir's contents into {app}\rom using robocopy. Robocopy's exit codes are a bitmask
// where 0-7 all mean success (e.g. 1 = "files copied") and only 8+ means a real failure -- a plain
// "ResultCode <> 0" check would wrongly treat a normal successful copy as an error.
function CopyRomFolder(const SourceDir: String; var ErrorMsg: String): Boolean;
var
  DestDir: String;
  ResultCode: Integer;
begin
  DestDir := ExpandConstant('{app}') + '\rom';
  if CompareText(AddBackslash(ExpandFileName(SourceDir)), AddBackslash(ExpandFileName(DestDir))) = 0 then begin
    ErrorMsg := 'The selected folder is the installed game folder itself. Choose your original extracted disc folder instead.';
    Result := False;
    Exit;
  end;
  ForceDirectories(DestDir);
  Exec(ExpandConstant('{cmd}'), '/C robocopy "' + SourceDir + '" "' + DestDir + '" /E /NFL /NDL /NJH /NJS /NC /NS /NP',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := ResultCode < 8;
  if not Result then
    ErrorMsg := 'Copying the game files failed (robocopy exit code ' + IntToStr(ResultCode) + ').';
end;

// -x extract mode, -d destination directory -- confirmed against this vendored build's own
// `extract-xiso -h` output in Task 3, Step 1.
function ExtractRomFromIso(const IsoPath: String; var ErrorMsg: String): Boolean;
var
  DestDir, ExtractXisoExe: String;
  ResultCode: Integer;
begin
  DestDir := ExpandConstant('{app}') + '\rom';
  ForceDirectories(DestDir);
  ExtractXisoExe := ExpandConstant('{tmp}') + '\extract-xiso.exe';
  ExtractTemporaryFile('extract-xiso.exe');
  Result := Exec(ExtractXisoExe, '-x -d "' + DestDir + '" "' + IsoPath + '"', '',
                 SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
  if not Result then
    ErrorMsg := 'Extracting the ISO into the install folder failed.';
end;

// Validates the candidate ROM and, for ISO input, performs the actual extraction as part of that
// validation. extract-xiso has no documented "list/extract one file" mode, so there is no cheap
// way to peek at an ISO's default.xex without extracting the whole disc -- unlike the folder-input
// case (SourcePath\default.xex can just be read in place, no copying needed to check it), ISO
// input's "preflight" IS the real extraction, done straight into {app}\rom. If the version check
// then fails, the partial extraction is rolled back with DelTree so no wrong-version game files
// are left behind. Callers must NOT call ExtractRomFromIso again after this returns True for ISO
// input -- it already happened here.
function PreflightCheckRom(const SourcePath: String; IsIso: Boolean; var ErrorMsg: String): Boolean;
var
  CandidateXex, XexCheckExe: String;
  ResultCode: Integer;
begin
  Result := False;

  if IsIso then begin
    if not ExtractRomFromIso(SourcePath, ErrorMsg) then Exit;
    CandidateXex := ExpandConstant('{app}') + '\rom\default.xex';
  end else begin
    CandidateXex := AddBackslash(SourcePath) + 'default.xex';
  end;

  if not FileExists(CandidateXex) then begin
    ErrorMsg := 'default.xex was not found. Make sure this is the extracted Skylanders Giants disc.';
    if IsIso then DelTree(ExpandConstant('{app}') + '\rom', True, True, True);
    Exit;
  end;

  XexCheckExe := ExpandConstant('{tmp}') + '\giantrecomp_xexcheck.exe';
  ExtractTemporaryFile('giantrecomp_xexcheck.exe');
  if not Exec(XexCheckExe, '"' + CandidateXex + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then begin
    ErrorMsg := 'Could not run the version-check tool.';
    if IsIso then DelTree(ExpandConstant('{app}') + '\rom', True, True, True);
    Exit;
  end;
  if ResultCode <> 0 then begin
    ErrorMsg := 'This is not the supported Skylanders Giants version (1.0, USA or Europe). ' +
      'The installer will not continue with an unsupported copy of the game.';
    if IsIso then DelTree(ExpandConstant('{app}') + '\rom', True, True, True);
    Exit;
  end;

  Result := True;
end;
```

- [ ] **Step 4: Wire it into the wizard's Next button**

Add this near the bottom of the `[Code]` section (Inno calls `NextButtonClick` automatically when it exists; returning `False` keeps the wizard on the current page):

```pascal
function NextButtonClick(CurPageID: Integer): Boolean;
var
  ErrorMsg: String;
  FreeMB: Int64;
begin
  Result := True;
  if CurPageID <> RomPage.ID then Exit;

  if Trim(RomPathEdit.Text) = '' then begin
    MsgBox('Choose your ISO file or extracted game folder first.', mbError, MB_OK);
    Result := False;
    Exit;
  end;

  // ~7 GB for the extracted disc, plus headroom -- checked before any extraction/copy starts (see
  // Review Focus in the plan: the installer must not fail partway through a multi-gigabyte
  // extraction). Checked here regardless of ISO vs. folder input, since for ISO input the
  // extraction happens inside PreflightCheckRom below, not in a separate later step.
  FreeMB := GetFreeSpaceMB(ExpandConstant('{app}'));
  if FreeMB < 8000 then begin
    MsgBox('Not enough free disk space. At least 8 GB free is needed; ' + IntToStr(FreeMB) +
      ' MB is available.', mbError, MB_OK);
    Result := False;
    Exit;
  end;

  RomStatusLabel.Caption := 'Checking game version and copying files -- this can take several minutes...';
  RomProgressBar.Visible := True;
  RomProgressBar.Style := npbstMarquee;
  WizardForm.Repaint;

  // For ISO input, PreflightCheckRom already performs the full extraction into {app}\rom as part
  // of validating the version (see its own comment) -- CopyRomFolder only runs for folder input.
  Result := PreflightCheckRom(RomPathEdit.Text, RomIsIsoRadio.Checked, ErrorMsg);
  if Result and not RomIsIsoRadio.Checked then
    Result := CopyRomFolder(RomPathEdit.Text, ErrorMsg);

  RomProgressBar.Style := npbstNormal;
  RomProgressBar.Visible := False;

  if not Result then
    MsgBox(ErrorMsg, mbError, MB_OK)
  else
    RomStatusLabel.Caption := 'Done.';
end;
```

- [ ] **Step 5: Compile and manually verify both paths**

Run: `& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\GiantRecomp.iss`
Expected: compiles cleanly.

Run the resulting installer three times manually:
1. Point it at your real extracted `rom\` folder. Expected: passes the check, copies into `%LocalAppData%\Programs\GiantRecomp\rom`, `default.xex` ends up there.
2. Point the folder picker at `%LocalAppData%\Programs\GiantRecomp` itself (the install target). Expected: rejected with the self-copy error message, before any copying starts.
3. Point it at some other unrelated file/folder (wrong version or no `default.xex`). Expected: rejected with the version/missing-file message, no copying attempted.

If you have a real ISO available, repeat case 1 with `-x -d` extraction instead of the folder-copy path, and confirm `installer\extract-xiso.exe -h`'s actual flags (checked in Task 3, Step 1) match what this task assumed; adjust the `Exec` parameter strings above if not.

- [ ] **Step 6: Commit**

```bash
git add installer/GiantRecomp.iss
git commit -m "feat: add ROM source page with fingerprint check and extraction"
```

---

### Task 6: Essentials settings page and toml generation

**Files:**
- Modify: `installer/GiantRecomp.iss`

**Interfaces:**
- Consumes: `installer/settings_template.toml` (Task 3), placeholder tokens `__PORTAL_MODE__`/`__RESOLUTION__`/`__RESOLUTION_SCALE__`.
- Produces: `{app}\giantsrecomp.toml`, written only if it doesn't already exist (Task 7 depends on this exact condition for update runs).

- [ ] **Step 1: Add the settings page**

Add to `InitializeWizard` (after the `RomPage := CreateCustomPage(...)` block from Task 5), plus new `var` declarations alongside the existing ones:

```pascal
var
  SettingsPage: TWizardPage;
  PortalModeCombo: TNewComboBox;
  ResolutionCombo: TNewComboBox;
  ResolutionScaleEdit: TNewEdit;
```

```pascal
  SettingsPage := CreateCustomPage(RomPage.ID, 'Settings',
    'Choose your Portal of Power and display settings (everything else can be changed later with F4 in-game)');

  PortalModeCombo := TNewComboBox.Create(SettingsPage);
  PortalModeCombo.Parent := SettingsPage.Surface;
  PortalModeCombo.Style := csDropDownList;
  PortalModeCombo.Items.Add('software (virtual Portal of Power)');
  PortalModeCombo.Items.Add('usb (real Portal of Power over USB)');
  PortalModeCombo.Items.Add('none (no portal)');
  PortalModeCombo.ItemIndex := 0;
  PortalModeCombo.Top := 0;
  PortalModeCombo.Width := SettingsPage.SurfaceWidth;

  ResolutionCombo := TNewComboBox.Create(SettingsPage);
  ResolutionCombo.Parent := SettingsPage.Surface;
  ResolutionCombo.Style := csDropDownList;
  ResolutionCombo.Items.Add('1920x1080');
  ResolutionCombo.Items.Add('2560x1440');
  ResolutionCombo.Items.Add('3840x2160');
  ResolutionCombo.ItemIndex := 0;
  ResolutionCombo.Top := PortalModeCombo.Top + PortalModeCombo.Height + 16;
  ResolutionCombo.Width := SettingsPage.SurfaceWidth;

  ResolutionScaleEdit := TNewEdit.Create(SettingsPage);
  ResolutionScaleEdit.Parent := SettingsPage.Surface;
  ResolutionScaleEdit.Text := '1';
  ResolutionScaleEdit.Top := ResolutionCombo.Top + ResolutionCombo.Height + 16;
  ResolutionScaleEdit.Width := 60;
```

- [ ] **Step 2: Map the combo selection to a toml value and write the file**

Add this function above `InitializeWizard`:

```pascal
function PortalModeTomlValue: String;
begin
  case PortalModeCombo.ItemIndex of
    1: Result := 'usb';
    2: Result := 'none';
  else
    Result := 'software';
  end;
end;

// Writes {app}\giantsrecomp.toml from the vendored template, substituting the three wizard-chosen
// values. Never overwrites an existing file: an update run (Task 7) relies on this exact check to
// leave a previously-configured, possibly hand-edited toml untouched.
procedure WriteSettingsFile;
var
  TemplatePath, DestPath, Contents: String;
  ResolutionScale: String;
begin
  DestPath := ExpandConstant('{app}') + '\giantsrecomp.toml';
  if FileExists(DestPath) then Exit;

  ExtractTemporaryFile('settings_template.toml');
  TemplatePath := ExpandConstant('{tmp}') + '\settings_template.toml';
  LoadStringFromFile(TemplatePath, Contents);

  ResolutionScale := Trim(ResolutionScaleEdit.Text);
  if (ResolutionScale = '') or (StrToIntDef(ResolutionScale, 0) < 1) then
    ResolutionScale := '1';

  StringChangeEx(Contents, '__PORTAL_MODE__', PortalModeTomlValue, True);
  StringChangeEx(Contents, '__RESOLUTION__', ResolutionCombo.Items[ResolutionCombo.ItemIndex], True);
  StringChangeEx(Contents, '__RESOLUTION_SCALE__', ResolutionScale, True);

  SaveStringToFile(DestPath, Contents, False);
end;
```

- [ ] **Step 3: Call it when the settings page is left**

Extend the `NextButtonClick` function from Task 5 (add this branch alongside the existing `if CurPageID <> RomPage.ID then Exit;` check — replace that single check with a small dispatch):

```pascal
function NextButtonClick(CurPageID: Integer): Boolean;
var
  ErrorMsg: String;
  FreeMB: Int64;
begin
  Result := True;

  if CurPageID = SettingsPage.ID then begin
    WriteSettingsFile;
    Exit;
  end;

  if CurPageID <> RomPage.ID then Exit;

  { ...unchanged body from Task 5 below this line... }
```

(Leave the rest of the function exactly as Task 5 left it — only the top of the function changes, adding the `SettingsPage.ID` branch before the existing `RomPage.ID` check.)

- [ ] **Step 4: Compile and manually verify**

Run: `& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\GiantRecomp.iss`
Expected: compiles cleanly.

Run the installer, choose `usb` for portal mode, `2560x1440` for resolution, `2` for resolution scale, finish the install. Expected: `%LocalAppData%\Programs\GiantRecomp\giantsrecomp.toml` contains `portal_mode = "usb"`, `resolution = "2560x1440"`, `resolution_scale = 2`, and every other line matches `giantsrecomp.toml.example` unchanged.

Re-run the installer over the same install without deleting anything. Expected: the settings page still appears (update-skip logic is Task 7, not yet built), but `giantsrecomp.toml` is left untouched (still shows your `usb`/`2560x1440`/`2` choices even if you pick different values on the second run) because `WriteSettingsFile` exits early when the file already exists.

- [ ] **Step 5: Commit**

```bash
git add installer/GiantRecomp.iss
git commit -m "feat: add essentials settings page and toml generation"
```

---

### Task 7: Update-run detection and uninstall confirmation

**Files:**
- Modify: `installer/GiantRecomp.iss`

**Interfaces:**
- Consumes: `{app}\rom\default.xex` and `{app}\giantsrecomp.toml` (written by Tasks 5-6), `giantrecomp_xexcheck.exe` (Task 2).
- Produces: `ShouldSkipPageID(PageID: Integer): Boolean` (Inno's own `ShouldSkipPage` event, used to skip the ROM and settings pages on a valid update run), `InitializeUninstall`/`CurUninstallStepChanged` (opt-in delete confirmation).

- [ ] **Step 1: Detect a valid existing install**

Add this function above `InitializeWizard`:

```pascal
// True when {app}\rom\default.xex already exists and passes the fingerprint check -- the signal
// used to treat this run as an update rather than a fresh install (spec's "Update runs" section).
function HasValidExistingRom: Boolean;
var
  ExistingXex, XexCheckExe: String;
  ResultCode: Integer;
begin
  Result := False;
  ExistingXex := ExpandConstant('{app}') + '\rom\default.xex';
  if not FileExists(ExistingXex) then Exit;

  XexCheckExe := ExpandConstant('{tmp}') + '\giantrecomp_xexcheck.exe';
  ExtractTemporaryFile('giantrecomp_xexcheck.exe');
  Result := Exec(XexCheckExe, '"' + ExistingXex + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)
    and (ResultCode = 0);
end;
```

- [ ] **Step 2: Skip the ROM and settings pages on a valid update**

Add this event function (Inno calls it automatically for every page; returning `True` skips that page):

```pascal
function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if (PageID = RomPage.ID) or (PageID = SettingsPage.ID) then
    Result := HasValidExistingRom;
end;
```

- [ ] **Step 3: Compile and manually verify the update path**

Run: `& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\GiantRecomp.iss`
Expected: compiles cleanly.

With a prior successful install already in place (from Task 6's verification), re-run the installer. Expected: the wizard goes straight from the install-directory page to the finish page — no ROM page, no settings page — and both `rom\` and `giantsrecomp.toml` are untouched (spot-check the toml still has your earlier choices).

Now delete `giantsrecomp.toml` from the existing install by hand (simulating the Review Focus case of a user deleting it) and re-run the installer. Expected: still skips the ROM page (rom/default.xex still passes), but the earlier update-skip on `SettingsPage.ID` would also skip settings — which would leave no toml at all. Fix `ShouldSkipPage` to only skip `SettingsPage.ID` when the toml actually exists:

```pascal
function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if PageID = RomPage.ID then
    Result := HasValidExistingRom;
  if PageID = SettingsPage.ID then
    Result := HasValidExistingRom and FileExists(ExpandConstant('{app}') + '\giantsrecomp.toml');
end;
```

Recompile and re-verify: deleting just the toml and re-running now shows the settings page again (and `WriteSettingsFile` from Task 6 writes a fresh one, since it no longer exists).

- [ ] **Step 4: Add the opt-in uninstall confirmation**

Add to the same `[Code]` section:

```pascal
var
  DeleteRomAndSettingsOnUninstall: Boolean;

function InitializeUninstall: Boolean;
begin
  Result := True;
  DeleteRomAndSettingsOnUninstall := False;
  // WizardSilent-equivalent for the uninstaller: never block on a dialog nobody can answer during
  // an unattended uninstall (e.g. `unins000.exe /VERYSILENT`) -- default to "do not delete".
  if UninstallSilent then Exit;

  if MsgBox('Also delete the extracted game files and settings (rom\ and giantsrecomp.toml)?' +
      #13#10 + 'Your saves are stored separately and are never deleted.',
      mbConfirmation, MB_YESNO) = IDYES then
    DeleteRomAndSettingsOnUninstall := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and DeleteRomAndSettingsOnUninstall then begin
    DelTree(ExpandConstant('{app}') + '\rom', True, True, True);
    DeleteFile(ExpandConstant('{app}') + '\giantsrecomp.toml');
  end;
end;
```

- [ ] **Step 5: Compile and manually verify uninstall**

Run: `& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\GiantRecomp.iss`
Expected: compiles cleanly.

Uninstall via the Start Menu shortcut, answer "No" to the prompt. Expected: `rom\` and `giantsrecomp.toml` remain in the (now otherwise empty) install directory.

Reinstall, then uninstall again and answer "Yes". Expected: `rom\` and `giantsrecomp.toml` are both gone.

Reinstall once more, then run `"%LocalAppData%\Programs\GiantRecomp\unins000.exe" /VERYSILENT` from a terminal. Expected: uninstalls without showing any dialog, and `rom\`/`giantsrecomp.toml` are left in place (silent default is "do not delete").

- [ ] **Step 6: Commit**

```bash
git add installer/GiantRecomp.iss
git commit -m "feat: add update-run page skipping and opt-in uninstall cleanup"
```

---

### Task 8: `just package-installer` recipe

**Files:**
- Modify: `justfile`
- Modify: `.gitignore` (already covers `/installer/staging/` and `/installer/Output/` from Task 4 — verify no further change needed)

**Interfaces:**
- Produces: `installer\Output\GiantRecompSetup.exe`, built end-to-end from one command, replacing the manual staging steps used for verification in Tasks 4-7.

- [ ] **Step 1: Find ISCC on the machine**

Confirm where Inno Setup 6 installed `ISCC.exe` (default is `C:\Program Files (x86)\Inno Setup 6\ISCC.exe`); the recipe below assumes that path.

- [ ] **Step 2: Add the recipe**

In `justfile`, add this after the existing `package-release` recipe (before the `# --- Test ---` section header):

```make
# Build the release binary, stage it alongside the installer's other inputs, and compile the
# Windows installer with Inno Setup (ISCC.exe). Windows only -- see docs/releasing.md.
package-installer: build-release
    rm -rf installer/staging && \
    mkdir -p installer/staging && \
    cp out/build/win-amd64-release/giantrecomp.exe installer/staging/ && \
    for lib in out/build/win-amd64-release/*.dll {{ sdk_lib_dir }}/*.dll; do [ -e "$lib" ] && cp "$lib" installer/staging/; done && \
    "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" installer/GiantRecomp.iss && \
    echo "Installer built at installer/Output/GiantRecompSetup.exe"
```

- [ ] **Step 3: Run it end-to-end**

Run: `just package-installer`
Expected: builds the release binary if needed, stages it, compiles the installer, and prints the success message. Run the resulting `installer\Output\GiantRecompSetup.exe` once more to confirm it's a fully working install (this exercises the exact artifact a real release would ship).

- [ ] **Step 4: Commit**

```bash
git add justfile
git commit -m "feat: add just package-installer recipe"
```

---

### Task 9: Documentation restructure

**Files:**
- Modify: `README.md`
- Create: `docs/development.md`
- Create: `docs/releasing.md`
- Modify: `docs/build.md` (one cross-reference line)

**Interfaces:**
- None (documentation only).

- [ ] **Step 1: Create `docs/development.md`**

Move the following sections out of `README.md` verbatim: the entire "Setup" section (both "Windows" and "Linux (experimental)" subsections, including the `just`/`cmake`/`ctest` commands) and the entire "For developers" section. Give the new file this structure:

```markdown
# Development

Building GiantRecomp from source. If you just want to play, download the installer instead — see
`README.md`.

## What you need

[...the numbered "What you need" list from the current README, items 1-4, unchanged...]

## Setup

[...the current README's "Setup" section, Windows and Linux subsections, unchanged...]

## For developers

[...the current README's "For developers" section, unchanged...]

See also: `docs/architecture.md` (repository layout and portal architecture), `docs/build.md`
(codegen fixes and testing), `docs/releasing.md` (cutting a release).
```

- [ ] **Step 2: Create `docs/releasing.md`**

```markdown
# Releasing

How to cut a release: build the binary, package the Windows installer, and publish it. No CI is
involved — every step here runs on the maintainer's own machine, against their own legally-owned
copy of the game (see `docs/superpowers/specs/2026-09-28-installer-design.md` for why).

## Build and package

```
just package-installer
```

This builds the release binary (`just build-release`), stages it with the installer's other
inputs, and compiles `installer/GiantRecomp.iss` with Inno Setup, producing
`installer/Output/GiantRecompSetup.exe`. See `installer/GiantRecomp.iss` and the `package-installer`
recipe in `justfile` for what it does under the hood.

## Before publishing, verify

- Fresh, clean Windows VM (no Visual Studio/CMake/Ninja installed) — install with a real ISO, and
  separately with an already-extracted folder; confirm the game launches both ways.
- A wrong-version `default.xex` is rejected before any extraction, with a clear message.
- Re-running the installer over an existing install preserves `rom/`, `giantsrecomp.toml`, and
  saves, and only replaces the binary.
- Uninstalling leaves `rom/` and the toml in place unless the opt-in prompt is answered "Yes";
  `unins000.exe /VERYSILENT` never prompts and defaults to leaving them.
- The Start Menu shortcut launches the game, and an "Uninstall GiantRecomp" entry appears in both
  the Start Menu and Windows' Add/Remove Programs.

## Publish

Upload `installer/Output/GiantRecompSetup.exe` to a new GitHub Release. Note in the release
description which exact game version/region the installer was built and tested against.

## Bump the version

`installer/GiantRecomp.iss`'s `MyAppVersion` define should match the release tag before compiling.
```

- [ ] **Step 3: Rewrite `README.md`**

Remove the "Setup" and "For developers" sections entirely (moved to `docs/development.md` in Step 1). In their place, add an "Installing" section right after "What you need" (before the old "Setup" section used to start):

```markdown
## Installing

Download the latest `GiantRecompSetup.exe` from
[Releases](https://github.com/TheBiemGamer/GiantsRecomp/releases), run it, and point it at your
own Skylanders Giants disc — either the ISO file directly, or an already-extracted folder. The
installer also lets you choose your Portal of Power mode and display resolution up front (anything
else can be changed later in-game with F4), and adds a Start Menu shortcut.

Building from source instead (for development, or if you don't trust a prebuilt binary) is covered
in `docs/development.md`.
```

Update the line "This repository contains **no game code and no game data**" to:

```markdown
This repository contains **no game data**. You need your own copy of the game.
```

(Immediately below the project pitch paragraph, same location as today.)

Update the "Playing" section: it currently assumes a source build's `out\build\...\giantrecomp.exe` path. Change its opening line to clarify it applies to a source build (installer users don't need this section — they use the Start Menu shortcut):

```markdown
## Playing

If you installed via `GiantRecompSetup.exe`, use the Start Menu shortcut. If you built from
source, run it from the project folder:
```

(Keep the rest of that section, including the `out\build\...` paths and the entire "Settings file" subsection, unchanged — settings documentation applies to both installer and source-build users alike.)

- [ ] **Step 4: Add the cross-reference to `docs/build.md`**

At the top of `docs/build.md`, right after its title line, add (matching `docs/architecture.md:3-5`'s existing "See also" style):

```markdown
See also: `docs/development.md` (building from source), `docs/releasing.md` (cutting a release).
```

- [ ] **Step 5: Verify no content was lost**

Run: `git diff README.md` and manually confirm every removed line reappears, unchanged, in either `docs/development.md` or is one of the two intentionally-reworded lines (the "no game data" line and the "Playing" section's opening line) called out in Steps 3 above.

- [ ] **Step 6: Commit**

```bash
git add README.md docs/development.md docs/releasing.md docs/build.md
git commit -m "docs: split README into player/dev/release docs"
```

---

## Final check

After Task 9, run the full test suite once to confirm nothing in the C++ side regressed:

```bash
just build-release
just test-release
```

Expected: all tests pass, including the two new ones from Task 1 (`xex_check_exit_code`) — the rest of the suite (`xex_verify`, `portal_framing_test`, etc.) is unaffected by this feature and should already be passing.
