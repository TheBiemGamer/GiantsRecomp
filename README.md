# Giants Recompiled

[![License: MIT](https://img.shields.io/github/license/TheBiemGamer/GiantsRecomp)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-informational)](#what-you-need)


An unofficial PC port of the Xbox 360 version of Skylanders Giants, made by static recompilation with [ReXGlue](https://github.com/rexglue/rexglue-sdk). It supports both the actual **portal of power** and a **virtual Portal of Power**, so you can play without the real toy hardware.

This repository contains **no game data**. You need your own copy of the game.

## About this project

This is a solo, for-fun project to see whether a static recompilation of Skylanders Giants was possible. It's early but playable — see [What works](#what-works-and-what-doesnt) below for where it stands. It isn't meant to compete with playing the game on Cemu, and realistically it may never surpass that experience; the point was the challenge, not replacing an existing emulator.

## What works, and what doesn't

**Works today**
- The game starts, renders, plays sound, and plays through Story mode with a controller.
- A virtual Portal of Power with your own figures on it, up to 16 at once (matching the real portal's slots).
- A real, physical Portal of Power over USB (`portal_mode = "usb"`) — tested with a Wii U Traptanium portal.
- Dumping your own figures with a real portal: with the portal connected, the **Dump to file** button saves a copy of that figure to your figures folder. You can then use that dump on the virtual portal without the toy. Dumping only reads the figure: the physical toy isn't changed.
- Keyboard/mouse controller emulation (`mnk_mode`) see [Keybinds](#keybinds).
- Ultrawide monitors: set `resolution` to your monitor's resolution (e.g. `"3440x1440"`) and the game renders the full width with a correctly widened field of view, with no black bars. The HUD and menus are still stretched to fit the wider screen; a fix is being investigated.

**Not yet**
- It can be slow, especially in the Debug build (see [Playing](#playing) for the faster Release build). Expect stutter the first time an effect appears in a scene; it should be smoother the second time. Cutscenes are the heaviest part and lag the most.

- Modes other than Story have not been tested.
- **Linux builds and runs** (see [Setup](docs/development.md#setup)), but it's newer and rougher than the Windows build: it's noticeably slower than Windows even on the same hardware, and there's an unresolved rendering bug where the screen renders with an incorrect red tint from the title screen onward. On a laptop with both an integrated and a discrete GPU, also check the `vulkan_device` tip in [Settings file](#settings-file) — the automatic GPU pick has no preference for the discrete GPU and can end up on the weaker one. macOS has build presets but hasn't been tried by anyone.

## What you need

1. **A 64-bit PC.** Windows 10 or 11 with a graphics card that supports DirectX 12 is the main target. Linux works too (see [Setup](docs/development.md#setup)) with a Vulkan 1.x capable GPU, but is newer and rougher.
2. **Your own copy of the game:** the Xbox 360 disc of Skylanders: Giants, **version 1.0 (the USA and Europe release)**, extracted to a folder on your PC (for example with a disc-extraction tool such as `extract-xiso` or `xdvdfs`) — or just the ISO file, if you're using the installer below. The program checks the game file against a fingerprint at startup and refuses to run any other version.

4. **Build tools**, but only if building from source (see [Development](docs/development.md)) rather than using the installer below: on Windows, Visual Studio 2022 with the C++ tools, CMake, Ninja and Git. On Linux, clang 20+, CMake, Ninja and Git.

## Installing

Download the latest `GiantsRecompSetup.exe` from
[Releases](https://github.com/TheBiemGamer/GiantsRecomp/releases), run it, and point it at your
own Skylanders Giants disc — either the ISO file directly, or an already-extracted folder. The
installer also lets you choose your Portal of Power mode and display resolution up front (anything
else can be changed later in-game with F4), and adds a Start Menu shortcut.

Building from source instead (for development, or if you don't trust a prebuilt binary) is covered
in `docs/development.md`.

## Playing

If you installed via `GiantsRecompSetup.exe`, use the Start Menu shortcut. If you built from
source, run it from the project folder (`out\build\win-amd64-release` if you built Release, `win-amd64-debug` otherwise; `out/build/linux-amd64-release` etc. on Linux, no `.exe`):

```
out\build\win-amd64-release\giantsrecompiled.exe   # Windows
out/build/linux-amd64-release/giantsrecompiled     # Linux
```

Or, with `just`: `just play-release` (or `just play-debug`) — builds first if needed, then runs it.

That's it if `rom\default.xex` exists — no flags needed for the common case.

### Settings file

Your settings live in a file called `giantsrecomp.toml`, in the same folder as `giantsrecompiled.exe`. The easiest way to change them is in-game: press **F4** to open the Settings overlay, which reads and writes this file for you.


Where to find it:

- **Installed with `GiantsRecompSetup.exe`:** the installer already created it in `%LOCALAPPDATA%\Programs\Giants Recompiled` 
- **Built from source:** the build creates it in your build folder (e.g. `out\build\win-amd64-release`) the first time, as a copy of [`giantsrecomp.toml.example`](giantsrecomp.toml.example). It never overwrites a file that's already there.

The settings you're most likely to want:

- `portal_mode`: `"software"` (default) uses the virtual portal; `"usb"` uses a real, physically connected Portal of Power (tested with a Wii U Traptanium portal) — just plug it in, no driver changes needed; `"none"` disables the portal.

- `gpu_allow_invalid_fetch_constants`: works around a GPU quirk that can otherwise make parts of the scene, or the whole screen, render blank. Recommended until the underlying cause is fixed.
- `user_data_root`: where saves, achievements, shader cache, and the default figures folder live. Leave empty (the default) to use `Documents\giantsrecomp`.
- `frame_rate_limit`: caps the host frame rate to this many FPS. `0` (the default) is unlimited.
- `resolution_scale`: supersamples the internal render resolution by this factor (`1`-`8`) before downscaling to your window/monitor — sharper, at a real GPU cost. `1` (the default) is no scaling.
- `resolution`: sets the startup window size, e.g. `"3440x1440"` for an ultrawide monitor or `"4k"`.
- `ui_scale`: scales the ImGui overlays (**F3**/**F4**/**F6**/**F7**, console) font size and widget sizing. `1.0` (the default) is unchanged; try `1.5` or higher if the overlay text looks too small on a large or high-resolution monitor.
- `vulkan_device`: only for linux - picks which GPU renders the game by index, for a PC with more than one (e.g. a laptop with both an integrated and a discrete GPU). `-1` (the default) auto-selects.
- `mnk_mode`: enables keyboard/mouse controller emulation. `true` (the default) turns it on; set to `false` to require a real controller. See [Keybinds](#keybinds) for the default key mapping.

Any setting can still be passed as a command-line flag instead (`--portal_mode software`), which overrides whatever the settings file has. One exception: `--game_data_root` (where `rom\` lives) can't be set from the settings file — it's read before the file loads — but it already defaults to `rom` next to the executable, so you only need the flag if your dump lives somewhere else.


## Keybinds

These work regardless of input device:

| Key | Action |
| --- | --- |
| **F3** | FPS / frame time overlay |
| **F4** | Settings overlay — edit `giantsrecomp.toml` live |
| **F6** | Portal of Power menu — virtual portal figure picker, or real portal status and figure dumping |
| **F7** | Achievements overlay |

### Keyboard/mouse controller emulation

With `mnk_mode = true` (the default), the keyboard emulates a controller using these keys. Each is
rebindable via its own `keybind_*` setting (e.g. `keybind_a = "Semicolon,Space"`) — a comma-separated
list means either key works.

| Controller | Default key(s) |
| --- | --- |
| A | `Semicolon` or `Space` |
| B | `Quote` or `Backspace` |
| X | `L` |
| Y | `P` |
| Left trigger | `Q` or `I` |
| Right trigger | `E` or `O` |
| Left shoulder | `1` |
| Right shoulder | `3` |
| Left stick | `W` `A` `S` `D` |
| Left stick press | `F` |
| Right stick | Arrow keys |
| Right stick press | `K` |
| D-pad | `Shift` + arrow keys |
| Back | `Z` or `Tab` |
| Start | `X` or `Return` |
| Guide | unbound |


## AI usage

Claude was used heavily throughout this project. This is my first time doing a static recompilation, and having an AI assistant to work through problems with sped things up enormously — I wouldn't have gotten nearly this far, this fast, without it.

I know AI assistance is a sore subject for some people, and that's a completely fair position to hold. If that's a dealbreaker for you, I understand. For me, this project was about learning how recompilation works, and Claude was a tool that helped me get there faster, not a replacement for understanding what the code does.

## Credits

This project stands on other people's work.

- **[DimensionsRecomp](https://github.com/NeverCookFirst/DimensionsRecomp)** by NeverCookFirst was the inspiration for this project, and I looked at its code and approach. It brought LEGO Dimensions to PC with a virtual toy pad and real-portal support, built on ReXGlue, and this project follows the same idea for Skylanders.
- **[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)** by Tom Clay is the recompiler and runtime everything here is built on. It derives from the [Xenia](https://xenia.jp) Xbox 360 emulator (Ben Vanik and the Xenia contributors) and is inspired by [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) by hedge-dev. It is included unmodified as a submodule and keeps its own license (see `thirdparty/rexglue-sdk/LICENSE`).
- **[Cemu](https://github.com/cemu-project/Cemu)** and **[RPCS3](https://github.com/RPCS3/rpcs3)** already emulate the Skylanders Portal of Power. I read how they handle it to learn how the portal works: the commands, the Xbox 360 message format, and the layout of a figure. No code was copied from them. Each project has its own license.

- **[Skylanders Ultimate NFC Pack V15](https://skylandersnfc.github.io/Skylanders-Ultimate-NFC-Pack/)** is the source of the figure dumps used to build the game's built-in Skylander name/id catalog, used for the in-game figure creator and picker.
- **[extract-xiso](https://github.com/XboxDev/extract-xiso)** is vendored (prebuilt, unmodified) in `installer/` to unpack an Xbox 360 ISO during installation. It keeps its own license (`installer/extract-xiso.LICENSE.txt`).
- Figure save-data decoding (level, gold, nickname) is built on public reverse-engineering of the Skylanders NFC format: **[SkyReader](https://github.com/reedstrm/SkyReader)** and Marijn Kneppers' **["Reverse engineering Skylanders' Toys-to-life mechanics"](https://marijnkneppers.dev/posts/reverse-engineering-skylanders-toys-to-life-mechanics/)**. 

- Everyone who documented the Portal of Power and its figures and made dumping tools that let people keep their own figures.

## License and legal

The code and documentation in this repository are released under the [MIT license](LICENSE). That does not cover the game or its data, which you must own and supply yourself, or the third-party components in `thirdparty/`, which keep their own licenses.

This is an unofficial fan project. It is not affiliated with, endorsed by, or connected to Activision, Toys for Bob, or the owners of Skylanders. Skylanders and related names are trademarks of their owners. No game data is distributed here; release binaries contain recompiled code only.