# Bergamot 3DS app

A small Mac and iPhone/iPad app around the Bergamot 3DS core. It reuses the
Bergamot app's 3DS screen layout and touch controls, without its library,
companion or DS/GBA cores. Games are data: open a file and play. Saves are kept
automatically.

## Build

Build the core first (see the [repository README](../README.md#quick-start-on-a-mac)),
then configure `player/` with the same platform settings as the core.

```sh
# Mac
python tools/build_core.py macos
cmake -S player -B build/player-macos -G Ninja -DCMAKE_BUILD_TYPE=Release -DCTR_PLATFORM=macos
cmake --build build/player-macos
open "build/player-macos/Bergamot 3DS.app"
```

```sh
# iPhone / iPad (sign the result with your own identity before installing)
python tools/build_core.py ios
cmake -S player -B build/player-ios -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCTR_PLATFORM=ios -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0
cmake --build build/player-ios
```

`python tools/package_player.py --mac --ios --source` then writes a Mac zip, an
unsigned `.ipa` for sideloading and the matching source archive to
`artifacts/player-release/`, with `SHA256SUMS`. The Mac app is ad-hoc signed
unless you pass `--identity "Developer ID Application: …"`; notarize before
sharing a download.

## Use

- **Mac:** File › Open Game (⌘O), drag a game onto the window, or Open Recent.
  Esc pauses, ⌘. stops. The mouse is the touch screen.
- **iPhone/iPad:** Import Game, or copy games into Files › On My iPhone ›
  Bergamot 3DS › Games. Tap a game to play; HOME opens the pause menu.
- Supported files: `.3ds`, `.cci`, `.cxi`, `.cia` (decrypted), `.3dsx`.
  Updates and DLC: File › Install Update or DLC (Mac), Install Update (iOS).
- Save in the game before stopping. The app keeps its data in
  `~/Library/Application Support/Bergamot 3DS` (Mac) or the app's Documents (iOS).

| Mac keys | 3DS |
| --- | --- |
| Z / X / A / S | A / B / Y / X |
| Arrow keys | Circle Pad |
| I J K L | D-pad |
| Q / W, 1 / 2 | L / R, ZL / ZR |
| Return / Tab | Start / Select |

Game controllers (Xbox, PlayStation, MFi) work on both platforms; buttons map by
position, so the bottom face button is 3DS B. The controller's home button pauses.

## Settings

| Setting | Where | Choices | Default |
| --- | --- | --- | --- |
| Internal resolution | Mac Settings (⌘,) or Emulation menu; iOS game list | 1× native (400×240), 2×, 3×, 4× (1600×960) | 1× |
| Screen layout | Mac Settings or Emulation menu | Stacked, Side by side | Stacked |
| GPU handoff | Mac Settings | Standard, Queue-ordered (experimental) | Standard |
| Shaders | Mac Settings | Prepare cached shaders; also compile new ones in background | Prepare only |

Resolution, GPU handoff and shaders apply when a game starts. Native resolution
runs best; higher settings look sharper and cost GPU time.

On launch the app prepares every shader this game has used before (shown as
"Preparing shaders"), so effects you have already seen never pause the game. An
effect seen for the first time compiles once and is then cached. Background
compiling avoids that one pause but can draw a scene with pieces missing for a
frame or two, so it is off by default.

## Behaviour notes

- The game keeps its own 59.83 Hz clock. On Mac, frames are presented without
  waiting for the display's refresh, so a ProMotion screen idling at 48 Hz does
  not slow the game.
- While a game runs, the Mac display is kept from dimming or sleeping. While the
  window is fully covered, the game keeps running and simply is not drawn.
- An installed `.cia` is remembered, so later launches open it directly instead
  of reinstalling.
- Saves, system data and shader caches live under the data folder's `Data/`
  directory in Azahar's usual layout.
