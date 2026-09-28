# RE Tooling Setup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give Giants Recompiled a fully headless, agent-drivable reverse-engineering workflow —
no GUI, no developer in the loop for routine use — that turns a function address into a real
symbol name in the recompiled C++, via `config/default.toml`'s existing `[functions]` section.

**Architecture:** A pure, dependency-free Python module (`tools/rexglue_toml_sync.py`) does the
TOML text surgery — merge in place, never reorder or drop anything it doesn't touch — unit
tested with the standard library. Everything Ghidra-side runs through `analyzeHeadless`
(`-import`/`-process`/`-postScript`, all non-interactive): one script prints a function's
decompiled C plus callers/callees/strings so an agent can read and reason about it
(`ghidra_dump_function.py`), another renames it in Ghidra's database and syncs
`config/default.toml` in the same call (`ghidra_rename_and_export.py`). A PowerShell wrapper
(`tools/ghidra_re.ps1`) is the single entry point, including automated setup of the JDK, Ghidra,
and the XEXLoaderWV extension — nobody has to click through an installer.

**Tech Stack:** Python 3 standard library only for `rexglue_toml_sync.py` (must run standalone
for tests, and inside Ghidra's bundled Jython for the headless scripts — no third-party
packages either place). PowerShell 7+ for the wrapper. Ghidra + XEXLoaderWV
(https://github.com/zeroKilo/XEXLoaderWV) for loading and analyzing `rom/default.xex`. A JDK
(21+, Ghidra's current minimum) to run Ghidra itself.

**Spec:** `docs/superpowers/specs/2026-09-28-re-tooling-design.md` (see the "Headless / agentic
operation" addendum for why this differs from a GUI-driven design)

## Global Constraints

- No changes to `thirdparty/rexglue-sdk/` (vendored, not modified) — `config/default.toml` is
  the only interface.
- Every Ghidra-side script is non-interactive: no `askFile`, no GUI dialogs, arguments only via
  `getScriptArgs()`, driven through `analyzeHeadless`.
- `config/default.toml` changes must preserve every existing entry and comment exactly where the
  sync logic doesn't touch them.
- `size` values in `[functions]` are decimal integers (confirmed: all 75 existing entries use
  decimal, e.g. `size = 12`, never `0xC`) — new entries must match.
- All downloaded/generated tooling state (JDK, Ghidra, the Ghidra project) lives under the
  already-git-ignored `logs/` folder — nothing here gets committed except the four `tools/*`
  scripts, `tools/rexglue_toml_sync_test.py`, and `docs/reverse-engineering.md`.
- No custom Ghidra XEX loader — use the XEXLoaderWV extension, installed by placing its unzipped
  contents under `<ghidra_install>/Extensions/Ghidra/` (how Ghidra discovers extensions in both
  GUI and headless mode — no separate "install" command exists or is needed).

## Review Focus

- Two different addresses discovered with the same name — must not silently write a duplicate
  symbol name into two `[functions]` entries (a C++ compile error waiting to happen).
- Updating an existing entry's `name`/`size` must leave every other key on that entry (e.g.
  `parent`) byte-for-byte untouched.
- The `[functions]` section can legally run to end-of-file (it's the last section in
  `config/default.toml` today) — appending new entries must not depend on a trailing section
  header existing.
- Running `rename` twice with identical input must be a no-op the second time — no reformatting
  churn on untouched entries, which would otherwise show up as noisy diffs across many RE
  sessions.
- A TOML file with no `[functions]` section at all must fail loudly and specifically, not
  silently do nothing (e.g. if `rename` is accidentally pointed at the wrong file).

---

## File Structure

- Create: `tools/rexglue_toml_sync.py` — pure merge logic, no file I/O, no Ghidra dependency.
- Create: `tools/rexglue_toml_sync_test.py` — `unittest` tests for the above.
- Create: `tools/ghidra_dump_function.py` — headless post-script, prints decompiled C +
  callers/callees/strings for one function. Read-only.
- Create: `tools/ghidra_rename_and_export.py` — headless post-script, renames functions in the
  Ghidra database and calls `rexglue_toml_sync.sync_functions` to update `config/default.toml`.
- Create: `tools/ghidra_re.ps1` — wrapper with `setup`/`import`/`dump`/`rename` subcommands;
  the only thing a human or agent needs to invoke directly.
- Create: `docs/reverse-engineering.md` — the workflow, end to end.
- Modify: `.gitignore` — nothing new needed; `/logs/` is already ignored and that's where every
  generated artifact (JDK, Ghidra, the Ghidra project) lives.
- Modify: `docs/architecture.md:1-5` — link the new doc.

---

### Task 1: TOML sync logic (pure, unit tested)

**Files:**
- Create: `tools/rexglue_toml_sync.py`
- Create: `tools/rexglue_toml_sync_test.py`

**Interfaces:**
- Consumes: nothing (first task).
- Produces (used by Task 4): `parse_entry`, `format_entry`, `find_functions_section`,
  `class SyncResult { lines, updated, added, conflicts }`, `sync_functions(lines, updates)`,
  `quoted_name(raw_name) -> str`, `decimal_size(size) -> str`.

- [ ] **Step 1: Write the failing tests**

Create `tools/rexglue_toml_sync_test.py`:

```python
"""Unit tests for tools/rexglue_toml_sync.py. Stdlib only.

Run with: python3 -m unittest tools.rexglue_toml_sync_test
"""

import unittest

from rexglue_toml_sync import (
    SyncResult,
    decimal_size,
    find_functions_section,
    format_entry,
    parse_entry,
    quoted_name,
    sync_functions,
)


SAMPLE_LINES = [
    "# header comment\n",
    "[[switch_tables]]\n",
    "address = 0x1000\n",
    "\n",
    "[functions]\n",
    "# leaf reached only through vtable calls\n",
    "0x822B9D58 = { size = 12 }\n",
    "\n",
    "# chunk of the function above\n",
    "0x822B9D70 = { parent = 0x822B9D58, size = 8 }\n",
]

SAMPLE_LINES_EOF = [
    "[functions]\n",
    "0x822B9D58 = { size = 12 }\n",
]


class ParseFormatTests(unittest.TestCase):
    def test_parse_entry_size_only(self):
        self.assertEqual(
            parse_entry("0x822B9D58 = { size = 12 }\n"),
            (0x822B9D58, "0x822B9D58", {"size": "12"}),
        )

    def test_parse_entry_non_matching_line_returns_none(self):
        self.assertIsNone(parse_entry("# just a comment\n"))
        self.assertIsNone(parse_entry("[functions]\n"))

    def test_format_entry_key_order(self):
        self.assertEqual(
            format_entry("0x1", {"size": "12", "name": '"Foo"', "parent": "0x2"}),
            '0x1 = { name = "Foo", parent = 0x2, size = 12 }\n',
        )

    def test_format_entry_empty_props(self):
        self.assertEqual(format_entry("0x1", {}), "0x1 = {}\n")


class HelperTests(unittest.TestCase):
    def test_quoted_name_sanitizes(self):
        self.assertEqual(quoted_name("Update Camera!"), '"Update_Camera"')

    def test_quoted_name_leading_digit(self):
        self.assertEqual(quoted_name("2ndFunc"), '"_2ndFunc"')

    def test_decimal_size(self):
        self.assertEqual(decimal_size(12), "12")


class FindSectionTests(unittest.TestCase):
    def test_finds_bounded_section(self):
        start, end = find_functions_section(SAMPLE_LINES)
        self.assertEqual(SAMPLE_LINES[start].strip(), "[functions]")
        self.assertEqual(end, len(SAMPLE_LINES))

    def test_section_can_run_to_end_of_file(self):
        start, end = find_functions_section(SAMPLE_LINES_EOF)
        self.assertEqual(start, 0)
        self.assertEqual(end, len(SAMPLE_LINES_EOF))

    def test_missing_section_raises(self):
        with self.assertRaises(ValueError):
            find_functions_section(["[other]\n", "x = 1\n"])


class SyncFunctionsTests(unittest.TestCase):
    def test_update_existing_entry_preserves_other_keys(self):
        result = sync_functions(
            list(SAMPLE_LINES),
            {0x822B9D70: {"name": '"ChunkFunc"'}},
        )
        self.assertEqual(result.updated, [0x822B9D70])
        updated_line = next(l for l in result.lines if l.startswith("0x822B9D70"))
        self.assertIn("parent = 0x822B9D58", updated_line)
        self.assertIn("size = 8", updated_line)
        self.assertIn('name = "ChunkFunc"', updated_line)

    def test_adds_new_entry_sorted_at_end_of_section(self):
        result = sync_functions(
            list(SAMPLE_LINES),
            {0x82500000: {"name": '"NewFunc"', "size": "40"}},
        )
        self.assertEqual(result.added, [0x82500000])
        self.assertEqual(
            result.lines[-1], '0x82500000 = { name = "NewFunc", size = 40 }\n'
        )

    def test_section_runs_to_end_of_file(self):
        result = sync_functions(
            list(SAMPLE_LINES_EOF),
            {0x82600000: {"name": '"Tail"'}},
        )
        self.assertEqual(result.added, [0x82600000])
        self.assertEqual(result.lines[-1], '0x82600000 = { name = "Tail" }\n')

    def test_idempotent_rerun_no_changes(self):
        first = sync_functions(list(SAMPLE_LINES), {0x822B9D58: {"name": '"Leaf"'}})
        second = sync_functions(list(first.lines), {0x822B9D58: {"name": '"Leaf"'}})
        self.assertEqual(second.updated, [])
        self.assertEqual(second.added, [])
        self.assertEqual(second.lines, first.lines)

    def test_missing_section_raises(self):
        with self.assertRaises(ValueError):
            sync_functions(["[other]\n"], {0x1: {"name": '"X"'}})

    def test_duplicate_name_collision_reported_and_skipped(self):
        lines = list(SAMPLE_LINES)
        result = sync_functions(
            lines,
            {
                0x822B9D58: {"name": '"SameName"'},
                0x822B9D70: {"name": '"SameName"'},
            },
        )
        names = [l for l in result.lines if "SameName" in l]
        self.assertEqual(len(names), 1)
        self.assertEqual(len(result.conflicts), 1)
        conflict_name, new_addr, existing_addr = result.conflicts[0]
        self.assertEqual(conflict_name, '"SameName"')


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest tools.rexglue_toml_sync_test -v` (from repo root)
Expected: `ModuleNotFoundError`/`ImportError` — the module doesn't exist yet.

- [ ] **Step 3: Write the implementation**

Create `tools/rexglue_toml_sync.py`:

```python
"""Pure logic for merging Ghidra-discovered function names into the
[functions] section of a ReXGlue codegen-fix TOML file (config/default.toml),
in place, without reordering or dropping anything the merge doesn't touch --
existing entries, their key order, and any comments stay exactly where they
are.

No file I/O and no Ghidra dependency here, so this is testable with plain
`python3 -m unittest`. tools/ghidra_rename_and_export.py is the headless
Ghidra driver that collects function data from the open program and calls
sync_functions().
"""

import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

_ENTRY_RE = re.compile(r'^(0[xX][0-9a-fA-F]+)\s*=\s*\{(.*)\}\s*$')
_KV_RE = re.compile(r'(\w+)\s*=\s*("(?:[^"\\]|\\.)*"|0[xX][0-9a-fA-F]+|\d+)')
_SECTION_RE = re.compile(r'^\[[^\[\]].*\]\s*$')  # matches [section], not [[array]]


@dataclass
class SyncResult:
    lines: List[str]
    updated: List[int] = field(default_factory=list)
    added: List[int] = field(default_factory=list)
    conflicts: List[Tuple[str, int, int]] = field(default_factory=list)


def parse_entry(line: str) -> Optional[Tuple[int, str, Dict[str, str]]]:
    """Parse one `0xADDR = { key = value, ... }` line. Returns
    (address, address_string_as_written, props) or None if the line isn't
    a functions-entry line (a comment, blank line, or section header)."""
    m = _ENTRY_RE.match(line.strip())
    if not m:
        return None
    addr_str = m.group(1)
    props = {kv.group(1): kv.group(2) for kv in _KV_RE.finditer(m.group(2))}
    return int(addr_str, 16), addr_str, props


def format_entry(addr_str: str, props: Dict[str, str]) -> str:
    """Render one entry line with a consistent key order (name, parent,
    size, then anything else) so diffs stay small and reviewable."""
    if not props:
        return f"{addr_str} = {{}}\n"
    key_order = ["name", "parent", "size"]
    parts = [f"{k} = {props[k]}" for k in key_order if k in props]
    parts += [f"{k} = {v}" for k, v in props.items() if k not in key_order]
    return f"{addr_str} = {{ {', '.join(parts)} }}\n"


def find_functions_section(lines: List[str]) -> Tuple[int, int]:
    """Return (start, end): start is the index of the '[functions]' header
    line itself, end is the index of the next top-level '[section]' header,
    or len(lines) if [functions] runs to the end of the file (it's the last
    section in config/default.toml today)."""
    start = None
    for i, line in enumerate(lines):
        if line.strip() == "[functions]":
            start = i
            break
    if start is None:
        raise ValueError("No [functions] section found in the TOML file.")
    end = len(lines)
    for i in range(start + 1, len(lines)):
        if _SECTION_RE.match(lines[i].strip()):
            end = i
            break
    return start, end


def quoted_name(raw_name: str) -> str:
    """Sanitize a Ghidra function name into a valid C/C++ identifier and
    wrap it as a TOML string value, e.g. 'Update Camera!' -> '"Update_Camera"'."""
    name = re.sub(r'[^a-zA-Z0-9_]', '_', raw_name)
    name = re.sub(r'_+', '_', name).strip('_')
    if name and name[0].isdigit():
        name = '_' + name
    return f'"{name}"'


def decimal_size(size: int) -> str:
    """Format a byte count as the project's decimal convention (every
    existing entry in config/default.toml uses e.g. `size = 12`, never hex)."""
    return str(size)


def sync_functions(lines: List[str], updates: Dict[int, Dict[str, str]]) -> SyncResult:
    """Merge `updates` (address -> {"name": '"Foo"', "size": "12", ...},
    values already formatted as raw TOML) into the [functions] section of
    `lines`. Existing entries are updated in place (only the given keys
    change; everything else on that line, and every other line, is left
    untouched). Addresses not already present are appended at the end of
    the section, sorted by address. A `name` that would collide with a
    different address's existing or newly-assigned name is skipped and
    reported in `conflicts` rather than written twice."""
    start, end = find_functions_section(lines)

    out = list(lines)
    remaining = dict(updates)

    used_names: Dict[str, int] = {}
    for i in range(start + 1, end):
        parsed = parse_entry(out[i])
        if parsed:
            addr_int, _, props = parsed
            if "name" in props:
                used_names.setdefault(props["name"], addr_int)

    result = SyncResult(lines=out)

    for i in range(start + 1, end):
        parsed = parse_entry(out[i])
        if not parsed:
            continue
        addr_int, addr_str, props = parsed
        if addr_int not in remaining:
            continue

        new_props = dict(props)
        changed = False
        for key, value in remaining[addr_int].items():
            if key == "name" and value in used_names and used_names[value] != addr_int:
                result.conflicts.append((value, addr_int, used_names[value]))
                continue
            if new_props.get(key) != value:
                new_props[key] = value
                changed = True
            if key == "name":
                used_names[value] = addr_int

        if changed:
            out[i] = format_entry(addr_str, new_props)
            result.updated.append(addr_int)
        del remaining[addr_int]

    insert_at = end
    for addr_int in sorted(remaining):
        values = remaining[addr_int]
        props: Dict[str, str] = {}
        for key, value in values.items():
            if key == "name" and value in used_names and used_names[value] != addr_int:
                result.conflicts.append((value, addr_int, used_names[value]))
                continue
            props[key] = value
            if key == "name":
                used_names[value] = addr_int
        addr_str = f"0x{addr_int:08X}"
        out.insert(insert_at, format_entry(addr_str, props))
        insert_at += 1
        result.added.append(addr_int)

    return result
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 -m unittest tools.rexglue_toml_sync_test -v` (from repo root)
Expected: all tests `ok`.

- [ ] **Step 5: Commit**

```bash
git add tools/rexglue_toml_sync.py tools/rexglue_toml_sync_test.py
git commit -m "feat: add TOML sync logic for RE function-name export"
```

---

### Task 2: Automated setup + import (`tools/ghidra_re.ps1 setup` / `import`)

**Files:**
- Create: `tools/ghidra_re.ps1` (this task adds `setup` and `import`; Tasks 3-4 add `dump` and
  `rename`)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `logs/toolchain/jdk/`, `logs/toolchain/ghidra/` (with XEXLoaderWV installed under
  `Extensions/Ghidra/`), `logs/toolchain/analyzeHeadless.bat` (resolved path), and
  `logs/ghidra_project/giantsrecomp.gpr` (an imported, analyzed program) — later tasks and their
  `dump`/`rename` subcommands depend on all of these existing.

- [ ] **Step 1: Write `tools/ghidra_re.ps1` with `setup` and `import`**

```powershell
<#
.SYNOPSIS
Headless Ghidra RE workflow for Giants Recompiled. Entry point for setup,
importing default.xex, reading a function's decompiled code, and renaming +
exporting names into config/default.toml. No GUI involved anywhere.

.USAGE
    tools/ghidra_re.ps1 setup
    tools/ghidra_re.ps1 import
    tools/ghidra_re.ps1 dump <address>
    tools/ghidra_re.ps1 rename <address> <name> [<address> <name> ...]
#>

param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet("setup", "import", "dump", "rename")]
    [string]$Command,

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$Rest
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ToolchainDir = Join-Path $RepoRoot "logs\toolchain"
$JdkDir = Join-Path $ToolchainDir "jdk"
$GhidraDir = Join-Path $ToolchainDir "ghidra"
$ProjectDir = Join-Path $RepoRoot "logs\ghidra_project"
$ProjectName = "giantsrecomp"
$XexPath = Join-Path $RepoRoot "rom\default.xex"

function Get-AnalyzeHeadlessPath {
    $found = Get-ChildItem -Path $GhidraDir -Filter "analyzeHeadless.bat" -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $found) {
        throw "analyzeHeadless.bat not found under $GhidraDir -- run 'tools/ghidra_re.ps1 setup' first."
    }
    return $found.FullName
}

function Get-JavaHome {
    $found = Get-ChildItem -Path $JdkDir -Filter "java.exe" -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $found) {
        throw "java.exe not found under $JdkDir -- run 'tools/ghidra_re.ps1 setup' first."
    }
    # java.exe is at <home>\bin\java.exe
    return (Get-Item $found.FullName).Directory.Parent.FullName
}

function Invoke-Setup {
    New-Item -ItemType Directory -Force -Path $ToolchainDir | Out-Null

    if (-not (Get-ChildItem -Path $JdkDir -Filter "java.exe" -Recurse -ErrorAction SilentlyContinue)) {
        Write-Host "Downloading JDK 21 (Temurin)..."
        $jdkRelease = Invoke-RestMethod -Uri "https://api.adoptium.net/v3/assets/latest/21/hotspot?os=windows&architecture=x64&image_type=jdk"
        $jdkUrl = $jdkRelease[0].binary.package.link
        $jdkZip = Join-Path $ToolchainDir "jdk.zip"
        Invoke-WebRequest -Uri $jdkUrl -OutFile $jdkZip
        New-Item -ItemType Directory -Force -Path $JdkDir | Out-Null
        Expand-Archive -Path $jdkZip -DestinationPath $JdkDir -Force
        Remove-Item $jdkZip
    } else {
        Write-Host "JDK already present, skipping."
    }

    if (-not (Get-ChildItem -Path $GhidraDir -Filter "ghidraRun.bat" -Recurse -ErrorAction SilentlyContinue)) {
        Write-Host "Downloading latest Ghidra release..."
        $ghidraRelease = Invoke-RestMethod -Uri "https://api.github.com/repos/NationalSecurityAgency/ghidra/releases/latest"
        $ghidraAsset = $ghidraRelease.assets | Where-Object { $_.name -match "^ghidra_.*_PUBLIC_.*\.zip$" } | Select-Object -First 1
        if (-not $ghidraAsset) {
            throw "Could not find a Ghidra release zip asset on the latest GitHub release."
        }
        $ghidraZip = Join-Path $ToolchainDir "ghidra.zip"
        Invoke-WebRequest -Uri $ghidraAsset.browser_download_url -OutFile $ghidraZip
        New-Item -ItemType Directory -Force -Path $GhidraDir | Out-Null
        Expand-Archive -Path $ghidraZip -DestinationPath $GhidraDir -Force
        Remove-Item $ghidraZip
    } else {
        Write-Host "Ghidra already present, skipping."
    }

    $ghidraInstallDir = Get-ChildItem -Path $GhidraDir -Directory | Select-Object -First 1
    $extensionsDir = Join-Path $ghidraInstallDir.FullName "Extensions\Ghidra"
    $alreadyInstalled = Get-ChildItem -Path $extensionsDir -Filter "*XEXLoader*" -Directory -ErrorAction SilentlyContinue
    if (-not $alreadyInstalled) {
        Write-Host "Downloading XEXLoaderWV extension..."
        $xexRelease = Invoke-RestMethod -Uri "https://api.github.com/repos/zeroKilo/XEXLoaderWV/releases/latest"
        $xexAsset = $xexRelease.assets | Where-Object { $_.name -match "\.zip$" } | Select-Object -First 1
        if (-not $xexAsset) {
            throw "Could not find an XEXLoaderWV release zip asset on the latest GitHub release."
        }
        $xexZip = Join-Path $ToolchainDir "xexloaderwv.zip"
        Invoke-WebRequest -Uri $xexAsset.browser_download_url -OutFile $xexZip
        New-Item -ItemType Directory -Force -Path $extensionsDir | Out-Null
        Expand-Archive -Path $xexZip -DestinationPath $extensionsDir -Force
        Remove-Item $xexZip
    } else {
        Write-Host "XEXLoaderWV already present, skipping."
    }

    Write-Host "Setup complete."
}

function Invoke-Import {
    if (-not (Test-Path $XexPath)) {
        throw "rom/default.xex not found at $XexPath -- place your own dump there first."
    }
    New-Item -ItemType Directory -Force -Path $ProjectDir | Out-Null
    $env:JAVA_HOME = Get-JavaHome
    $analyzeHeadless = Get-AnalyzeHeadlessPath
    & $analyzeHeadless $ProjectDir $ProjectName -import $XexPath
    if ($LASTEXITCODE -ne 0) {
        throw "analyzeHeadless import failed with exit code $LASTEXITCODE"
    }
    Write-Host "Import complete: $ProjectDir\$ProjectName"
}

switch ($Command) {
    "setup" { Invoke-Setup }
    "import" { Invoke-Import }
    "dump" { throw "'dump' not implemented yet (Task 3)." }
    "rename" { throw "'rename' not implemented yet (Task 4)." }
}
```

- [ ] **Step 2: Run setup for real**

Run: `pwsh tools/ghidra_re.ps1 setup`
Expected: downloads complete, ends with `Setup complete.`. If any URL/asset-matching assumption
above is wrong (GitHub API shape, asset naming), this step will throw with a clear error — fix
the script (asset name pattern, JSON path) and re-run until it succeeds. Confirm afterward:
`Test-Path logs\toolchain\jdk\**\java.exe` and the Ghidra directory both resolve, and
`Extensions\Ghidra\` under the Ghidra install contains an XEXLoaderWV folder.

- [ ] **Step 3: Run import for real**

Run: `pwsh tools/ghidra_re.ps1 import`
Expected: XEXLoaderWV picks up `rom/default.xex` (XEX2 magic is enough for Ghidra's loader
auto-detection; no `-loader` flag should be needed), full auto-analysis runs (this can take
several minutes to tens of minutes for a full game binary), ends with `Import complete: ...`.
If the loader isn't picked up automatically, add `-loader XexLoader` (or whatever class name
XEXLoaderWV registers — check its README) to the `analyzeHeadless` call in `Invoke-Import` and
re-run.

- [ ] **Step 4: Commit**

```bash
git add tools/ghidra_re.ps1
git commit -m "feat: add headless Ghidra setup/import wrapper"
```

---

### Task 3: Read a function's decompiled code (`ghidra_dump_function.py` + `dump`)

**Files:**
- Create: `tools/ghidra_dump_function.py`
- Modify: `tools/ghidra_re.ps1` (implement the `dump` case)

**Interfaces:**
- Consumes: the imported project from Task 2 (`logs/ghidra_project/`).
- Produces: nothing further in-repo consumes this — its stdout is read directly by whoever
  (agent or developer) is deciding what to name a function.

- [ ] **Step 1: Write `tools/ghidra_dump_function.py`**

```python
# Print decompiled C, callers, callees, and referenced strings for one
# function, so an agent (or a developer) can decide what to name it without
# ever opening the Ghidra GUI.
#
# Usage (via analyzeHeadless -postScript): ghidra_dump_function.py <hex address>
#
# @category ReXGlue

from ghidra.app.decompiler import DecompInterface
from ghidra.program.model.symbol import RefType
from ghidra.util.task import ConsoleTaskMonitor


def get_function_at(addr_str):
    addr = currentProgram.getAddressFactory().getAddress(addr_str)
    func = currentProgram.getFunctionManager().getFunctionAt(addr)
    if func is None:
        raise Exception("No function at {}".format(addr_str))
    return func


def print_decompiled(func):
    decompiler = DecompInterface()
    decompiler.openProgram(currentProgram)
    try:
        results = decompiler.decompileFunction(func, 60, ConsoleTaskMonitor())
        if not results.decompileCompleted():
            println("-- decompile failed: {}".format(results.getErrorMessage()))
            return
        println("-- decompiled C --")
        println(results.getDecompiledFunction().getC())
    finally:
        decompiler.dispose()


def print_callers_callees(func):
    println("-- callers --")
    for ref in currentProgram.getReferenceManager().getReferencesTo(func.getEntryPoint()):
        if ref.getReferenceType().isCall():
            caller = currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress())
            caller_name = caller.getName() if caller else "?"
            println("  {} ({})".format(ref.getFromAddress(), caller_name))

    println("-- callees --")
    addr_set = func.getBody()
    for instr in currentProgram.getListing().getInstructions(addr_set, True):
        for ref in instr.getReferencesFrom():
            if ref.getReferenceType().isCall():
                callee = currentProgram.getFunctionManager().getFunctionAt(ref.getToAddress())
                if callee:
                    println("  {} ({})".format(ref.getToAddress(), callee.getName()))


def print_strings(func):
    println("-- referenced strings --")
    listing = currentProgram.getListing()
    for instr in listing.getInstructions(func.getBody(), True):
        for ref in instr.getReferencesFrom():
            data = listing.getDataAt(ref.getToAddress())
            if data is not None and data.hasStringValue():
                println("  {} : {}".format(ref.getToAddress(), data.getValue()))


def main():
    args = getScriptArgs()
    if len(args) != 1:
        raise Exception("Usage: ghidra_dump_function.py <hex address>")
    func = get_function_at(args[0])
    println("Function: {} @ {}".format(func.getName(), func.getEntryPoint()))
    println("Size: {} bytes".format(func.getBody().getNumAddresses()))
    print_decompiled(func)
    print_callers_callees(func)
    print_strings(func)


main()
```

- [ ] **Step 2: Implement the `dump` case in `tools/ghidra_re.ps1`**

Replace `"dump" { throw "'dump' not implemented yet (Task 3)." }` with:

```powershell
    "dump" {
        if ($Rest.Count -ne 1) {
            throw "Usage: tools/ghidra_re.ps1 dump <address>"
        }
        $env:JAVA_HOME = Get-JavaHome
        $analyzeHeadless = Get-AnalyzeHeadlessPath
        & $analyzeHeadless $ProjectDir $ProjectName -process "*" -noanalysis `
            -scriptPath (Join-Path $RepoRoot "tools") `
            -postScript "ghidra_dump_function.py" $Rest[0]
    }
```

- [ ] **Step 3: Run it for real**

Run: `pwsh tools/ghidra_re.ps1 dump 0x82403BB8` (the game's portal-read wrapper, hooked in
`src/hooks/portal_hook.cpp`, already fully understood per `docs/architecture.md` — a known-good
address to sanity-check against).
Expected: prints `Function: sub_82403BB8 @ 82403bb8`, a decompiled C body, its callers/callees,
and any referenced strings. If the API calls above don't match the installed Ghidra version
exactly (decompiler/reference-manager method names shift occasionally between releases), fix
`ghidra_dump_function.py` against the error message and re-run until output looks right.

- [ ] **Step 4: Commit**

```bash
git add tools/ghidra_dump_function.py tools/ghidra_re.ps1
git commit -m "feat: add headless function-dump script for RE"
```

---

### Task 4: Rename + export (`ghidra_rename_and_export.py` + `rename`)

**Files:**
- Create: `tools/ghidra_rename_and_export.py`
- Modify: `tools/ghidra_re.ps1` (implement the `rename` case)

**Interfaces:**
- Consumes: `tools.rexglue_toml_sync.{sync_functions, quoted_name, decimal_size}` from Task 1;
  the imported project from Task 2.
- Produces: a modified `config/default.toml` on disk.

- [ ] **Step 1: Write `tools/ghidra_rename_and_export.py`**

```python
# Rename one or more functions in the current Ghidra program and sync those
# names into config/default.toml's [functions] section in the same call, so
# renaming and exporting is one non-interactive step.
#
# Usage (via analyzeHeadless -postScript):
#   ghidra_rename_and_export.py <toml path> <hex address> <name> [<hex address> <name> ...]
#
# @category ReXGlue

import os
import sys

from ghidra.program.model.symbol import SourceType

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

from rexglue_toml_sync import decimal_size, quoted_name, sync_functions  # noqa: E402


def main():
    args = getScriptArgs()
    if len(args) < 3 or len(args) % 2 != 1:
        raise Exception(
            "Usage: ghidra_rename_and_export.py <toml path> <hex address> <name> [...]"
        )

    toml_path = args[0]
    pairs = args[1:]

    func_manager = currentProgram.getFunctionManager()
    addr_factory = currentProgram.getAddressFactory()
    updates = {}

    for i in range(0, len(pairs), 2):
        addr_str, name = pairs[i], pairs[i + 1]
        addr = addr_factory.getAddress(addr_str)
        func = func_manager.getFunctionAt(addr)
        if func is None:
            raise Exception("No function at {}".format(addr_str))
        func.setName(name, SourceType.USER_DEFINED)
        updates[addr.getOffset()] = {
            "name": quoted_name(name),
            "size": decimal_size(func.getBody().getNumAddresses()),
        }

    currentProgram.save(monitor)

    with open(toml_path, "r") as f:
        lines = f.readlines()

    result = sync_functions(lines, updates)

    with open(toml_path, "w") as f:
        f.writelines(result.lines)

    println("Export complete.")
    println("  {} existing entries updated".format(len(result.updated)))
    println("  {} new entries added".format(len(result.added)))
    if result.conflicts:
        println("  {} name conflict(s) SKIPPED (fix and re-run):".format(len(result.conflicts)))
        for name, new_addr, existing_addr in result.conflicts:
            println("    {} : 0x{:08X} conflicts with 0x{:08X}".format(
                name, new_addr, existing_addr))
    println("Saved to: {}".format(toml_path))


main()
```

- [ ] **Step 2: Implement the `rename` case in `tools/ghidra_re.ps1`**

Replace `"rename" { throw "'rename' not implemented yet (Task 4)." }` with:

```powershell
    "rename" {
        if ($Rest.Count -lt 2 -or $Rest.Count % 2 -ne 0) {
            throw "Usage: tools/ghidra_re.ps1 rename <address> <name> [<address> <name> ...]"
        }
        $env:JAVA_HOME = Get-JavaHome
        $analyzeHeadless = Get-AnalyzeHeadlessPath
        $tomlPath = Join-Path $RepoRoot "config\default.toml"
        $scriptArgs = @($tomlPath) + $Rest
        & $analyzeHeadless $ProjectDir $ProjectName -process "*" -noanalysis `
            -scriptPath (Join-Path $RepoRoot "tools") `
            -postScript "ghidra_rename_and_export.py" @scriptArgs
    }
```

- [ ] **Step 3: Round-trip verification for real**

Run: `pwsh tools/ghidra_re.ps1 rename 0x82403BB8 XamInputNonControllerGetRaw_wrapper`
Expected:
1. Console reports `1 existing entries updated` or `1 new entries added` (depending on whether
   Task 2's codegen-fix scan already has an entry at that address) and `0` conflicts.
2. `git diff config/default.toml` shows exactly one line changed/added — a
   `0x82403BB8 = { name = "XamInputNonControllerGetRaw_wrapper", ... }` entry — nothing else in
   the file touched.
3. Re-run the exact same command again: confirm `0 existing entries updated`, `0 new entries
   added` (idempotent), and `git diff config/default.toml` shows no further changes.
4. `pwsh tools/ghidra_re.ps1 dump 0x82403BB8` now shows `Function: XamInputNonControllerGetRaw_wrapper @ ...`,
   confirming the rename landed in Ghidra's database, not just the TOML.
5. Re-run codegen (`just build-debug` or the project's normal configure+build step) and grep
   `generated/default/` for `XamInputNonControllerGetRaw_wrapper` — it should appear as a real
   function name in place of `sub_82403BB8`, and the project should still build.
6. `git checkout -- config/default.toml` to discard this smoke-test rename before committing,
   unless it's worth keeping permanently (it's a real, correct name either way).

- [ ] **Step 4: Commit**

```bash
git add tools/ghidra_rename_and_export.py tools/ghidra_re.ps1
git commit -m "feat: add headless rename+export script for RE function names"
```

---

### Task 5: Reverse-engineering docs

**Files:**
- Create: `docs/reverse-engineering.md`
- Modify: `docs/architecture.md:1-5` (add it to the "See also" list at the top)

**Interfaces:**
- Consumes: the workflow built in Tasks 1-4 (subcommand names, arguments, verification steps).
- Produces: nothing further in this plan consumes this; it's the hand-off document for
  sub-project B (finding the ultrawide-relevant functions) and any future RE work.

- [ ] **Step 1: Write the doc**

Create `docs/reverse-engineering.md`:

```markdown
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
pwsh tools/ghidra_re.ps1 setup    # downloads a JDK, Ghidra, and XEXLoaderWV into logs/toolchain/
pwsh tools/ghidra_re.ps1 import   # imports + auto-analyzes rom/default.xex into logs/ghidra_project/
```

Both are idempotent — safe to re-run; `setup` skips anything already downloaded, `import`
re-imports into the same project.

## Workflow

1. `pwsh tools/ghidra_re.ps1 dump <address>` — prints the function's decompiled C, its
   callers/callees, and any referenced string literals. Read this to figure out what the
   function does.
2. `pwsh tools/ghidra_re.ps1 rename <address> <name> [<address> <name> ...]` — renames the
   function(s) in Ghidra's database and syncs `config/default.toml`'s `[functions]` section in
   the same call. Prints how many entries were updated/added and any name conflicts.
3. `git diff config/default.toml` — should show only the entries for functions just renamed,
   nothing else touched.
4. Re-run codegen and rebuild (see `docs/build.md`) to confirm the new names show up in
   `generated/` and the project still builds.
5. Add a one-line comment above any newly-added entry explaining what the function is and how
   it was identified, matching the convention already used throughout
   `config/default.toml`'s `[functions]` section (the exporter itself only merges `name`/`size`,
   it doesn't write comments — that's a manual step after the fact).
6. Commit `config/default.toml` (metadata only — never commit `rom/`, `generated/`, or anything
   under `logs/`; see the "What gets committed" section of
   `docs/superpowers/specs/2026-09-28-re-tooling-design.md`).

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
- **Conflicts reported by `rename`**: two different addresses were given the same name. Ghidra
  function names are unique per-program anyway, so this usually means a stale name on the
  *other* address — check it with `dump` and rename it to something else, or re-run `rename`
  with a different name.
```

- [ ] **Step 2: Link it from architecture.md**

Read `docs/architecture.md:1-5`, then add `docs/reverse-engineering.md` to the "See also" list:

```
How Giants Recompiled is put together, and how the game talks to a Portal of Power. See also:
`docs/portal-protocol.md` (wire format), `docs/figures.md` (figure format and the in-game picker),
`docs/build.md` (codegen fixes and testing), `docs/reverse-engineering.md` (identifying
functions and naming them, headless).
```

- [ ] **Step 3: Commit**

```bash
git add docs/reverse-engineering.md docs/architecture.md
git commit -m "docs: add headless reverse-engineering workflow guide"
```

---

## Self-Review

**Spec coverage:**
- Headless setup (JDK/Ghidra/XEXLoaderWV, no GUI) -> Task 2. Covered.
- `dump`/`rename` headless scripts, TOML merge preserving existing entries -> Tasks 1, 3, 4.
  Covered.
- `docs/reverse-engineering.md` -> Task 5. Covered.
- Data flow (xex -> setup -> import -> dump -> rename -> TOML -> codegen -> generated C++) ->
  exercised end-to-end across Tasks 2-4's real verification runs. Covered.
- What gets committed vs. not -> Global Constraints + every task's commit step only adds
  `tools/*`/`docs/*`, never `logs/`. Covered.
- Round-trip test on `sub_82403BB8` -> Task 4, Step 3, verbatim, plus a `dump` check that the
  rename landed in Ghidra's own database, not just the TOML. Covered.
- Out-of-scope items (finding ultrawide functions, ReXGlue changes, custom loader, IDA) ->
  correctly excluded from every task.

**Placeholder scan:** No TBD/TODO. Every step has real code, a real command, or a concrete
troubleshooting fallback for the specific unknowns this plan can't verify ahead of time
(exact GitHub asset names, exact Ghidra API surface for this installed version) — those are
resolved by actually running the steps, not left as placeholders in the deliverable.

**Type consistency:** `sync_functions(lines, updates) -> SyncResult` used identically in Task 1
(defined, tested) and Task 4 (`ghidra_rename_and_export.py`). `quoted_name`/`decimal_size`
signatures match between definition and both call sites. `tools/ghidra_re.ps1`'s subcommand
signatures (`dump <address>`, `rename <address> <name> [...]`) match exactly what
`ghidra_dump_function.py` and `ghidra_rename_and_export.py` expect from `getScriptArgs()`.

**Review Focus:** all five items have a corresponding test in Task 1:
`test_duplicate_name_collision_reported_and_skipped`,
`test_update_existing_entry_preserves_other_keys`, `test_section_runs_to_end_of_file`,
`test_idempotent_rerun_no_changes`, `test_missing_section_raises` (both classes) — plus Task 4's
manual round-trip re-exercises idempotency and conflict-free operation against the real file.
