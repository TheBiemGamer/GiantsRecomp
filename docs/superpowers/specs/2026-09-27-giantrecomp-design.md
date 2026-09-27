# GiantRecomp Design

Date: 2026-09-27
Status: Draft, pending review

## 1. Goal

Produce a native Windows PC port of Skylanders Giants (Xbox 360) by static recompilation with the ReXGlue SDK. The port supports two ways of providing a Portal of Power:

1. A **software portal**, driven from an in-game overlay.
2. **Real portal passthrough** over USB, for Wii U and Xbox 360 Traptanium portals (both owned by the author).

The project follows the shape of [DimensionsRecomp](https://github.com/NeverCookFirst/DimensionsRecomp): the repository contains no game assets, and users supply their own dump.

### Success criteria

- The game boots to the title screen and is playable on Windows (D3D12).
- With the software portal, the game detects a portal, and figures can be placed, swapped, removed and saved from the overlay during play.
- With a real Traptanium portal (Wii U or Xbox 360 model), the game reads and writes real figures.

### Non-goals for v1

- Linux or macOS support.
- Portal speaker audio and Traptanium trap features. Giants does not use them. The interface keeps room for speaker audio later.
- A localhost web UI for figure selection (may follow the overlay later; the portal core is frontend-agnostic).

## 2. Inputs and constraints

- Source binary: `rom/default.xex`, extracted from the author's own disc. `rom/` is git-ignored, except its README.
- Toolchain: ReXGlue SDK (installed locally), Visual Studio 2022 with clang 19+, CMake 3.25+, Ninja, x64 developer environment.
- The SHA-256 of `default.xex` is recorded in the repository docs and checked at startup. Hooks that reference game function addresses only fit one build, so a mismatch is refused.
- Recompiled output (`generated/`) is derived from the game's code and is git-ignored. Users generate it locally. Codegen output goes to `generated/default/`.
- Figure dumps and any game data are never committed.

## 3. Licensing approach

Cemu (`src/Cafe/OS/libs/nsyshid/Skylander*.cpp`, `Backend*.cpp`) and RPCS3 (`rpcs3/Emu/Io/Skylander.cpp`) are used as references for the portal protocol, the Xbox 360 framing and the figure format. Their licenses appear to be copyleft (Cemu MPL-2.0, RPCS3 GPL-2.0), which has not been verified from the repositories.

Decision (recommended, pending author confirmation): read them to learn protocol facts and re-implement independently, and keep this repository MIT. Copying their code would bind this project to their licenses.

## 4. Architecture

### 4.1 Repository layout

```
GiantRecomp/
  rom/                      user's dump, git-ignored
  giantrecomp_manifest.toml ReXGlue project manifest
  config/default.toml       codegen fixes for default.xex (included by the manifest)
  generated/                codegen output, git-ignored (rexglue.cmake is tracked)
  src/
    game/                   game-specific recompile fixes (overrides, mid-asm hooks)
    hooks/                  guest-facing hooks binding the game to a PortalDevice
    portal/
      software/             SoftwarePortal backend
      usb/                  UsbPortal backend and transports
    overlay/                in-game figure picker
  docs/superpowers/specs/
```

### 4.2 Portal layering

```
Game (guest)
   |  HID-style reads/writes with Xbox framing
   v
hooks/PortalHook       adds/strips Xbox headers; owns the guest-facing API
   |  raw portal reports
   v
PortalDevice           interface: Read(report), Write(report)
   |-- SoftwarePortal  protocol state machine + figure slots
   `-- UsbPortal       forwards raw reports to hardware
         `-- Transport converts wire framing to and from raw reports
               |-- HidTransport    Wii U / PS3 / Wii-style portals, plain HID
               `-- XusbTransport   Xbox 360 portals, with Xbox framing
```

Components:

- **PortalDevice**: speaks raw portal reports only. It knows nothing about Xbox framing or the recompiler.
- **SoftwarePortal**: answers the game's commands (activate, status, read block, write block, LEDs) and manages up to 16 figure slots with queued state transitions (removed, added, ready, removing). It exposes a thread-safe control API: `LoadFigure`, `RemoveFigure`, `CreateFigure`.
- **UsbPortal**: forwards reports to a real portal through a Transport chosen by USB VID/PID. Because framing is handled by the hook and the transport, a Wii U portal works with the Xbox 360 game without a matching-console portal. Adding a portal model means adding a table entry, or a transport if it frames differently.
- **FigureStore**: reads and writes the 1024-byte figure dump (64 blocks of 16 bytes), one file per figure. Progress saves back to that file.
- **FigureCodec**: tag crypto and CRC16 that make blocks valid, required to create new figures. Pure library, no game dependency.
- **FigureCatalog**: maps figure ID and variant to a display name.
- **PortalHook**: the only component that depends on how the game reaches the portal (see 5, milestone 3).

Driver notes: the Wii U portal should appear as a normal HID device and be reachable with SDL's hidapi. The Xbox 360 portal uses Microsoft's Xbox 360 peripheral driver, so Windows likely needs it replaced with WinUSB (for example through Zadig) and needs libusb or direct WinUSB access, which is not in the build yet (libusb was not found at SDK configure time).

Reference facts from Cemu, to be re-verified against hardware and traces: the Xbox 360 portal is a HID device (VID `0x1430`, PID `0x0150` as Cemu presents it), with one interrupt-in and one interrupt-out endpoint; reports are wrapped in Xbox-specific headers; the underlying portal protocol is the same as on other consoles.

### 4.3 Hooking strategy

ReXGlue provides `REX_HOOK` / `REX_HOOK_RAW` to override recompiled game functions (original still callable via `__imp__sub_XXXXXXXX`), `REX_EXPORT` to implement kernel and XAM exports, and `REX_STUB_LOG` for logged no-op stubs.

Two possible hook levels for the portal:

1. **Kernel/XAM level (preferred)**: if the game reaches the portal through an import (an input or USB call), implement that export with `REX_EXPORT` and route it to `PortalHook`. Stable and independent of game addresses.
2. **Game-function level (fallback)**: if the game's portal driver is its own code over generic calls, `REX_HOOK` its portal read/write functions. Works, but tied to this exact executable.

The choice is made in milestone 3 by inspecting imports in the generated code and comparing against a Xenia run with kernel-call logging.

### 4.4 Runtime integration

- The app is a `ReXApp`. The portal is created in `OnPostSetup` and torn down in `OnShutdown`.
- The overlay is a `PortalDialog` registered in `OnCreateDialogs(ImGuiDrawer*)`, opened by a hotkey that does not collide with F3 (Debug), F4 (Settings) or backtick (Console).
- Configuration uses ReXGlue CVars, loaded from a TOML file next to the executable, with `REX_*` environment variables overriding it:
  - `portal_mode`: `none`, `software` or `usb` (`usb` is not implemented yet). Default `software`.
  - `portal_test_figure`: development only. Puts an all-zero figure in slot 0; the game reports it as a problem toy. Default `false`.
  - `portal_usb_device`: optional VID/PID choice when several are attached.
  - `portal_figures_dir`: where figure files live and are saved.
- `portal_mode = none` is a valid state: the game runs with no portal attached, reaches its title screen, and shows "Can't find the Portal of Power" when the player presses A to start, so the recompile milestone does not depend on portal work.

### 4.5 Overlay

- Shows 16 slots. Each slot can load a figure file, create a new figure from `FigureCatalog`, or remove one.
- Navigable with gamepad and mouse.
- Talks only to `SoftwarePortal`'s control API, never to protocol bytes. With a real USB portal active, it shows portal status and disables figure controls.
- Threading: the overlay runs on the UI thread and the game reads on its own thread. Slot state is mutex-guarded, and figure changes are queued as state transitions so the game sees a clean placement.

## 5. Milestones

1. **Toolchain.** Done: ReXGlue SDK builds and installs.
2. **Recompile and boot.** Run `rexglue codegen` on `default.xex`, fix `config/default.toml` (function boundaries, switch tables, setjmp/longjmp), and stub then implement kernel imports until the game runs and renders. The game reaches its title screen ("Press A to start"), which is the milestone-2 finish line (recompiled code, graphics, audio and file access all working). With no portal attached, pressing A shows "Can't find the Portal of Power"; getting past that check is milestone 4. Record the XEX SHA-256 and add the startup check.
3. **Find the portal API.** Determine how the game reaches the portal (imports, report formats, any enumeration handshake). Decide the hook level (4.3). Main reference: Cemu's Xbox 360 Skylander code.
4. **Minimal SoftwarePortal.** The game accepts the portal (handshake, status polling, LEDs) and reads a figure's 64 blocks. A test figure with zeroed data is reported as a problem toy; valid figure data is milestone 5.
5. **Figure library.** Dump import, new-figure creation, `FigureCodec`, `FigureStore`, per-figure saves.
6. **Overlay.** Hotkey figure picker (4.5).
7. **UsbPortal.** `HidTransport` for the Wii U Traptanium portal first, then `XusbTransport` for the Xbox 360 one. Verify with real figures that Traptanium portals stay compatible with Giants.

Each milestone ends with something runnable. Milestones 2 and 3 carry the real unknowns.

## 6. Testing

- **Unit tests (no game or GPU):**
  - `FigureCodec` against known-good dumps supplied by the user.
  - `SoftwarePortal` protocol state machine driven by scripted report sequences.
  - `FigureStore` round-trips.
- **Hardware tests (manual):** `UsbPortal` on the Wii U and Xbox 360 Traptanium portals in milestone 7.
- **Game checks:** M2 boots to the title screen; M4 the game sees a figure; M5 and M6 figures can be swapped in the overlay during play.
- **Fixture rule:** tests needing game assets or figure dumps skip cleanly when those files are absent. None are committed.

## 7. Risks and open questions

- **Recompile difficulty.** Unresolved functions, jump tables and missing kernel imports are expected. ReXGlue's graphics, audio and input backends are marked as in flux, so some early failures may be SDK issues.
- **Title update.** Whether Giants needs a patched executable, and how ReXGlue handles a separate patch file, is unknown. To check in milestone 2 (ReXGlue Discord).
- **Portal API and handshake.** The game polls `XamInputNonControllerGetRaw` (about 350 calls in 20 seconds) and shows a "Can't find the Portal of Power" screen when the player presses A at the title screen until a portal answers; this is the likely hook point (see `docs/investigation/portal-api.md` once milestone 3 is done). If the game only recognizes the portal after a specific USB enumeration handshake, `SoftwarePortal` must reproduce it. Found in milestone 3.
- **Xbox 360 portal access on Windows.** Needs a driver swap and libusb or WinUSB, not yet in the build.
- **Traptanium compatibility.** Assumed backwards compatible with Giants figures and protocol; verified in milestone 7.
- **Licensing.** Section 3 decision needs author confirmation.
