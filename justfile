# GiantRecomp dev tooling. Run `just` with no arguments to list every recipe.
#
# Needs: CMake, Ninja, `just`, and clang-20/clang++-20 on PATH.
#   - Windows: Visual Studio 2022 with the "C++ Clang tools for Windows" component (its bundled
#     clang is put on PATH below), plus Git for Windows (its own usr/bin isn't on PATH by default,
#     so it's pointed at directly below -- these recipes run under its sh.exe). Works from any
#     shell (PowerShell, cmd, Git Bash); the VS "x64 Native Tools" prompt is not required.
#   - Linux: clang-20/clang++-20 on PATH. Some distros (e.g. Arch's `clang20` package) install them
#     unversioned under a version-specific directory (e.g. /usr/lib/llvm20/bin/clang++, no
#     "-20" suffix) -- add that directory to PATH, or symlink clang-20/clang++-20 onto it yourself.
#   - macOS: Xcode's clang normally suffices.

set windows-shell := ["C:/Program Files/Git/usr/bin/sh.exe", "-cu"]

export PATH := if os() == "windows" {
    env_var('PATH') + ":/c/Program Files/Git/usr/bin:/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin"
} else {
    env_var('PATH')
}

# Preset/output-directory triple for the host OS -- matches the *-amd64-* preset names in
# CMakePresets.json (win-amd64, linux-amd64, mac-amd64). Not parameterized for arm64: neither was
# the original Windows-only version of this file.
triple := if os() == "windows" { "win-amd64" } else if os() == "macos" { "mac-amd64" } else { "linux-amd64" }

# Host executable suffix: ".exe" on Windows, empty everywhere else.
exe_suffix := if os() == "windows" { ".exe" } else { "" }

# Where the SDK puts its own shared libraries (librexruntime, libTracyClient, ...), matching
# rexglue-sdk/CMakeLists.txt's CMAKE_RUNTIME_OUTPUT_DIRECTORY. On Windows and macOS, the SDK's
# rexglue_configure_target() copies these next to the host executable as a post-build step, so
# out/build/<triple>-*/ already has everything it needs. On Linux it only stages the GPU plugin
# that way, not the runtime libraries themselves -- so play-*/package-release point
# LD_LIBRARY_PATH/DYLD_LIBRARY_PATH at this directory instead of relying on a copy that doesn't
# happen. Harmless to set on every platform.
sdk_lib_dir := "thirdparty/rexglue-sdk/out/" + triple

# List every recipe
default:
    @just --list

# --- Build -------------------------------------------------------------------

# Configure the debug preset from scratch. Not normally needed by hand -- build-debug does this
# automatically the first time (when out/build/<triple>-debug doesn't exist yet).
configure-debug:
    cmake --preset {{ triple }}-debug

configure-release:
    cmake --preset {{ triple }}-release

# Configure (if not already configured) and build the debug preset.
# Note: recipe lines below without a shebang each run as their own separate shell invocation (no
# shared state) -- just itself still stops the recipe if any line fails, so that's fine as long as
# no line depends on a variable set by an earlier one. Recipes that need that (package-release,
# logs-*) chain their lines with && on one shell line instead.
build-debug:
    if [ ! -f out/build/{{ triple }}-debug/build.ninja ]; then just configure-debug; fi
    cmake --build --preset {{ triple }}-debug

# Configure (if not already configured) and build the release preset.
build-release:
    if [ ! -f out/build/{{ triple }}-release/build.ninja ]; then just configure-release; fi
    cmake --build --preset {{ triple }}-release

# Regenerate recompiled code from rom/default.xex, then reconfigure and rebuild (debug). Run this
# after replacing rom/default.xex with a different dump -- see README.md's "For developers".
regen-debug:
    if [ ! -f out/build/{{ triple }}-debug/build.ninja ]; then just configure-debug; fi
    cmake --build --preset {{ triple }}-debug --target giantrecomp_codegen
    cmake --preset {{ triple }}-debug
    cmake --build --preset {{ triple }}-debug

regen-release:
    if [ ! -f out/build/{{ triple }}-release/build.ninja ]; then just configure-release; fi
    cmake --build --preset {{ triple }}-release --target giantrecomp_codegen
    cmake --preset {{ triple }}-release
    cmake --build --preset {{ triple }}-release

# --- Play ----------------------------------------------------------------------

# Build (only if needed -- ninja no-ops when nothing changed) then run the debug build. Extra
# args are passed straight to the game, e.g.:
#   just play-debug --portal_mode usb
play-debug *ARGS: build-debug
    LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:{{ sdk_lib_dir }}" DYLD_LIBRARY_PATH="${DYLD_LIBRARY_PATH:-}:{{ sdk_lib_dir }}" ./out/build/{{ triple }}-debug/giantrecomp{{ exe_suffix }} {{ ARGS }}

# Build (only if needed) then run the release build -- smoother than debug, prefer this for
# actually playing rather than debugging.
play-release *ARGS: build-release
    LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:{{ sdk_lib_dir }}" DYLD_LIBRARY_PATH="${DYLD_LIBRARY_PATH:-}:{{ sdk_lib_dir }}" ./out/build/{{ triple }}-release/giantrecomp{{ exe_suffix }} {{ ARGS }}

# --- Package -------------------------------------------------------------------

# Package the release build for copying to another machine: the executable, its runtime shared
# libraries, and the settings template -- everything except rom/. This repo ships no game data on
# purpose (see README.md), and the running game reads from rom/ continuously, not just at build
# time, so your own disc dump (rom/default.xex, same version 1.0 USA/EU, plus the rest of the
# extracted disc) has to exist on the target machine too -- bring it over separately and drop it in
# next to the packaged executable there.
package-release: build-release
    dist=dist/giantrecomp-{{ triple }}-release && \
    rm -rf "$dist" && \
    mkdir -p "$dist" && \
    cp out/build/{{ triple }}-release/giantrecomp{{ exe_suffix }} "$dist/" && \
    for lib in out/build/{{ triple }}-release/*.dll out/build/{{ triple }}-release/*.so out/build/{{ triple }}-release/*.dylib {{ sdk_lib_dir }}/*.so {{ sdk_lib_dir }}/*.dylib; do [ -e "$lib" ] && cp "$lib" "$dist/"; done && \
    cp giantsrecomp.toml.example "$dist/giantsrecomp.toml" && \
    echo "Packaged to $dist/" && \
    echo "Copy that folder to the other machine, then add your own rom/ next to giantrecomp{{ exe_suffix }} there" && \
    echo "(rom/default.xex must exist) before running it."

# --- Test ------------------------------------------------------------------

test-debug: build-debug
    ctest --test-dir out/build/{{ triple }}-debug --output-on-failure

test-release: build-release
    ctest --test-dir out/build/{{ triple }}-release --output-on-failure

# --- Housekeeping --------------------------------------------------------------

# Delete a preset's entire build output (forces a full reconfigure + rebuild next time you build).
clean-debug:
    rm -rf out/build/{{ triple }}-debug

clean-release:
    rm -rf out/build/{{ triple }}-release

clean: clean-debug clean-release

# Init/update every git submodule recursively -- run after a fresh clone, or after pulling a
# branch that added a new one.
submodules:
    git submodule update --init --recursive

# Show the newest debug-build log file. Extra args go straight to `tail`, e.g.:
#   just logs-debug -f        (follow it live)
#   just logs-debug -n 200    (last 200 lines instead of the default 10)
logs-debug *ARGS:
    f=$(ls -t out/build/{{ triple }}-debug/logs/*.log | head -1) && \
    echo "==> $f" && \
    tail {{ ARGS }} "$f"

logs-release *ARGS:
    f=$(ls -t out/build/{{ triple }}-release/logs/*.log | head -1) && \
    echo "==> $f" && \
    tail {{ ARGS }} "$f"
