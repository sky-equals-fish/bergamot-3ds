# Bergamot 3DS status

Last updated 2026-11-09. Covers the 3DS core and the Bergamot 3DS app only.

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
- **Mac, encrypted CIA (patch 0011).** An encrypted Alpha Sapphire `.cia` installs, decrypts
  with the user's `keys.txt`, and runs at 59.9 fps / 100% speed.

## Not tested yet

- The current core (patches 0008–0012) and the Bergamot 3DS app on a physical iPhone.
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
