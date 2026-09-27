# GiantRecomp dev tooling. Run `just` with no arguments to list every recipe.
#
# Needs: CMake, Ninja, `just`, Git for Windows (for the sh.exe these recipes run under -- its own
# usr/bin isn't on PATH by default, so it's pointed at directly below), and Visual Studio 2022
# with the "C++ Clang tools for Windows" component (its bundled clang is put on PATH below for
# every recipe -- see README.md setup). Works from any shell (PowerShell, cmd, Git Bash) --
# the VS "x64 Native Tools" prompt is not required.

set windows-shell := ["C:/Program Files/Git/usr/bin/sh.exe", "-cu"]

export PATH := env_var('PATH') + ":/c/Program Files/Git/usr/bin:/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin"

# List every recipe
default:
    @just --list

# --- Build -------------------------------------------------------------------

# Configure the debug preset from scratch. Not normally needed by hand -- build-debug does this
# automatically the first time (when out/build/win-amd64-debug doesn't exist yet).
configure-debug:
    cmake --preset win-amd64-debug

configure-release:
    cmake --preset win-amd64-release

# Configure (if not already configured) and build the debug preset.
# Note: recipe lines below without a shebang each run as their own separate shell invocation (no
# shared state) -- just itself still stops the recipe if any line fails, so that's fine as long as
# no line depends on a variable set by an earlier one. Recipes that need that (package-release,
# logs-*) use a real #!/usr/bin/env sh script body instead.
build-debug:
    if [ ! -f out/build/win-amd64-debug/build.ninja ]; then just configure-debug; fi
    cmake --build --preset win-amd64-debug

# Configure (if not already configured) and build the release preset.
build-release:
    if [ ! -f out/build/win-amd64-release/build.ninja ]; then just configure-release; fi
    cmake --build --preset win-amd64-release

# Regenerate recompiled code from rom/default.xex, then reconfigure and rebuild (debug). Run this
# after replacing rom/default.xex with a different dump -- see README.md's "For developers".
regen-debug:
    if [ ! -f out/build/win-amd64-debug/build.ninja ]; then just configure-debug; fi
    cmake --build --preset win-amd64-debug --target giantrecomp_codegen
    cmake --preset win-amd64-debug
    cmake --build --preset win-amd64-debug

regen-release:
    if [ ! -f out/build/win-amd64-release/build.ninja ]; then just configure-release; fi
    cmake --build --preset win-amd64-release --target giantrecomp_codegen
    cmake --preset win-amd64-release
    cmake --build --preset win-amd64-release

# --- Play ----------------------------------------------------------------------

# Build (only if needed -- ninja no-ops when nothing changed) then run the debug build. Extra
# args are passed straight to the game, e.g.:
#   just play-debug --portal_mode usb
play-debug *ARGS: build-debug
    ./out/build/win-amd64-debug/giantrecomp.exe {{ARGS}}

# Build (only if needed) then run the release build -- smoother than debug, prefer this for
# actually playing rather than debugging.
play-release *ARGS: build-release
    ./out/build/win-amd64-release/giantrecomp.exe {{ARGS}}

# --- Package -------------------------------------------------------------------

# Package the release build for copying to another machine: the exe, its 2 runtime DLLs, and the
# settings template -- everything except rom/. This repo ships no game data on purpose (see
# README.md), and the running game reads from rom/ continuously, not just at build time, so your
# own disc dump (rom/default.xex, same version 1.0 USA/EU, plus the rest of the extracted disc)
# has to exist on the target machine too -- bring it over separately and drop it in next to
# giantrecomp.exe there.
package-release: build-release
    dist=dist/giantrecomp-win-amd64-release && \
    rm -rf "$dist" && \
    mkdir -p "$dist" && \
    cp out/build/win-amd64-release/giantrecomp.exe "$dist/" && \
    cp out/build/win-amd64-release/rexgpu-xenos.dll "$dist/" && \
    cp out/build/win-amd64-release/rexruntime.dll "$dist/" && \
    cp giantsrecomp.toml.example "$dist/giantsrecomp.toml" && \
    echo "Packaged to $dist/" && \
    echo "Copy that folder to the other PC, then add your own rom/ next to giantrecomp.exe there" && \
    echo "(rom/default.xex must exist) before running it."

# --- Test ------------------------------------------------------------------

test-debug: build-debug
    ctest --test-dir out/build/win-amd64-debug --output-on-failure

test-release: build-release
    ctest --test-dir out/build/win-amd64-release --output-on-failure

# --- Housekeeping --------------------------------------------------------------

# Delete a preset's entire build output (forces a full reconfigure + rebuild next time you build).
clean-debug:
    rm -rf out/build/win-amd64-debug

clean-release:
    rm -rf out/build/win-amd64-release

clean: clean-debug clean-release

# Init/update every git submodule recursively -- run after a fresh clone, or after pulling a
# branch that added a new one.
submodules:
    git submodule update --init --recursive

# Show the newest debug-build log file. Extra args go straight to `tail`, e.g.:
#   just logs-debug -f        (follow it live)
#   just logs-debug -n 200    (last 200 lines instead of the default 10)
logs-debug *ARGS:
    f=$(ls -t out/build/win-amd64-debug/logs/*.log | head -1) && \
    echo "==> $f" && \
    tail {{ARGS}} "$f"

logs-release *ARGS:
    f=$(ls -t out/build/win-amd64-release/logs/*.log | head -1) && \
    echo "==> $f" && \
    tail {{ARGS}} "$f"
