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
