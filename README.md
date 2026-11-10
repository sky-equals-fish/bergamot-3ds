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
on iPhone it doesn't need a debugger attached or any special entitlements.

It's an early preview. Expect rough edges, and please open an issue if a game
doesn't work.

*Bergamot 3DS is not affiliated with or endorsed by Nintendo.*

## Features

- Upscaling up to 4×, with an Auto setting that picks a good level for your device
- Stacked or side-by-side screens
- Encrypted games, using your own `keys.txt`
- Shaders are prepared when a game starts, so games you've played before run smoothly
- Shader caches can be moved between devices, so your iPhone can start with what your Mac has already built
- Game controllers, keyboard, and mouse or touch for the bottom screen
- Opens `.3ds`, `.cci`, `.cxi`, `.3dsx` and `.cia`

## Installation

Download the latest build from [Releases](https://github.com/sky-equals-fish/bergamot-3ds/releases/latest).

### Mac

Requires an Apple Silicon Mac with macOS 13 or later.

1. Download the Mac `.zip` and unzip it.
2. Open the app. Builds aren't notarized yet, so the first time you'll need to
   allow it in **System Settings › Privacy & Security › Open Anyway**.
3. Open a game with **File › Open Game**, or drag it onto the window.

### iPhone and iPad

Requires iOS 18 or later.

1. Download the Xcode project `.zip` and open `Bergamot3DS.xcodeproj`.
2. Under **Signing & Capabilities**, pick your team. A free Apple ID works, but
   the app expires after 7 days.
3. Connect your device and press **Run**.

You can also sideload the unsigned `.ipa` with AltStore or SideStore.

Add games from the app's library, or put them in
**Files › On My iPhone › Bergamot 3DS › Games**.

See the [app guide](docs/PLAYER.md) for controls, settings and keys.

## Building

You'll need an Apple Silicon Mac with Xcode, Git and Python 3.

```sh
git clone https://github.com/sky-equals-fish/bergamot-3ds.git
cd bergamot-3ds
python3 tools/bootstrap.py
export PATH="$PWD/.venv/bin:$PATH"
python tools/build_core.py macos
cmake -S player -B build/player-macos -G Ninja -DCMAKE_BUILD_TYPE=Release -DCTR_PLATFORM=macos
cmake --build build/player-macos
```

`bootstrap.py` downloads Azahar and applies our patches. iPhone builds are covered
in the [app guide](docs/PLAYER.md#building-it-yourself).

The emulator core is also a standalone CMake library, if you want 3DS emulation in
your own Mac or iOS app. See [docs/THREEDS_SOURCE_PACKAGE.md](docs/THREEDS_SOURCE_PACKAGE.md).

## Credits

- [Azahar](https://github.com/azahar-emu/azahar) and the Citra team, who wrote the
  emulator this is built on. Our changes are listed in
  [docs/AZAHAR_PATCHES.md](docs/AZAHAR_PATCHES.md).
- [MoltenVK](https://github.com/KhronosGroup/MoltenVK), for running Azahar's Vulkan
  renderer on Metal.

Bergamot 3DS is part of Project Bergamot by Sky=Fish, which also includes an
upcoming multi-system front end called Bergamot. Bergie, up top, is the mascot.

## License

Bergamot 3DS is licensed under GPL-3.0-or-later, the same as Azahar. See
[LICENSE](LICENSE), [NOTICE.txt](NOTICE.txt) and
[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt). No games, keys or firmware are
included.
