# GiantRecomp

An unofficial PC port of the Xbox 360 version of Skylanders Giants, made by static recompilation with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). Work in progress: the recompiled game runs and renders, but it stops on its own "Can't find the Portal of Power" screen because portal support is not written yet.

This repository contains no game code or assets. You need your own copy of the game.

## What you need

- **The Xbox 360 disc of Skylanders Giants, version 1.0 (the USA and Europe release).** Extract it yourself; nothing from the game is distributed here. The program checks `default.xex` against the hash in `docs/game/default.xex.sha256` at startup and refuses to run on any other build.
- Windows 10 or 11 (x64).
- Visual Studio 2022 with the "Desktop development with C++" workload and the C++ Clang tools (clang 18 or newer), CMake 3.25 or newer, and Ninja. Run the commands below from an **x64 Native Tools Command Prompt for VS 2022**.
- Plenty of free disk space: the SDK and its dependencies are large, and the first build compiles a lot.

## Getting the source

The ReXGlue SDK is included as a git submodule (`thirdparty/rexglue-sdk`, pinned to v0.10.0). Its sources use symlinks, so on Windows enable them **before you clone**: turn on Developer Mode (Settings, System, For developers), then run:

```
git config --global core.symlinks true
git clone --recursive https://github.com/TheBiemGamer/GiantsRecomp.git
```

If you already cloned without `--recursive`, run `git submodule update --init --recursive`.

## Building

1. Extract your game disc into `rom/` so that `rom/default.xex` exists. `rom/` is git-ignored.
2. Configure: `cmake --preset win-amd64-debug`
3. Generate the recompiled code from your `default.xex` (this also builds the SDK's `rexglue` tool the first time, which takes a while): `cmake --build --preset win-amd64-debug --target giantrecomp_codegen`
4. Configure again so the generated code is picked up: `cmake --preset win-amd64-debug`
5. Build: `cmake --build --preset win-amd64-debug`
6. Run: `out\build\win-amd64-debug\giantrecomp.exe --game_data_root rom`

If the `thirdparty/rexglue-sdk` submodule is missing, the build falls back to an SDK installed into CMake's package registry. Use `-DREXSDK_DIR=<path>` to point at another SDK checkout.

## Layout

- `giantrecomp_manifest.toml` and `config/default.toml`: ReXGlue project manifest and the codegen fixes for `default.xex`.
- `src/`: the app and the startup check. `src/game/`: game-specific runtime code.
- `thirdparty/rexglue-sdk`: the ReXGlue SDK (submodule).
- `docs/`: design spec, implementation plans, and investigation notes (`docs/investigation/`).
