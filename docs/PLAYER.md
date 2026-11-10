# Bergamot 3DS app

Bergamot 3DS is a small Mac and iPhone/iPad app for playing 3DS games. It uses
the same screen layout and touch controls as the full Bergamot app. Open a game
and play; saves are kept automatically.

## Playing

**Mac:** open a game with **File › Open Game** (⌘O), drag it onto the window, or
pick it from **Open Recent**. Esc pauses, ⌘. stops, and the mouse acts as the
touch screen.

**iPhone and iPad:** tap **Import** or copy games into **Files › On My iPhone ›
Bergamot 3DS › Games**. The library shows each game's own icon and title, with
search. Tap a game to play; HOME opens the pause menu.

Supported files are `.3ds`, `.cci`, `.cxi`, `.3dsx` and `.cia`. Encrypted games need your
console's `keys.txt`.
Updates and DLC go through **File › Install Update or DLC** on Mac, or
**Update** on iPhone. **File › Install Keys…** on Mac, or **Keys** on iPhone, installs your
`keys.txt` or `aes_keys.txt` (and optionally `boot9.bin` and `otp.bin`) into Azahar's `sysdata`
folder after checking them. Install keys before the first game of a launch; the core reads them once. Save in the game before you stop it.

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
| Upscaling | Auto, 1× native (400×240), 2×, 3×, 4× (1600×960) | Auto on iPhone, 1× on Mac |
| Screen layout | Stacked, side by side | Stacked |
| Shaders | Prepare cached shaders, or also compile new ones in the background | Prepare only |
| GPU handoff | Standard, queue-ordered (experimental) | Standard |

On Mac, everything is in **Settings** (⌘,), and upscaling and layout are also
in the **Emulation** menu. On iPhone, upscaling is on the library screen. Upscaling,
shaders and GPU handoff take effect the next time a game starts.

**About Auto:** the game renders at the whole-number scale closest to the top
screen's physical pixel width (400 px per 1×), then is filtered to fit. It is capped
by chip: 3× on A17 Pro and newer, 2× on A15/A16, 1× on older devices. Only the
iPhone 15 Pro has been measured; the other caps are estimates. In portrait the top
screen spans the full width of the phone at its native 5:3 shape.

**About shaders:** when a game starts, the app builds every shader that game has
used before ("Preparing shaders"), so effects you've already seen never pause the
game. The first time a brand-new effect appears, it compiles once and is cached
from then on. Background compiling skips that one pause, but can draw a scene
with pieces missing for a frame or two, which is why it's off by default.

**Bringing shaders to another device:** on Mac, **File › Export Shaders…** saves
every game's shader list to one `.bgshaders` file. Open it on your iPhone (AirDrop,
Files, or the library's **Import** button) or with **File › Import Shaders…** on
another Mac. The next time each game starts, those shaders are built during
"Preparing shaders" instead of pausing the game. A game's list is only replaced
by a larger one. The file holds shader setups taken from your games, so keep it to
your own devices.

Import also takes a game's four raw cache files (`<title ID>_vs.vkch`, `_fs`, `_gs`,
`_pl`) picked together, for example from another app's
`saves/Azahar/shaders/vulkan/transferable` folder. Close that app first. A cache
from a different core build is accepted; its shaders are rebuilt once while the
game loads.

## Good to know

- Your data lives in `~/Library/Application Support/Bergamot 3DS` on Mac, or the
  app's Documents folder on iPhone. Saves, system files and shader caches are in
  its `Data` folder.
- A `.cia` is installed the first time you open it. After that the app opens the
  installed copy directly.
- On Mac, the display stays awake while a game runs. If the window is fully
  covered, the game keeps running and just isn't drawn.

## Building it yourself

The Mac build is in the [README](../README.md#building). It produces
`build/player-macos/Bergamot 3DS.app`.

```sh
# iPhone / iPad: needs Xcode's iOS SDK; sign the result with your own identity
python tools/build_core.py ios
cmake -S player -B build/player-ios -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCTR_PLATFORM=ios -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0
cmake --build build/player-ios
```

To make release files, run `python tools/package_player.py --mac --ios --xcode --source`.
It writes a Mac zip, an unsigned `.ipa`, the iPhone Xcode project (prebuilt core
and libraries, built by `tools/make_xcode_kit.py`), the matching source archive
and `SHA256SUMS` to `artifacts/player-release/`. The Mac app is ad-hoc signed unless
you pass `--identity "Developer ID Application: …"`, and it should be notarized
before you share it.
