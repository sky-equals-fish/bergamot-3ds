<!-- Root README of the standalone bergamot-3ds repository. Links are relative to that
     repository's root; tools/prepare_source_release.py copies this file to README.md
     when exporting the 3ds component. -->
<p align="center">
  <img src="docs/art/fuitlet.png" width="128" alt="Bergie, the Project Bergamot mascot">
</p>

<h1 align="center">Bergamot 3DS</h1>

<p align="center">
  <a href="https://github.com/sky-equals-fish/bergamot-3ds/releases/latest"><img src="https://img.shields.io/github/v/release/sky-equals-fish/bergamot-3ds?include_prereleases&label=release" alt="Latest release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue" alt="License: GPL-3.0"></a>
</p>

Bergamot 3DS is a Nintendo 3DS emulator for iPhone, iPad and Apple Silicon Macs,
built on [Azahar](https://github.com/azahar-emu/azahar). It runs without JIT, so
on iPhone it doesn't need a debugger attached or any special entitlements to run
smoothly.

It's an experimental preview. Please open an issue if a game doesn't work.

*Bergamot 3DS is not affiliated with or endorsed by Nintendo.*

## Features

- Upscaling up to 4×, with an Auto setting that picks a level for your device
- Shaders are prepared when a game starts, so effects you've already seen don't stutter
- Shader caches can be moved between devices, so an iPhone can start with what a Mac has already built

## Installation

Download the latest build from [Releases](https://github.com/sky-equals-fish/bergamot-3ds/releases/latest).

- **Mac:** Apple Silicon, macOS 13 or later. Builds aren't notarized yet, so allow
  the app in **System Settings › Privacy & Security** the first time.
- **iPhone and iPad:** iOS 18 or later. Build the Xcode project with your own team,
  or sideload the unsigned `.ipa` with AltStore or SideStore.

See the [app guide](docs/PLAYER.md) for controls, settings and keys.

## Building

Needs an Apple Silicon Mac with Xcode, Git and Python 3.

```sh
python3 tools/bootstrap.py
export PATH="$PWD/.venv/bin:$PATH"
python tools/build_core.py macos
cmake -S player -B build/player-macos -G Ninja -DCMAKE_BUILD_TYPE=Release -DCTR_PLATFORM=macos
cmake --build build/player-macos
```

`bootstrap.py` downloads Azahar and applies our patches. iPhone builds are covered
in the [app guide](docs/PLAYER.md#building-it-yourself).

The emulator core is also a standalone CMake library. See
[docs/THREEDS_SOURCE_PACKAGE.md](docs/THREEDS_SOURCE_PACKAGE.md).

## Credits

- [Azahar](https://github.com/azahar-emu/azahar) and the Citra team, who wrote the
  emulator this is built on. Our changes are listed in
  [docs/AZAHAR_PATCHES.md](docs/AZAHAR_PATCHES.md).
- [MoltenVK](https://github.com/KhronosGroup/MoltenVK), for running Azahar's Vulkan
  renderer on Metal.

## License

GPL-3.0-or-later, the same as Azahar. See
[LICENSE](LICENSE), [NOTICE.txt](NOTICE.txt) and
[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt). No games, keys or firmware are
included.
