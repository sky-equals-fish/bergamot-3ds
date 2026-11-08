# Bergamot app source preview

Bergamot is an independent Apple frontend for 3DS, DS and GBA. It uses pinned
Azahar, libretro melonDS and mGBA implementations with credited Bergamot host,
interpreter-integration, presentation and lifecycle changes. It is not affiliated
with Nintendo or endorsed by those emulator projects.

## Bring your own content

No commercial games, Nintendo AES keys, dumped firmware, updates, saves, imported
skins or downloaded box art are included. Import compatible game files that you
have the right to use. Owning a cartridge is not a blanket statement about the
legality of every copying or circumvention method; requirements depend on your
jurisdiction and circumstances.

- 3DS: supply a compatible title. Encrypted content can require your own `keys.txt`
  data. Some titles also require your own updates or system data. Use the Settings
  import controls; importing keys alone does not guarantee encrypted-game support; do not place keys in source, issues, screenshots or releases.
- DS: import your `.nds` file. The app extracts its static banner icon locally.
  The current DS configuration has booted the tested title without supplied BIOS
  files; this does not establish all-title compatibility or DSi-mode support.
- GBA: import your `.gba` file. The current configuration uses the core's supported
  replacement behavior; no dumped Nintendo BIOS is bundled.
- Cover artwork: optional missing-art downloads use Libretro Thumbnails. Downloads
  stay in the user's cache, are not shipped as project artwork, and do not replace
  manually selected covers. Copyright remains with the artwork's owners.

Use only system material you are entitled to use. The project does not provide
retail download links, shared keys or a method to bypass rights restrictions.
This documentation is release engineering guidance, not a guarantee against claims.

## First run

1. Open **Import** on Home and choose a compatible game file from Files.
2. Open its 3DS, DS or GBA library, then select the game to play. Use Search or
   Favorites to find it later; Continue returns to the last-played title.
3. For 3DS content that needs extra files, Settings provides **Import keys.txt**,
   **Install game update** and **Import 3DS system folder**. Stop the game before
   importing; restart the app after changing keys. Supply an extracted supported
   system folder, not an arbitrary raw NAND image.
4. Use the on-screen Home control to reach the session menu. Save inside the
   game, then use **Stop game** to end the session cleanly.

## Build your own signed app

This section requires the full app source archive. The standalone 3DS archive
has no app UI or app-build scripts; use its separate integration guide instead.

Install full Xcode and Python 3 on an Apple Silicon Mac. Run from `ctr-runtime`
in the repository, or from the root of the extracted app source archive:

```sh
python3 tools/bootstrap.py --simulator
.venv/bin/python tools/build_core.py ios
.venv/bin/python tools/build_handheld_cores.py ios
.venv/bin/python tools/build_device.py --team YOUR_TEAM_ID --device YOUR_DEVICE_UDID --install
```

You must use your own Apple development/signing identity. The historical target
`CTRRuntime` and bundle identifier are preserved for existing local saves; choose
an appropriate identity for your own distribution and review migration separately.
To list paired devices, run `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
xcrun devicectl list devices`. Device installation requires a paired, reachable
device with Developer Mode enabled; wireless installation also requires an
existing pairing and working network connection.

For Simulator:

```sh
.venv/bin/python tools/build_core.py ios-simulator
.venv/bin/python tools/build_handheld_cores.py ios-simulator
.venv/bin/python tools/build_simulator.py --device YOUR_SIMULATOR_UDID
```

Use `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcrun simctl list devices`
to find a booted Simulator. Omit `--device` to build without installing. The
app target is iOS 18.0 or later; that deployment target is not a compatibility
promise for every device. Physical testing has focused on iPhone 15 Pro.

This source preview requires pinned dependencies fetched during bootstrap. It is
not a signed IPA or a complete corresponding-source offer for already-built
binaries. See [release readiness](RELEASE_READINESS.md), [LICENSE](../LICENSE),
[NOTICE.txt](../NOTICE.txt), and [third-party notices](../THIRD_PARTY_NOTICES.txt)
before distributing a build.

3DS is exposed separately through `systems/3ds`; other applications do not need
Bergamot's UI. See [the standalone 3DS guide](THREEDS_SOURCE_PACKAGE.md). Smooth 4x gameplay and complete
compatibility are not claimed by this preview.
