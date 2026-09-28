# Development

Building GiantsRecomp from source. If you just want to play, download the installer instead — see
`README.md`.

## What you need

1. **A 64-bit PC.** Windows 10 or 11 with a graphics card that supports DirectX 12 is the main target. Linux works too (see [Setup](#setup)) with a Vulkan 1.x capable GPU, but is newer and rougher.
2. **Your own copy of the game:** the Xbox 360 disc of Skylanders: Giants, **version 1.0 (the USA and Europe release)**, extracted to a folder on your PC (for example with a disc-extraction tool such as `extract-xiso` or `xdvdfs`). The program checks the game file against a fingerprint at startup and refuses to run any other version.
3. **A controller.** An Xbox controller works.
4. **Build tools** (see the setup below): on Windows, Visual Studio 2022 with the C++ tools, CMake, Ninja and Git. On Linux, clang 20+, CMake, Ninja and Git. [`just`](https://github.com/casey/just) is optional on either platform but simplifies the commands below a lot.

## Setup

A [`justfile`](../justfile) wraps every command below (`just build-debug`, `just build-release`, `just play-debug`, `just play-release`, ...; run `just` with no arguments to list them all) and picks the right preset for your OS automatically. It's optional — the plain `cmake`/`ctest` commands below always work too — but shortens most of this section to one command.

### Windows

**1. Turn on symlinks (once).** One of the libraries uses a Windows feature called symbolic links. Turn on **Developer Mode** (Settings, System, For developers), then run:

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
cmake --build --preset win-amd64-debug --target giantsrecomp_codegen
cmake --preset win-amd64-debug
cmake --build --preset win-amd64-debug
```

The second command converts *your* `default.xex` into C++ code on your own machine. That generated code is never uploaded. The third command is needed so the build notices it.

**Optional, but recommended for actually playing:** repeat the same four steps with `win-amd64-release` instead of `win-amd64-debug`. The Release build is optimized and noticeably smoother; use Debug only if something crashes and you want to capture more detail for a bug report.

### Linux (experimental)


**1. Install the build tools.** You need clang 20 or newer (both `clang-20` and `clang++-20` on `PATH`), CMake 3.25 or newer, Ninja and Git — e.g. on Arch, `sudo pacman -S clang20 cmake ninja git`. Some distros' packages don't add the versioned `clang-20`/`clang++-20` names to `PATH` even though they install the binaries (Arch's `clang20` package is one; check where it put `clang++` and symlink `clang-20`/`clang++-20` onto it somewhere on `PATH`, such as `~/.local/bin`, if `which clang-20` comes up empty).

**2. Download the code, including its libraries:**

```
git clone --recursive https://github.com/TheBiemGamer/GiantsRecomp.git
cd GiantsRecomp
```

If you already cloned without `--recursive`, run `git submodule update --init --recursive`.

**3. Put your game files in place.** Copy the extracted disc into the `rom` folder so that `rom/default.xex` exists. 


**4. Build.** The first build takes a long time because it also builds the ReXGlue SDK.

```
cmake --preset linux-amd64-debug
cmake --build --preset linux-amd64-debug --target giantsrecomp_codegen
cmake --preset linux-amd64-debug
cmake --build --preset linux-amd64-debug
```

The second command converts *your* `default.xex` into C++ code on your own machine. That generated code is never uploaded. The third command is needed so the build notices it.

**Optional, but recommended for actually playing:** repeat the same four steps with `linux-amd64-release` instead of `linux-amd64-debug`. The Release build is optimized and noticeably smoother; use Debug only if something crashes and you want to capture more detail for a bug report.

With `just` installed, all of the above (both platforms) is just `just build-debug` or `just build-release`.

## For developers

- `giantsrecomp_manifest.toml` and `config/default.toml` are the project settings and the fixes needed to convert `default.xex` correctly.
- `src/portal/` is the portal emulation (no dependency on the SDK, with unit tests in `tests/`). `src/hooks/` connects it to the game. `src/game/` holds other game-specific code.
- `thirdparty/rexglue-sdk` is the ReXGlue SDK, included as a git submodule.
- `docs/architecture.md` covers the repository layout and portal architecture, `docs/portal-protocol.md` the wire protocol, `docs/figures.md` the figure format and overlay, `docs/build.md` the codegen fixes and testing.
- Run the unit tests with `ctest --test-dir out/build/win-amd64-debug` (`linux-amd64-debug` on Linux), or `just test-debug`.

See also: `docs/architecture.md` (repository layout and portal architecture), `docs/build.md`
(codegen fixes and testing), `docs/releasing.md` (cutting a release).
