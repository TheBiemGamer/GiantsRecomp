# Giants Recompiled

An unofficial PC port of the Xbox 360 version of Skylanders Giants, made by static recompilation with [ReXGlue](https://github.com/rexglue/rexglue-sdk). It comes with a **virtual Portal of Power**, so you can play without the real toy hardware. Support for real portals is planned.

This repository contains **no game code and no game data**. You need your own copy of the game.

## About this project

This is a solo, for-fun project to see whether a static recompilation of Skylanders Giants was possible. It's early but playable — see [What works](#what-works-and-what-doesnt) below for where it stands. It isn't meant to compete with playing the game on Cemu, and realistically it may never surpass that experience; the point was the challenge, not replacing an existing emulator.

## What works, and what doesn't

**Works today**
- The game starts, renders, plays sound, and plays through Story mode with a controller.
- A virtual Portal of Power with one of your own figures on it. Progress on that figure (levelling up, upgrades) is saved back to its `.dump` file, so it carries over between play sessions.

**Not yet**
- Only one figure at a time, chosen before you start. A menu to swap figures while playing is planned.
- Real portals over USB (Wii U and Xbox 360 Traptanium portals are the targets).
- It can be slow, especially in the everyday (Debug) build (see [Playing](#playing) for the faster Release build). Expect stutter the first time an effect appears in a scene; it should be smoother the second time. Cutscenes are the heaviest part and lag the most.
- Keyboard and mouse. Use a controller.
- Modes other than Story have not been tested. Linux and macOS are not supported.

## What you need

1. **A Windows 10 or 11 PC (64-bit)** with a graphics card that supports DirectX 12.
2. **Your own copy of the game:** the Xbox 360 disc of Skylanders: Giants, **version 1.0 (the USA and Europe release)**, extracted to a folder on your PC (for example with a disc-extraction tool such as `extract-xiso` or `xdvdfs`). The program checks the game file against a fingerprint at startup and refuses to run any other version.
3. **A controller.** An Xbox controller works.
4. **Figure dumps (optional but recommended):** raw 1024-byte `.dump` files of your own figures, made with a real portal and a dumping tool. They are not included here.
5. **Build tools** (see step 3 of the setup below): Visual Studio 2022 with the C++ tools, CMake, Ninja and Git.

## Setup

**1. Turn on symlinks (Windows only, once).** One of the libraries uses a Windows feature called symbolic links. Turn on **Developer Mode** (Settings, System, For developers), then run:

```
git config --global core.symlinks true
```

**2. Download the code, including its libraries:**

```
git clone --recursive https://github.com/TheBiemGamer/GiantsRecomp.git
cd GiantsRecomp
```

If you already cloned without `--recursive`, run `git submodule update --init --recursive`.

**3. Install the build tools.** You need Visual Studio 2022 with the "Desktop development with C++" workload and the "C++ Clang tools for Windows" component (clang 18 or newer), plus CMake 3.25 or newer and Ninja. From now on, run every command in the **x64 Native Tools Command Prompt for VS 2022** (search for it in the Start menu).

**4. Put your game files in place.** Copy the extracted disc into the `rom` folder so that `rom\default.xex` exists. That folder is ignored by git.

**5. Build.** The first build takes a long time because it also builds the ReXGlue SDK.

```
cmake --preset win-amd64-debug
cmake --build --preset win-amd64-debug --target giantrecomp_codegen
cmake --preset win-amd64-debug
cmake --build --preset win-amd64-debug
```

The second command converts *your* `default.xex` into C++ code on your own machine. That generated code is never uploaded. The third command is needed so the build notices it.

**Optional, but recommended for actually playing:** repeat the same four steps with `win-amd64-release` instead of `win-amd64-debug`. The Release build is optimized and noticeably smoother; use Debug only if something crashes and you want to capture more detail for a bug report.

## Playing

Run the game from the project folder (`out\build\win-amd64-release` if you built Release, `win-amd64-debug` otherwise):

```
out\build\win-amd64-release\giantrecomp.exe
```

That's it if `rom\default.xex` exists — no flags needed for the common case. Press **A** at the title screen, then choose **Story** and a slot marked **NEW**. To quit, close the window.

### Settings file

Copy [`giantsrecomp.toml.example`](giantsrecomp.toml.example) next to `giantrecomp.exe` and rename it to `giantsrecomp.toml` to change any of the following without typing flags every time (the in-game **Settings** overlay, **F4**, reads and writes this same file):

- `portal_mode`: `"software"` (default) uses the virtual portal; `"none"` disables it and the game says it can't find one.
- `portal_figures_dir`: the folder the in-game figure picker searches and creates new figures in. Press **F6** to open it (mouse only for now): browse your dumps, or create a new figure by name, grouped by game and release order. Leave empty (the default) to use a `figures` folder next to your saves.
- `portal_figure`: puts one of your own figure dumps on the portal at startup (slot 0). **The game's changes to the figure are saved back to this exact file as you play**, the same as a real portal would. Use a copy if you want to keep the original untouched.
- `gpu_allow_invalid_fetch_constants`: works around a GPU quirk that can otherwise make parts of the scene, or the whole screen, render blank. Recommended until the underlying cause is fixed (see `docs/investigation/boot-issues.md`).
- `user_data_root`: where saves, achievements, shader cache, and the default figures folder live. Leave empty (the default) to use `Documents\giantsrecomp`.

Any setting can still be passed as a command-line flag instead (`--portal_mode software`), which overrides whatever the settings file has. One exception: `--game_data_root` (where `rom\` lives) can't be set from the settings file — it's read before the file loads — but it already defaults to `rom` next to the executable, so you only need the flag if your dump lives somewhere else.

`--portal_test_figure` (command line only, for development) puts an all-zero test figure on the portal; the game reports it as a problem toy.

## If something goes wrong

| What you see | What it means |
| --- | --- |
| A window saying it "Cannot read ... default.xex" | The game folder is wrong. Check that `rom\default.xex` exists. |
| "default.xex is not the supported build" | Your disc is a different version. Only version 1.0 (USA and Europe) works. |
| "Can't find the Portal of Power" | No portal is active. Don't use `--portal_mode none`. |
| "A toy on the Portal of Power has a problem" | The figure file is not a valid dump, or you used `--portal_test_figure`. |
| The build stops with an "undefined symbol" error | The build didn't pick up the generated code. Run the third build command again (the second `cmake --preset win-amd64-debug`), then the last one. |
| A file error about `lzxd.c` during the build | Symlinks were off when you cloned. Do step 1, then re-clone or run `git submodule update --init --recursive`. |
| The game closes suddenly | This is an early build. Please note which screen you were on and open an issue. |

## For developers

- `giantrecomp_manifest.toml` and `config/default.toml` are the project settings and the fixes needed to convert `default.xex` correctly.
- `src/portal/` is the portal emulation (no dependency on the SDK, with unit tests in `tests/`). `src/hooks/` connects it to the game. `src/game/` holds other game-specific code.
- `thirdparty/rexglue-sdk` is the ReXGlue SDK, included as a git submodule.
- `docs/` has the design (`docs/superpowers/specs/`), the implementation plans, and notes on how the game and portal behave (`docs/investigation/`).
- Run the unit tests with `ctest --test-dir out/build/win-amd64-debug`.

## AI usage

Claude was used heavily throughout this project. This is my first time doing a static recompilation, and having an AI assistant to work through problems with sped things up enormously — I wouldn't have gotten nearly this far, this fast, without it.

I know AI assistance is a sore subject for some people, and that's a completely fair position to hold. If that's a dealbreaker for you, I understand. For me, this project was about learning how recompilation works, and Claude was a tool that helped me get there faster, not a replacement for understanding what the code does.

## Credits

This project stands on other people's work.

- **[DimensionsRecomp](https://github.com/NeverCookFirst/DimensionsRecomp)** by NeverCookFirst was the inspiration for this project, and I looked at its code and approach. It brought LEGO Dimensions to PC with a virtual toy pad and real-portal support, built on ReXGlue, and this project follows the same idea for Skylanders.
- **[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)** by Tom Clay is the recompiler and runtime everything here is built on. It derives from the [Xenia](https://xenia.jp) Xbox 360 emulator (Ben Vanik and the Xenia contributors) and is inspired by [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) by hedge-dev. It is included unmodified as a submodule and keeps its own license (see `thirdparty/rexglue-sdk/LICENSE`).
- **[Cemu](https://github.com/cemu-project/Cemu)** and **[RPCS3](https://github.com/RPCS3/rpcs3)** already emulate the Skylanders Portal of Power. I read how they handle it to learn how the portal works: the commands, the Xbox 360 message format, and the layout of a figure. No code was copied from them. Each project has its own license.
- Everyone who documented the Portal of Power and its figures and made dumping tools that let people keep their own figures.
- **[Skylanders Ultimate NFC Pack V15](https://skylandersnfc.github.io/Skylanders-Ultimate-NFC-Pack/)** is the source of the figure dumps used to build the game's built-in Skylander name/id catalog, used for the in-game figure creator and picker.

## License and legal

The code and documentation in this repository are released under the [MIT license](LICENSE). That does not cover the game or its data, which you must own and supply yourself, or the third-party components in `thirdparty/`, which keep their own licenses.

This is an unofficial fan project. It is not affiliated with, endorsed by, or connected to Activision, Toys for Bob, or the owners of Skylanders. Skylanders and related names are trademarks of their owners. Nothing from the game is distributed here.