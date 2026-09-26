# How Skylanders Giants reaches the portal

Findings for `default.xex` (Title ID 415608DA, version 0.0.0.2), gathered while running the recompiled game with no portal attached. Everything below marked *observed* comes from the generated code or from a run; anything marked *inferred* is a reading of that evidence.

## Xenia baseline

Not done. The port itself shows the no-portal behaviour, so the optional Xenia comparison was skipped.

## Candidate imports

The game's import table (from the executable image) has no portal-specific import. Its `xam` input imports are only these four:

- `XamInputGetCapabilities` (0x190), `XamInputGetState` (0x191), `XamInputSetState` (0x192), `XamInputGetKeystrokeEx` (0x198)

The portal is not reached through them. Instead the game resolves two undocumented `xam.xex` exports **at runtime**, by ordinal, using `XexGetModuleHandle` and `XexGetProcedureAddress`:

| Ordinal | SDK name | Role |
|---|---|---|
| 1185 (0x4A1) | `XamInputNonControllerGetRaw` | read a report from a non-controller USB device |
| 1186 (0x4A2) | `XamInputNonControllerSetRaw` | write a report to it |

*Observed* in `generated/default`: `sub_82403B18` loads the string `xam.xex` (guest address 0x82001198), gets its module handle, then calls the ordinal resolver `sub_8240A020` with `r4 = 1185` and `r4 = 1186`.

## Observed calls

The SDK implements both functions as logged no-ops (`REX_EXPORT_STUB` in `xam_input.cpp`), so the runtime logs `__imp__XamInputNonControllerGetRaw STUB` about 60 times a second, once per frame. `SetRaw` is never called while no portal answers.

The game reaches them through two small recompiled wrappers. Wrapping them with a temporary logging hook showed:

- `sub_82403BB8(r3, r4, r5)` calls `GetRaw(r3, r4, r5)`.
  - `r3`: pointer to a 32-bit value, `0` on entry. Role not yet known.
  - `r4`: pointer to a 32-bit value equal to `0x20` (32). *Inferred*: the buffer length.
  - `r5`: buffer pointer (guest `0xBB32C7A0`, a 32-byte-sized buffer; the same address on every call).
  - The wrapper returns `1` when the underlying call returned `0` and `0` otherwise. With the SDK stub, `r3` is left non-zero, so the game sees failure. *Inferred*: a return of `0` means a report was read into the buffer.
- `sub_82403C28(r3, r4)` calls `SetRaw(r3, r4)` in the same style. Its arguments could not be observed because it is never called without a portal.
- If a function pointer is missing, both wrappers use the error code `1627` and return failure.

## Hook level decision

**Game-function level**, on the two wrappers `sub_82403BB8` (read) and `sub_82403C28` (write).

Evidence:

- A kernel/XAM-level override was tried first and does not work from this project. Registering our own `XamInputNonControllerGetRaw` with `PPCFuncRegistrar` had no effect: the runtime still called the SDK's stub, because `XexGetProcedureAddress` resolves names in the SDK library's own registry. Overriding these exports would need SDK support.
- A `REX_HOOK_RAW` on the game's own wrapper works today: it ran on every call, could read the argument buffers in guest memory, and could call the original through `__imp__sub_82403BB8`.
- The wrappers are the only game code that calls through the resolved pointers for reading and writing (`sub_822308F0`, `sub_82232E58` and `sub_821BDF68` read the same table, which needs a second look in milestone 4).

Trade-off: these hooks are tied to this exact executable, which the startup SHA-256 check already pins.

## Handshake and detection

- *Observed*: `sub_82403B18` is the initializer, called at the start of both wrappers. It stores three words at `0x8265BA9C`: `+0` an availability flag (1 only if both pointers resolved), `+4` the `GetRaw` function pointer, `+8` the `SetRaw` function pointer. `sub_824CF918` and `sub_8260D5A0` read the flag.
- *Observed*: the game polls `GetRaw` every frame with a 32-byte buffer and shows "Can't find the Portal of Power. Is the wired Portal of Power plugged in to a USB connector?" until a call succeeds. Keyboard input does not dismiss the screen.
- *Not yet known*: what the first bytes of a valid report must look like for the game to accept the portal, and whether it sends an activate command through `SetRaw` before treating the portal as present. These come from the portal protocol and from a real capture (milestone 4).

## Open questions

1. What is the role of the first argument of `GetRaw` (a pointer to a zero on entry)? A device or port selector, or an out-parameter?
2. Are reports exactly 32 bytes? Cemu's Skylander code treats a full status snapshot as 64 bytes, and the 360 wraps reports in extra headers, so the relation between the 32-byte buffer and the portal protocol needs checking against a real portal.
3. What does `SetRaw`'s second argument point to (buffer layout and length)?
4. What do `sub_822308F0`, `sub_82232E58` and `sub_821BDF68` do with the table entries?
5. Does the game call `XamInputNonControllerGetRawEx` / `SetRawEx` (ordinals 0x52A, 0x52B) on any path? Not seen in this run.

References: Cemu's `SkylanderXbox360.cpp` was read for portal protocol facts only; no code was copied.
