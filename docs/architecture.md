# Architecture

How Giants Recompiled is put together, and how the game talks to a Portal of Power. See also:
`docs/portal-protocol.md` (wire format), `docs/figures.md` (figure format and the in-game picker),
`docs/build.md` (codegen fixes and testing), `docs/reverse-engineering.md` (identifying
functions and naming them).

## Repository layout

```
GiantsRecomp/
  rom/                       user's own game dump, git-ignored (except rom/README.md)
  giantsrecomp_manifest.toml  ReXGlue project manifest
  config/default.toml        codegen fixes for default.xex (included by the manifest)
  generated/                 codegen output, git-ignored (rexglue.cmake is tracked)
  src/
    game/                    game-specific recompile fixes (kernel stubs, overrides)
    hooks/                   game hooks (ultrawide)
  tests/                     unit tests for the app (XEX verification)
  thirdparty/                rexglue-sdk and skylanders-portal, as git submodules
  docs/
    architecture.md          this file
    portal-protocol.md       wire protocol
    figures.md               figure format, catalog, overlay
    build.md                 codegen fixes and testing
    development.md           building from source
    releasing.md             cutting a release and packaging the installer
    reverse-engineering.md   identifying and naming functions with Ghidra
```

## Portal layering

```
Game (guest, PowerPC) ── XamInputNonControllerGetRaw/SetRaw (xam.xex ordinals 1185/1186)
      │
rexglue-sdk (fork)            NonControllerHandler hook (rex/kernel/xam/noncontroller.h)
      ▼
thirdparty/skylanders-portal  skylanders_portal_rex: settings, XAM handler, F6 overlay,
      │                                             UsbPortal (hidapi), SDL speaker audio
      ▼                       skylanders_portal_core: SoftwarePortal, XamBridge (0B 14 framing),
                                                      figure files, catalog, figure stats
```

The portal emulation lives in the [skylanders-portal](https://github.com/TheBiemGamer/skylanders-portal)
submodule, shared with Trap Team Recompiled. It started as this project's own portal code.

- **`portal::PortalDevice`** (`portal/portal_device.h`): the interface the rest of the portal code
  depends on. A `Report` is a raw 32-byte array with no console-specific framing. A figure
  (`FigureData`) is 64 blocks of 16 bytes (1024 bytes total), matching a real Portal of Power tag.
- **`portal::SoftwarePortal`** (`portal/software/software_portal.cpp`): answers the game's command
  reports and manages up to 16 figure slots. Its control API (`PlaceFigure`, `RemoveFigure`,
  `HasFigure`, `Figure`, `Source`) is thread-safe, so the overlay can drive it from another thread.
- **`portal::UsbPortal`** (`portal/usb/usb_portal.cpp`): opens the first attached device on a small
  VID/PID whitelist via hidapi and relays raw reports both ways. Commands go out through
  `hid_send_output_report()` (a HID `SET_REPORT` control transfer), because the real hardware drops
  interrupt-OUT commands. No driver replacement (WinUSB/Zadig) is needed.
- **`portal::XamBridge`** (`portal/xam_bridge.cpp`): translates the XAM raw reads and writes to a
  `PortalDevice`, adding and removing the Xbox 360 frame header (`0B 14` + 30-byte payload).

Paths above are relative to `thirdparty/skylanders-portal/src/`. See `docs/portal-protocol.md` for
the wire format.

## Hooking the game

The game does not import a portal-specific function. At runtime it resolves two undocumented
`xam.xex` exports by ordinal (`XamInputNonControllerGetRaw`/`SetRaw`, ordinals 1185/1186) and calls
them through two small recompiled wrapper functions, `sub_82403BB8` (read) and `sub_82403C28`
(write). The rexglue-sdk fork implements these exports and forwards them to one handler that the
app registers, so no game address is hooked for the portal.

`skylanders::InstallPortal()` (called from `OnPostSetup`) creates the backend selected by the
`portal_mode` cvar (`software`, `usb`, or `none`) and registers the handler. The backend lives for
the whole process, because game threads may still call into it during shutdown. With
`portal_mode = none` (or an unrecognized value), the game's own "Can't find the Portal of Power"
path runs unchanged. `skylanders::RegisterPortalOverlay()` (called from `OnCreateDialogs`) binds
the F6 overlay.

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
