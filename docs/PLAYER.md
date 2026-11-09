# Bergamot 3DS app

Bergamot 3DS is a small Mac and iPhone/iPad app for playing 3DS games. It uses
the same screen layout and touch controls as the full Bergamot app. Open a game
and play; saves are kept automatically.

## Playing

**Mac:** open a game with **File › Open Game** (⌘O), drag it onto the window, or
pick it from **Open Recent**. Esc pauses, ⌘. stops, and the mouse acts as the
touch screen.

**iPhone and iPad:** tap **Import Game**, or copy games into **Files › On My
iPhone › Bergamot 3DS › Games**. Tap a game to play. HOME opens the pause menu.

Supported files are `.3ds`, `.cci`, `.cxi`, `.3dsx` and decrypted `.cia`.
Updates and DLC go through **File › Install Update or DLC** on Mac, or
**Install Update** on iPhone. Save in the game before you stop it.

| Mac keys | 3DS |
| --- | --- |
| Z / X / A / S | A / B / Y / X |
| Arrow keys | Circle Pad |
| I J K L | D-pad |
| Q / W | L / R |
| 1 / 2 | ZL / ZR |
| Return / Tab | Start / Select |

Xbox, PlayStation and MFi controllers work on both platforms. Buttons map by
position, so the bottom face button is the 3DS B button. The controller's home
button pauses.

## Settings

| Setting | Options | Default |
| --- | --- | --- |
| Resolution | 1× native (400×240), 2×, 3×, 4× (1600×960) | 1× |
| Screen layout | Stacked, side by side | Stacked |
| Shaders | Prepare cached shaders, or also compile new ones in the background | Prepare only |
| GPU handoff | Standard, queue-ordered (experimental) | Standard |

On Mac, everything is in **Settings** (⌘,), and resolution and layout are also
in the **Emulation** menu. On iPhone, resolution is on the game list. Resolution,
shaders and GPU handoff take effect the next time a game starts.

Native resolution runs best. Higher settings look sharper but cost GPU time.

**About shaders:** when a game starts, the app builds every shader that game has
used before ("Preparing shaders"), so effects you've already seen never pause the
game. The first time a brand-new effect appears, it compiles once and is cached
from then on. Background compiling skips that one pause, but can draw a scene
with pieces missing for a frame or two, which is why it's off by default.

## Good to know

- Your data lives in `~/Library/Application Support/Bergamot 3DS` on Mac, or the
  app's Documents folder on iPhone. Saves, system files and shader caches are in
  its `Data` folder.
- A `.cia` is installed the first time you open it. After that the app opens the
  installed copy directly.
- On Mac, the display stays awake while a game runs. If the window is fully
  covered, the game keeps running and just isn't drawn.

## Building it yourself

Build the core first (see the [README](../README.md#build-from-source)), then
build the app with the same platform settings.

```sh
# Mac
cmake -S player -B build/player-macos -G Ninja -DCMAKE_BUILD_TYPE=Release -DCTR_PLATFORM=macos
cmake --build build/player-macos
open "build/player-macos/Bergamot 3DS.app"
```

```sh
# iPhone / iPad: needs Xcode's iOS SDK; sign the result with your own identity
python tools/build_core.py ios
cmake -S player -B build/player-ios -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCTR_PLATFORM=ios -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0
cmake --build build/player-ios
```

To make release files, run `python tools/package_player.py --mac --ios --source`.
It writes a Mac zip, an unsigned `.ipa`, the matching source archive and
`SHA256SUMS` to `artifacts/player-release/`. The Mac app is ad-hoc signed unless
you pass `--identity "Developer ID Application: …"`, and it should be notarized
before you share it.
