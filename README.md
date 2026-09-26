# GiantRecomp

An unofficial PC port of the Xbox 360 version of Skylanders Giants, made by static recompilation with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). Work in progress: the recompiled game runs and renders, but it stops on its own "Can't find the Portal of Power" screen because portal support is not written yet.

This repository contains no game code or assets. You need your own copy of the game.

## Building (Windows)

You need Visual Studio 2022 with the C++ Clang tools, CMake 3.25+, Ninja, and the ReXGlue SDK built and installed (see its [Getting Started](https://github.com/rexglue/rexglue-sdk/wiki/Getting-Started) page). Use an **x64 Native Tools** prompt.

1. Extract your own game disc into `rom/` so that `rom/default.xex` exists. `rom/` is git-ignored.
2. Configure: `cmake --preset win-amd64-debug`
3. Generate the recompiled code from your `default.xex`: `cmake --build --preset win-amd64-debug --target giantrecomp_codegen`
4. Configure again so the generated code is picked up: `cmake --preset win-amd64-debug`
5. Build: `cmake --build --preset win-amd64-debug`
6. Run: `out\build\win-amd64-debug\giantrecomp.exe --game_data_root rom`

Only the disc build recorded in `docs/game/default.xex.sha256` is supported. The program checks `default.xex` against that hash at startup and refuses to run on any other build.

## Layout

- `giantrecomp_manifest.toml` and `config/default.toml`: ReXGlue project manifest and the codegen fixes for `default.xex`.
- `src/`: the app and the startup check. `src/game/`: game-specific runtime code.
- `docs/`: design spec, implementation plans, and investigation notes (`docs/investigation/`).
