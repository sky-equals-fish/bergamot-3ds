# Bergamot 3DS status

Last updated 2026-11-09 (iPhone run added). Covers the 3DS core and the Bergamot 3DS app only.

## Tested

- **Mac (M3 Pro), Pokémon Alpha Sapphire with its update.** Full speed. Three 3-minute
  intro runs at 4× with no frame over 100 ms after startup (worst 81 ms). Before
  patch 0010, 3 of 5 runs had a 0.7–0.9 s freeze when the game loaded a code module.
- **Mac, homebrew probe.** The standalone example runs an original ARM11 test program
  and passes with a test hook that fails on any executable-memory request.
- **Builds.** The no-JIT core builds for macOS, iPhoneOS and iOS Simulator. The seven
  patches apply in order to a clean Azahar checkout at the pinned commit.
- **iPhone 15 Pro, earlier core build.** Alpha Sapphire booted at 4× near 100% guest
  speed; the queue-ordered handoff roughly halved per-frame work on the emulation
  thread. Details in [PRESENTATION_SYNC_EXPERIMENT.md](PRESENTATION_SYNC_EXPERIMENT.md).
- **Mac, shader stutter (patch 0012 and shader import).** Alpha Sapphire intro, 3 minutes, every
  cache wiped: 23 pauses over 100 ms (about 15 s in all, worst 2.3 s). Same route after
  importing a shader list made on the Mac, with the compiler caches still wiped: about 12 s of
  "Preparing shaders", then 4 pauses over 100 ms (new effects the earlier run hadn't seen).
  Fully warm: none. Dynamic state renders the same 15 sampled frames identically.
- **iPhone 15 Pro, v0.1.0 release build.** Built from the published Xcode project and
  installed with a development signature. Encrypted Alpha Sapphire `.cia`, warm shader and
  driver caches, the same 3-minute auto-advance intro route as on the Mac, run twice back to
  back: at 1×, median 100% speed and 59.8 fps, no frame over 100 ms (worst 79 ms), 0.3 s of
  "Preparing shaders"; at Auto (3× on this model), median 100% / 59.8 fps, no frame over
  100 ms (worst 92 ms), 0.3 s preparing. One of about 190 one-second samples per run fell
  below 95% speed. Cold caches, longer sessions and thermals on the phone are still untested.
- **iPhone 15 Pro, app updates (0.1.1).** 0.1.0 reinstalled each game once after every app
  update (about 10 s for Alpha Sapphire), because iOS moves the app's data folder and the
  installed-games list stored full paths. 0.1.1 stores paths relative to the app's folder and
  converts 0.1.0 entries. Three installs in a row, each into a new data folder, started the game
  in 7–16 ms with no reinstall. On the Mac the list converts the same way and launches still skip
  the install.
- **Mac, encrypted CIA (patch 0011).** An encrypted Alpha Sapphire `.cia` installs, decrypts
  with the user's `keys.txt`, and runs at 59.9 fps / 100% speed.
- **Mac, missing keys (0.1.2).** With an empty data folder and no `keys.txt`, an encrypted
  `.cia` is refused with "This game is encrypted. Install your keys.txt, then open it again"
  and nothing is installed. A `keys.txt` with no usable keys gets a "couldn't decrypt" message
  instead. Before 0.1.2 both cases showed "Package installation failed (3)". Not checked on an
  iPhone.

## Not tested yet

- Cold-cache shader stutter on a physical iPhone, and how it compares with the Mac.
- 3DS rendering in iOS Simulator, which hits a MoltenVK assertion at startup.
- Long sessions, thermals, and games beyond the ones above.
- A notarized Mac build.

## Known limits

- An effect seen for the very first time can still pause the game once while its
  shader compiles. It is cached after that. Importing a shader list from another
  device (`.bgshaders`) covers whatever that device has already seen.
- Encrypted games need the user's `keys.txt`. A `keys.txt` built from `boot9.bin` alone has no
  ECC root key, so `otp.bin` is ignored; games don't need it.
- The C++ API is tied to the toolchain. There is no stable C ABI or XCFramework yet.
