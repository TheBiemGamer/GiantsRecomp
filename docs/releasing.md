# Releasing

How to cut a release: build the binary, package the Windows installer, and publish it. No CI is
involved — every step here runs on the maintainer's own machine, against their own legally-owned
copy of the game (see `docs/superpowers/specs/2026-09-28-installer-design.md` for why).

## Build and package

```
just package-installer
```

This builds the release binary (`just build-release`), stages it with the installer's other
inputs, and compiles `installer/GiantRecomp.iss` with Inno Setup, producing
`installer/Output/GiantRecompSetup.exe`. See `installer/GiantRecomp.iss` and the `package-installer`
recipe in `justfile` for what it does under the hood. Needs Inno Setup 6 installed (download from
jrsoftware.org, or `winget install JRSoftware.InnoSetup`) — the recipe checks both of its common
install locations and reports clearly if it can't find `ISCC.exe`.

## Before publishing, verify

- Fresh, clean Windows VM (no Visual Studio/CMake/Ninja installed) — install with a real ISO, and
  separately with an already-extracted folder; confirm the game launches both ways.
- A wrong-version `default.xex` is rejected with a clear message -- before any copying for an
  already-extracted folder; after the (unavoidable) full extraction for an ISO, since extract-xiso
  has no way to check the version without extracting first.
- Re-running the installer over an existing install preserves `rom/`, `giantsrecomp.toml`, and
  saves, and only replaces the binary.
- Uninstalling leaves `rom/` and the toml in place unless the opt-in prompt is answered "Yes";
  `unins000.exe /VERYSILENT` never prompts and defaults to leaving them.
- The Start Menu shortcut launches the game, and an "Uninstall GiantRecomp" entry appears in both
  the Start Menu and Windows' Add/Remove Programs.

## Publish

Upload `installer/Output/GiantRecompSetup.exe` to a new GitHub Release. Note in the release
description which exact game version/region the installer was built and tested against.

## Bump the version

`installer/GiantRecomp.iss`'s `MyAppVersion` define should match the release tag before compiling.
