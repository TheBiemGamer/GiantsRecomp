# Boot issues and workarounds (milestone 2)

Findings from getting `default.xex` (Title ID 415608DA, version 0.0.0.2, no title update) to boot on ReXGlue 0.10.0.

## Result

The recompiled game runs for minutes without crashing, renders through the Xenos GPU plugin, plays audio, reads game files, and reaches its title screen ("Press A to start"). Pressing A with no portal attached shows "Can't find the Portal of Power", so getting into the game needs the portal work in milestone 4.

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
- ~~Repeated GPU warnings `Texture fetch constant (...) has "invalid" type`~~ **Resolved 2026-09-28:** ReXGlue has the same cvar as Xenia, `gpu_allow_invalid_fetch_constants` (bool, default off), settable on the command line as `--gpu_allow_invalid_fetch_constants` (no value needed). With it off, `src/graphics/pipeline/texture/cache.cpp` and the vertex-fetch equivalent in `command_processor.cpp` skip the texture or the whole draw when a fetch constant's type is "invalid", which is the likely cause of the "screen goes blank during gameplay" the author reported. The warning fired over 100 times a second during a 95 s play session (9655 times total); with the flag on, it never fires and no new FATAL appeared in a 25 s smoke test. Now on by default in both launcher `.cmd` files. Still to confirm: whether it actually fixes the visible blanking during real play (needs the author to test), and why so many fetch constants end up "invalid" in the first place (likely an uninitialized-register quirk inherited from Xenia's xenos emulation, not something specific to this game).
- `NtCreateFile` for `D:\character\.bld` fails with `0xc000000f` (file not found). Likely an optional file the game probes for.

## Portal observations (input for milestone 3)

- The game calls `XamInputNonControllerGetRaw` continuously. ReXGlue logs it as `STUB`. This is the leading candidate for how the game reads the portal.
- On the title screen, pressing A on a controller with no portal attached shows "Can't find the Portal of Power. Is the wired Portal of Power plugged in to a USB connector?" (reported by the author, who pressed A on their controller). Keyboard keys sent by my automated runs did not advance the title screen, so keyboard input is probably not mapped; I did not test a controller myself.

## Tooling used (not committed)

Debug helpers lived in the git-ignored `logs/` folder: a script that boots the game, reads the fatal address, sizes the function from a dump of the loaded guest image and adds the `[functions]` entry, and a script that finds truncated jump tables. Guest crashes were located by logging the faulting RVA and resolving it with `llvm-symbolizer`. The dumping and crash-logging code was temporary and removed.

## More virtual-only functions, found once the portal worked (milestone 4)

With a portal answering, the game runs code that never ran before, and each new path hit another `Call to invalid or unregistered function` fatal. `config/default.toml` now has 75 `[functions]` entries in total. A batch of them came from scanning the loaded image for pointers into code that are not function starts. Rules learned the hard way:

- **Data-table pointers** (virtual method tables) were reliable once sized correctly. A function's size has to follow forward jumps to non-function addresses, or codegen rejects the result with "target not in any function".
- **Import thunks** (`0x8261B000` and up) also show up as pointers into code. They are imports, not game functions, and registering them causes undefined-symbol link errors.
- **Jump tables** sit inline in the code range, and their entries look like code addresses. Candidates whose first word is itself a code address must be skipped.
- **Pointers built in code** (`lis` + `addi`/`ori`) produced many false positives, mostly labels in the middle of large functions. Registering one splits the containing function and breaks its branches (`Unresolved branch from ... to ...`). Only candidates that are not strictly inside a function that has unwind (PDATA) information were kept.
- Entries in the config must not overlap; the loader refuses the whole manifest otherwise.
