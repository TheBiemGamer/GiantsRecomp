# Giants Recompiled

An unofficial PC port of the Xbox 360 version of Skylanders Giants, made by static recompilation with [ReXGlue](https://github.com/rexglue/rexglue-sdk). It supports both a real Portal of Power and a virtual one, so you can play without the toy hardware.

This repository contains **no game data**. You need your own copy of the game.


This is a solo, for-fun project to see whether a static recompilation of Skylanders Giants was possible. It's early but playable. It isn't meant to compete with playing the game on Cemu, and realistically it may never surpass that experience. The point was the challenge, not replacing an existing emulator.


## Getting started

Windows is the main target. You'll need a 64-bit PC running Windows 10 or 11, with a graphics card that supports DirectX 12. You'll also need your own copy of the Xbox 360 disc of *Skylanders: Giants*, **version 1.0**, either as an ISO file or already extracted to a folder. The game checks your copy when it starts and won't run any other version.

Once you have that:

1. Download `GiantsRecompSetup.exe` from [Releases](https://github.com/TheBiemGamer/GiantsRecomp/releases).
2. Run it and point it at your ISO or extracted folder.
3. Choose your Portal of Power mode and display resolution.
4. Launch **Giants Recompiled** from the Start Menu.

Anything else can be changed later in-game by pressing **F4**.

**Linux:** it's possible to build and run it on Linux with a Vulkan 1.x capable GPU, but it's experimental, so use it at your own risk. There's no installer, so you'll need to build it yourself (see [Building from source](#building-from-source)).

---

## Portal of Power

Pick how you want to use the portal with `portal_mode`. You can set it in the installer, in-game with **F4**, or in `giantsrecomp.toml`.

| Mode | What it does |
| --- | --- |
| `"software"` (default) | A virtual portal with your own figures, up to 16 at once. Press **F6** to pick figures. |
| `"usb"` | Uses a real Portal of Power over USB. Just plug it in, no driver changes needed. Tested with a Wii U Traptanium portal. |
| `"none"` | No portal at all. |

**Want to use your real figures on the virtual portal?** Plug in a real portal, press **F6**, and click **Dump to file**. That saves a copy of the figure to your figures folder, and you can use it without the toy afterwards. Dumping only reads the figure, so it won't change anything on it.

---

## Controls

These keys work no matter what you're playing with:

| Key | What it does |
| --- | --- |
| **F3** | Shows FPS and frame time |
| **F4** | Opens the settings overlay |
| **F6** | Portal of Power menu (figure picker, or real portal status and dumping) |
| **F7** | Achievements |

### Playing with keyboard and mouse

With `mnk_mode = true` (the default), your keyboard works as a controller. You can rebind any key with its own `keybind_*` setting, for example `keybind_a = "Semicolon,Space"`. A comma-separated list means either key works.

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
| Guide | Not bound |

---

## Settings

The easiest way to change settings is to press **F4** in-game. Everything is saved in `giantsrecomp.toml`, which lives next to `giantsrecompiled.exe`:

- **If you used the installer:** `%LOCALAPPDATA%\Programs\Giants Recompiled`
- **If you built from source:** in your build folder (for example `out\build\win-amd64-release`). It's created the first time you run the game, as a copy of [`giantsrecomp.toml.example`](giantsrecomp.toml.example), and an existing file is never overwritten.

| Setting | What it does |
| --- | --- |
| `portal_mode` | `"software"`, `"usb"` or `"none"`. See [Portal of Power](#portal-of-power). |
| `gpu_allow_invalid_fetch_constants` | Fixes parts of the scene (or the whole screen) showing up blank. Recommended until I find the real cause. |
| `resolution` | Window size when the game starts, like `"3440x1440"` or `"4k"`. |
| `resolution_scale` | Renders at 1x to 8x before scaling down to your screen. Looks sharper but is harder on your GPU. Default is `1`. |
| `frame_rate_limit` | Caps the FPS. `0` (the default) means no limit. |
| `ui_scale` | Makes the F3/F4/F6/F7 overlays bigger or smaller. Try `1.5` or higher on a big or high-res monitor. Default is `1.0`. |
| `mnk_mode` | Keyboard and mouse controller emulation. Set to `false` if you only want to use a real controller. Default is `true`. |
| `ultrawide_hud` | On a screen wider than 16:9, moves the HUD to the screen edges and keeps menus in proportion. Default is `true`. |
| `skip_intro` | Skips the Activision and Toys for Bob logo movies at startup. Default is `true`. |
| `user_data_root` | Where saves, achievements, shader cache and figures go. Leave it empty to use `Documents\giantsrecomp`. |
| `vulkan_device` | **Linux only.** Picks which GPU to use, by index. `-1` (the default) chooses automatically. |

You can also pass any setting as a command-line flag (like `--portal_mode software`), and that overrides the settings file. The one exception is `--game_data_root` (where `rom\` lives). It can only be a flag, and it already defaults to `rom` next to the executable.

---

## Something not working?

| Problem | What to try |
| --- | --- |
| The game won't start and says it's the wrong version | Only Skylanders Giants **v1.0 (USA/EU)** works. |
| Parts of the screen, or all of it, are blank | Set `gpu_allow_invalid_fetch_constants = true`. |
| Overlay text is too small | Raise `ui_scale` in the F4 menu. |
| It's slow or stuttery | Use the **Release** build. Some stutter the first time an effect shows up is normal and gets better the second time. Cutscenes are the heaviest part. |
| Ultrawide: the picture or HUD looks stretched | Set `resolution` to your monitor's resolution, since that's what the ultrawide fixes use. Cutscene movies still stretch for now. |
| Linux: the screen has a red tint | Known bug, and it isn't fixed yet. It shows up from the title screen onward. |
| Linux laptop: it runs slowly | The automatic GPU pick can land on the integrated GPU. Set `vulkan_device` to your discrete GPU's index. |

---

## Where things stand

**Works today**
- The game starts, renders, plays sound, and you can play through **Story mode** with a controller
- Virtual portal with up to 16 figures, and a real USB portal
- Dumping your own figures
- Keyboard and mouse controls
- Ultrawide resolutions, with a wider camera and the HUD at the screen edges
- Skipping the startup logo movies

**Not there yet**
- It can be slow, especially in the Debug build
- Modes other than Story haven't been tested
- **Linux** builds and runs, but it's rougher than Windows: noticeably slower on the same hardware, plus the red tint bug above
- **macOS** has build presets, but nobody has tried it yet

---

## Building from source

You only need this if you're developing or would rather not use a prebuilt binary. The full guide is in [`docs/development.md`](docs/development.md).

- **Windows:** Visual Studio 2022 (with the C++ tools), CMake, Ninja and Git
- **Linux:** clang 20+, CMake, Ninja and Git

Extract your game into `rom\` so that `rom\default.xex` exists (tools like `extract-xiso` or `xdvdfs` work for this). Then run:

```
just play-release
```

That builds the game if needed and launches it. Use `just play-debug` for the Debug build. You can also run the executable directly:

```
out\build\win-amd64-release\giantsrecompiled.exe   # Windows
out/build/linux-amd64-release/giantsrecompiled     # Linux
```

---

## A note on AI

I used Claude a lot while making this. It's my first static recompilation, and having an AI to work through problems with sped things up enormously. I wouldn't have gotten this far, this fast, without it.

I know AI is a sore subject for some people, and that's a completely fair position. For me, this project was about learning how recompilation works, and Claude was a tool that helped me get there faster, not a replacement for understanding what the code does.

## Credits

This project stands on other people's work.

- **[DimensionsRecomp](https://github.com/NeverCookFirst/DimensionsRecomp)** by NeverCookFirst inspired this project, and I looked at its code and approach. It brought LEGO Dimensions to PC with a virtual toy pad and real-portal support, built on ReXGlue.
- **[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)** by Tom Clay is the recompiler and runtime everything here is built on. It comes from the [Xenia](https://xenia.jp) emulator (Ben Vanik and contributors) and is inspired by [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) by hedge-dev. It's included unmodified as a submodule and keeps its own license (`thirdparty/rexglue-sdk/LICENSE`).
- **[Cemu](https://github.com/cemu-project/Cemu)** and **[RPCS3](https://github.com/RPCS3/rpcs3)** already emulate the Portal of Power. I read how they handle it to learn the commands, the Xbox 360 message format, and the figure layout. No code was copied, and each project has its own license.
- **[Skylanders Ultimate NFC Pack V15](https://skylandersnfc.github.io/Skylanders-Ultimate-NFC-Pack/)** provided the figure dumps used to build the built-in Skylander name/id catalog for the figure creator and picker.
- **[extract-xiso](https://github.com/XboxDev/extract-xiso)** is included (prebuilt, unmodified) in `installer/` to unpack an Xbox 360 ISO during installation. It keeps its own license (`installer/extract-xiso.LICENSE.txt`).
- Figure save-data decoding (level, gold, nickname) builds on public reverse-engineering of the Skylanders NFC format: **[SkyReader](https://github.com/reedstrm/SkyReader)** and Marijn Kneppers' **["Reverse engineering Skylanders' Toys-to-life mechanics"](https://marijnkneppers.dev/posts/reverse-engineering-skylanders-toys-to-life-mechanics/)**.
- Everyone who documented the Portal of Power and its figures, and made the dumping tools that let people keep their own figures.

## License and legal

The code and documentation in this repository are released under the [MIT license](LICENSE). That doesn't cover the game or its data, which you need to own and supply yourself, or the third-party components in `thirdparty/`, which keep their own licenses.

This is an unofficial fan project. It isn't affiliated with, endorsed by, or connected to Activision, Toys for Bob, or the owners of Skylanders. Skylanders and related names are trademarks of their owners. No game data is distributed here, and release binaries contain recompiled code only.