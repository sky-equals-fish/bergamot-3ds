# Bergamot 3DS status

Last updated 2026-11-10 (GPU handoff default, 6× on Mac). Covers the 3DS core and the Bergamot 3DS app only.

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

- **GPU handoff default (0.1.3).** Queue-ordered handoff is now the
  app default: the emulation thread no longer waits for the GPU inside each frame. Same warm
  75-second intro route, alternating order, time inside the emulation step per 16.7 ms frame:
  - iPhone 15 Pro, Auto (3×): 14.6 / 14.8 ms syncing each frame, 6.4 / 6.9 ms queue-ordered.
  - iPhone 15 Pro, 4×: 16.4 / 16.4 ms syncing (speed dipped to 89%), 7.2 / 7.5 ms queue-ordered
    (100%, no one-second sample under 99.9%).
  - Mac (M3 Pro), 1×: 9.3 / 9.5 ms syncing, 4.6 / 4.5 ms queue-ordered. At 4×: 4.3 ms. At the
    new 6× (2400×1440): 4.6 ms queue-ordered, 11.7 ms syncing.
  The lifecycle test (queue-ordered run-ahead bound) and the headless no-JIT host pass. These
  are short runs of one game's intro; gameplay scenes, other games, long sessions and thermals
  are not covered.
- **iPhone 15 Pro, 8 minutes at 4× (queue-ordered, 0.1.3).** Auto-advanced intro route,
  warm caches: 6.9–7.8 ms per frame in minutes 2–8 with no upward drift, median speed 100%,
  60 fps scenes from minute 2. Two frames over 40 ms after startup (48 ms and 140 ms, both
  core-side). One audio interruption late in the run: the engine restarted by itself; about
  14 s of audio was discarded while it was interrupted. Thermal state was not recorded.
- **Link-time optimization.** Measured on Mac, alternating with the standard core: 4.2 / 4.1 ms
  against 4.0 / 4.2 ms. No gain, not adopted.
- **iPhone audio.** One test build was silent on the phone. The session is now configured
  before the audio engine is built, failures are logged, and audio restarts after route changes
  and interruptions. Sound was confirmed by ear on the next build; the cause of the silent run
  was not isolated, and route changes and interruptions have not been exercised.

## Not tested yet

- Cold-cache shader stutter on a physical iPhone, and how it compares with the Mac.
- 3DS rendering in iOS Simulator, which hits a MoltenVK assertion at startup.
- Long sessions, thermals, and games beyond the ones above.
- A notarized Mac build.

## Known limits

- An effect seen for the very first time can still pause the game once while its
  shader compiles. It is cached after that. Importing a shader list from another
  device (`.bgshaders`) covers whatever that device has already seen.
- Games that use seed encryption also need the user's `seeddb.bin`, which the key importer
  accepts from 0.1.3. No seed-encrypted game has been run yet: the one tried had no seed file.
- Encrypted games need the user's `keys.txt`. A `keys.txt` built from `boot9.bin` alone has no
  ECC root key, so `otp.bin` is ignored; games don't need it.
- The C++ API is tied to the toolchain. There is no stable C ABI or XCFramework yet.
