#!/usr/bin/env python3
"""Rename one or more functions in the current Ghidra program and sync
those names into config/default.toml's [functions] section in the same
call, so renaming and exporting is one non-interactive step.

Uses PyGhidra directly (native CPython, no analyzeHeadless -postScript) --
see tools/ghidra_dump_function.py for why: Ghidra 12's .py GhidraScript
provider requires PyGhidra, which is itself a standalone Python library
that starts its own embedded JVM, so there's no separate script-runner
step needed at all.

Usage: python3 ghidra_rename_and_export.py <project_dir> <project_name> <toml path> <hex address> <name> [<hex address> <name> ...]
Requires JAVA_HOME and GHIDRA_INSTALL_DIR to be set (tools/ghidra_re.ps1 sets both).
"""

import os
import sys

import pyghidra

pyghidra.start()

from ghidra.program.model.symbol import SourceType

from rexglue_toml_sync import decimal_size, quoted_name, sync_functions


def main():
    if len(sys.argv) < 6 or len(sys.argv) % 2 != 0:
        raise SystemExit(
            "Usage: ghidra_rename_and_export.py <project_dir> <project_name> <toml path> "
            "<hex address> <name> [<hex address> <name> ...]"
        )
    project_dir, project_name, toml_path = sys.argv[1:4]
    pairs = sys.argv[4:]

    with pyghidra.open_project(os.path.abspath(project_dir), project_name) as project:
        with pyghidra.program_context(project, "/default.xex") as program:
            func_manager = program.getFunctionManager()
            addr_factory = program.getAddressFactory()
            updates = {}

            with pyghidra.transaction(program, "Rename functions"):
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

            program.save("Renamed functions", pyghidra.task_monitor())

    with open(toml_path, "r") as f:
        lines = f.readlines()

    result = sync_functions(lines, updates)

    with open(toml_path, "w") as f:
        f.writelines(result.lines)

    print("Export complete.")
    print("  {} existing entries updated".format(len(result.updated)))
    print("  {} new entries added".format(len(result.added)))
    if result.conflicts:
        print("  {} name conflict(s) SKIPPED (fix and re-run):".format(len(result.conflicts)))
        for name, new_addr, existing_addr in result.conflicts:
            print("    {} : 0x{:08X} conflicts with 0x{:08X}".format(
                name, new_addr, existing_addr))
    print("Saved to: {}".format(toml_path))


if __name__ == "__main__":
    main()
