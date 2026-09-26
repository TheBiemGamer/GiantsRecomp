# Boot issues and workarounds (milestone 2)

Findings from getting `default.xex` (Title ID 415608DA, version 0.0.0.2, no title update) to boot on ReXGlue 0.10.0.

## Result

The recompiled game runs for minutes without crashing, renders through the Xenos GPU plugin, plays audio, reads game files, and stops on its own "Can't find the Portal of Power" screen. It cannot reach the title screen without a portal (milestone 4).

## SDK behaviour that needed workarounds

### Functions reached only through virtual calls were never registered

ReXGlue's GapFill phase splits uncovered code on `blr` and on tail-call `b`, but not on `bctr`. Tiny virtual-method leaves that follow a `bctr` (for example a run of getters like `lwz; blr` after a `bctr` thunk) end up inside another function's claimed extent and are never registered. The first call through a vtable then stops the game with `Call to invalid or unregistered function at guest address 0x...`.

Workaround: `[functions]` entries in `config/default.toml`, one per address the runtime reports, each sized by decoding forward from the address to its terminator. The entries are commented in that file. 18 were needed to get to the portal screen, and more may appear as later game paths run.

### Jump tables with no bounds check were cut short

When a `bctr` index comes from masked bits with no compare-and-branch, the analyzer guessed 4 entries for tables that have 11. The generated `switch` then hit `__builtin_trap()` (`Switch case out of range`) at runtime. Four sites in the bit-stream decoder (`sub_821658B8`, `sub_8216A958` and two more) were fixed with `[[switch_tables]]` entries. The full list of generated `switch` sites with a trap default was checked by table size; only sites with 4 or 8 cases looked suspect.

### GPU plugin is off by default

Without `rexglue_setup_target(... GPU_PLUGINS xenos)` and `gpu_plugin = "xenos"`, the `Vd*` ring-buffer calls are ignored and nothing renders. `CMakeLists.txt` links the plugin and `GiantrecompApp::OnPreSetup` defaults the cvar.

## Warnings seen and not yet understood

- 20 codegen warnings, `Unexpected float16_4 pack instruction at 8241DF2C ...` (through `8241EAB8`). No crash traced to them so far. Watch for wrong vertex data once the game draws 3D scenes.
- Repeated GPU warnings `Texture fetch constant (...) has "invalid" type`. The menu screen still draws correctly. Xenia offers `--gpu_allow_invalid_fetch_constants` for this; ReXGlue's equivalent, if any, is untested.
- `NtCreateFile` for `D:\character\.bld` fails with `0xc000000f` (file not found). Likely an optional file the game probes for.

## Portal observations (input for milestone 3)

- The game calls `XamInputNonControllerGetRaw` continuously. ReXGlue logs it as `STUB`. This is the leading candidate for how the game reads the portal.
- The game shows "Can't find the Portal of Power. Is the wired Portal of Power plugged in to a USB connector?" and does not respond to keyboard input while no portal answers.

## Tooling used (not committed)

Debug helpers lived in the git-ignored `logs/` folder: a script that boots the game, reads the fatal address, sizes the function from a dump of the loaded guest image and adds the `[functions]` entry, and a script that finds truncated jump tables. Guest crashes were located by logging the faulting RVA and resolving it with `llvm-symbolizer`. The dumping and crash-logging code was temporary and removed.
