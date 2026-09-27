# Architecture

How Giants Recompiled is put together, and how the game talks to a Portal of Power. See also:
`docs/portal-protocol.md` (wire format), `docs/figures.md` (figure format and the in-game picker),
`docs/build.md` (codegen fixes and testing).

## Repository layout

```
GiantsRecomp/
  rom/                       user's own game dump, git-ignored (except rom/README.md)
  giantrecomp_manifest.toml  ReXGlue project manifest
  config/default.toml        codegen fixes for default.xex (included by the manifest)
  generated/                 codegen output, git-ignored (rexglue.cmake is tracked)
  src/
    game/                    game-specific recompile fixes (kernel stubs, overrides)
    hooks/                   binds the game's portal calls to a PortalDevice
    portal/                  portal emulation: no ReXGlue dependency, unit-tested
      software/              SoftwarePortal backend (virtual portal)
      usb/                   UsbPortal backend (real hardware over USB HID)
    overlay/                 in-game figure picker (F6)
  tests/                     unit tests, one file per src/portal component
  thirdparty/                rexglue-sdk and hidapi, as git submodules
  docs/
    architecture.md          this file
    portal-protocol.md       wire protocol
    figures.md               figure format, catalog, overlay
    build.md                 codegen fixes and testing
```

## Portal layering

```
Game (guest, PowerPC)
   |  calls two resolved xam.xex ordinals (XamInputNonControllerGetRaw/SetRaw)
   v
src/hooks/portal_hook.cpp   REX_HOOK_RAW on the game's own wrapper functions;
   |                        adds/strips the Xbox 32-byte frame header
   v
portal::PortalDevice        interface: Write(report), Read() -> report
   |-- portal::SoftwarePortal   protocol state machine + up to 16 figure slots
   `-- portal::UsbPortal        forwards raw reports to real hardware over USB HID
```

- **`portal::PortalDevice`** (`src/portal/portal_device.h`): the only thing the rest of the portal
  code depends on. A `Report` is a raw 32-byte array with no console-specific framing. A figure
  (`FigureData`) is 64 blocks of 16 bytes (1024 bytes total), matching a real Portal of Power tag.
- **`portal::SoftwarePortal`** (`src/portal/software/software_portal.cpp`): answers the game's
  command reports and manages up to 16 figure slots. Thread-safe control API (`PlaceFigure`,
  `RemoveFigure`, `HasFigure`, `Figure`, `Source`) so the overlay can drive it from a different
  thread than the one calling `Write`/`Read`. A figure is announced to the game only on the
  inactive-to-active transition; announcing it on every activate command makes the game re-read the
  figure and visibly flicker it off and on. A write callback (`SetWriteCallback`) fires after every
  accepted write command, carrying the slot, the figure's full data, and the file path it was
  loaded from (if any), all captured under one lock so they can never fall out of sync with each
  other during a slot swap.
- **`portal::UsbPortal`** (`src/portal/usb/usb_portal.cpp`): opens the first attached device
  matching a small VID/PID whitelist (`kKnownPortals`) via hidapi and relays raw reports both ways.
  Commands go out through `hid_send_output_report()` — a HID `SET_REPORT` control transfer, not
  `hid_write()` (interrupt OUT): the real hardware silently drops interrupt-OUT commands and only
  reacts to `SET_REPORT`. No driver replacement (WinUSB/Zadig) is needed; the stock HID class
  driver handles it. Status (which slots are occupied, and their detected id/variant) is derived
  passively from replies that already flow through `Read()` as the game polls, with no extra USB
  traffic. Supported and tested against real hardware: the Wii U Traptanium portal (`1430:0150`).
  The Xbox 360 Traptanium portal (`1430:1F17`) is on the same VID/PID whitelist Cemu uses for it,
  but this project has no such hardware to test against.

`src/portal/xbox_frame.h` handles the Xbox 360's specific 32-byte frame (header `0B 14` + 30-byte
payload) that wraps a raw `Report` on the guest side; `PortalDevice` and everything under
`src/portal/` never see that header. See `docs/portal-protocol.md` for the full wire format.

## Hooking the game

The game does not import a portal-specific function. At runtime it resolves two undocumented
`xam.xex` exports by ordinal (`XamInputNonControllerGetRaw`/`SetRaw`, ordinals 1185/1186) and calls
them through two small recompiled wrapper functions, `sub_82403BB8` (read) and `sub_82403C28`
(write). `src/hooks/portal_hook.cpp` uses `REX_HOOK_RAW` directly on those two wrapper functions
rather than overriding the kernel export: overriding the export does not work, because the SDK's
own export registry resolves the name before a project-level override gets a chance. Hooking the
wrapper functions ties the hook to this exact executable, which is why `default.xex` is checked
against a pinned SHA-256 at startup (`src/xex_verify.cpp`) before any hook runs.

`InstallConfiguredPortal` (`src/hooks/portal_hook.cpp`) creates the backend selected by the
`portal_mode` cvar (`software`, `usb`, or `none`) once, at `OnPostSetup`, and it lives for the whole
process — game threads may still call into it during shutdown. With `portal_mode = none` (or an
unrecognized value), the hooks are not installed and the game's own "Can't find the Portal of
Power" path runs unchanged.

## Settings

Runtime configuration is ReXGlue cvars (`REXCVAR_DEFINE_*`), loaded from `giantsrecomp.toml` next
to the executable and overridable with `--flag` or `REX_*` environment variables. See
`giantsrecomp.toml.example` for the full list with descriptions; the notable portal-related ones
are `portal_mode`, `portal_figure`, `portal_figures_dir`, and `portal_test_figure` (development
only — an all-zero test figure the game reports as a problem toy).

`gpu_allow_invalid_fetch_constants` is on by default: ReXGlue's Xenos GPU plugin skips a texture or
draw call whenever a fetch constant's type is "invalid" unless this flag is set, and this game
triggers that path heavily during normal gameplay, which otherwise shows as parts of the scene, or
the whole screen, rendering blank.
