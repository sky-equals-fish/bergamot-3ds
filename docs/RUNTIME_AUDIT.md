# Bergamont runtime audit — November 7, 2026

The original pass below inspected the owned 3DS and handheld adapters, Vulkan/Metal handoff,
frame pacing, save handling, build scripts and frontend lifecycle integration.
It is a focused audit, not a complete audit of upstream emulator source. UI
layout/assets and vendor cores were not edited. No app was installed or restarted during that original pass. See the follow-up below for later installation and device evidence.

## Fixed

### 3DS callback exceptions could escape into the core

`src/runtime.cpp` previously invoked the audio sink directly from the libretro
callback. A native audio integration exception could unwind through core code
instead of returning to the host's error handling. Environment callbacks also
performed allocation/string operations without an exception boundary.

Audio/environment callbacks now contain exceptions. The first exception is
retained behind a mutex and rethrown by initialization/load/step on the host
side. `step()` checks for a pending error before running the next frame as well
as after returning from the core. Video uses the same first-error mechanism.
The regression test injects a failure through the registered audio callback,
checks that the callback returns, and checks that subsequent stepping fails
without advancing the frame count. The homebrew itself generates no DSP audio,
so direct callback fault injection is necessary to exercise this error path.

### A failed handheld save restoration left the title runnable

`src/handheld/session.cpp` marked the core loaded before restoring battery/RTC
memory. If restoration rejected a malformed save, `step()` still ran the title.
A new regression check reproduced this in `artifacts/audit-failed-load-before.log`.

Load failure now unloads the title, releases input and prevents further stepping,
without flushing partially restored memory. The test checks both that stepping
is rejected and that malformed save bytes survive cleanup unchanged. Successful
GBA save persistence and GBA/DS sequential-session probes still pass.

### 3DS partial initialization and ownership

The single-owner pointer used an unsynchronized check followed by assignment.
It now claims ownership with atomic compare/exchange. Successful initialization
followed by a callback error releases the initialized core and ownership.
A Vulkan negotiation/reset failure after loading now unloads the title before
propagating the error. Existing single-owner and sequential-session tests pass;
this does not make lifecycle methods generally safe to call from arbitrary threads.

### Per-message logging allocations

The 3DS log callback allocated a 16 KB vector and a string for every message.
It now uses a bounded stack buffer and searches it without heap allocation.
This is a small hot-path cleanup, not a measured gameplay FPS improvement.

## Validation

- `artifacts/audit-3ds-lifecycle.log`: malformed-title rejection, ownership,
  audio callback fault injection, no stepping after error, three successful
  sequential sessions, software/Vulkan execution and exact guest clock assertion.
- `artifacts/audit-handheld-tests.log`: GBA/DS homebrew boot/input/audio-buffer
  checks, guest GBA battery persistence, safety regression and Metal upload test.
- `artifacts/audit-pacer.log` and `artifacts/audit-layout.log`: jitter/hitch/resume
  pacing and screen/touch geometry checks.
- Host adapter tests used ASan/UBSan; upstream optimized cores are not sanitized.
  Leak detection is disabled, so no whole-process leak-free claim is made.
- Updated 3DS and handheld runtime libraries compiled for iPhoneOS and iOS
  Simulator. These are library builds, not installation or device-play evidence.

## Remaining findings

- The default Vulkan-to-Metal handoff still serializes GPU completion with CPU
  waits. The November 8 opt-in queue-ordered mode removes the per-frame waits by
  presenting on MoltenVK's own Metal queue (see PRESENTATION_SYNC_EXPERIMENT.md);
  a four-run phone boot comparison halved per-frame step work at 4x; it stays
  opt-in until matched gameplay scenes are measured.
- The handheld Metal uploader deliberately allocates independent textures each
  frame. A completion-aware pool may reduce allocations once integrated with
  the UI's presentation queue; immediate reuse would break retained-frame safety.
- Handheld destructors can only perform best-effort saves. UI integration must
  call `flush_save()`/`close()` explicitly and surface failures before discarding
  the session. DS commercial in-game save round trips remain unverified.
- Build receipts identify core revisions/platforms but are not cryptographic
  attestations of every source modification. Future release packaging should
  record patch/source and binary digests, not treat a matching HEAD as proof
  of an unchanged working tree.
- The Duo setup status still records an incompatible iOS runtime. No successful
  Duo boot is asserted by this audit.
- MoltenVK lifecycle logs mention memory still allocated while destroying the
  device. Leak detection was not run; this message alone does not establish a
  leak, but deserves a repeated-session memory measurement.

No performance percentage or 4x stability improvement is claimed by these fixes.


## November 7 follow-up: focused UI and efficiency

The Apple front end now presents only 3DS, DS and GBA with the original six-tile
home, palettes, grid and controls. Extra-console work/art was backed up locally
under `artifacts/focus-ui-backup`; it was not deleted. Packaging omits retired
console art and sprite sheets (1,216 PNGs in the clean Simulator bundle, down
from 1,828). Product text says Bergamot; bundle IDs and save paths are unchanged.

DS/GBA are regular playable library routes. The obsolete development empty-state
message is removed, and the document importer now accepts `.nds` and `.gba`.
3DS upscaling settings are labeled explicitly; DS/GBA currently render native
software output and scale it for display. Original GB/GBC support is not implied.

The controller plate is transparent, with a matching nonopaque Metal layer so
the grid remains visible around screens/buttons. Shoulder targets expand by 12
points horizontally and 10 vertically on each side, clipped to the view. Visible
controls win first, the lower touchscreen wins over added margins, and overlapping
margins choose the nearest control. Held touches use the expanded bounds too.
Artwork size and layout stay unchanged. Physical finger usability still needs the
user's confirmation after trying the new target sizes.

Hidden/inactive native banners stop their timer and continuous SceneKit rendering.
Vulkan wait-stage storage retains capacity rather than allocating each handoff;
synchronization semantics are unchanged. Handheld color conversion specializes
once per frame instead of testing format and variable-copy size per pixel.

A Release Mac conversion-only benchmark used five alternating paired rounds,
1,000 256x384 frames per round. Median old/new times in microseconds were:
XRGB8888 318.415/10.829; RGB565 190.968/13.255; RGB1555 195.562/15.523.
These are conversion microbenchmarks, not game-FPS or iPhone speedup claims.
Correctness checks cover all 16-bit colors, randomized 32-bit colors, unaligned
buffers, padded rows, native widths and output guards.

Validation passed in the bergamot-systems worktree: ASan/UBSan pixel conversion,
handheld save/lifecycle safety, GBA/DS input/audio/video probes, retained Metal
upload ownership, independent headless 3DS/GBA/DS consumers, runtime lifecycle,
frame pacing, display layout and skin presenter tests. Logs use `artifacts/focus-*`.
A fresh 4x synthetic host sync/deferred capture pair matched byte for byte.
Release iPhoneOS and Simulator builds passed. iPhone and Duo Simulator screenshots
cover the restored home/library/settings and three control layouts across palettes.
These are visual checks, not Duo hardware or iPhone performance evidence.

The signed app was installed wirelessly on the unlocked iPhone 15 Pro. A physical
Alpha Sapphire screenshot verifies the grid through the transparent controller
surround. Short 4x boot timing is recorded in PRESENTATION_SYNC_EXPERIMENT.md:
near-100% median sampled guest speed still included substantial stalls, so smooth
4x is not achieved or claimed. Deferred presentation stays opt-in. No JIT enabled.

The earlier UI-save finding is now clarified: the current handheld runner explicitly
flushes on pause and closes on normal stop; malformed-save tests pass. Commercial
DS in-game save/reopen and extended play still need validation. An earlier Simulator
3DS startup produced a MoltenVK buffer-view assertion; physical boot succeeded,
but this does not establish that the Simulator issue is fixed. Mac frontend
handheld UI parity is not established by the independent host checks.

The existing Emerald/White game copies were hash-verified and added to the phone's
Documents/Games without copying or replacing saves. Emerald loaded on the physical
phone and closed normally after 328 frames when the user stopped it. That is a short
launch/lifecycle check, not extended gameplay. White loaded and completed 600 and 1,800 frames
in the updated Duo Simulator app; the later physical check below supersedes the initial DS device gap.
Logs: `artifacts/focus-review/phone-emerald-console.log` and
`simulator-white-console.log`.

### Later phone usability and artwork follow-up

The final tall iPhone DS layout follows the 3DS-style stacked arrangement: a
large 4:3 upper display, a centered 4:3 touch display, L/R beside the lower
display, and larger D-pad/face controls beneath it. This supersedes both the
24-point offset and the intervening small-screen-between-controls experiment.
Expanded shoulder hit areas remain. The final layout passed Release iPhoneOS
and Simulator builds, was visually inspected in Simulator, and was installed
over Wi-Fi on the iPhone 15 Pro. White was reopened for user testing; the user
accepted the result. Evidence: artifacts/focus-review/iphone-ds-balanced.png
and ds-balanced-device-install.json.
Search dismisses on outside taps (without cancelling button actions), Done,
scrolling, or hiding the menu. Native Simulator UI automation was unavailable
through the connected computer-use surface, so physical tap/keyboard usability
is not represented as an automated end-to-end pass.

`ios/library_art.h` extracts the DS static 32x32 banner icon and the first two
English title lines, including White's title. Animated DSi icon playback is not
implemented. Missing box art downloads automatically while browsing; Settings
also offers Find missing covers. Manual sidecars take precedence. Downloads and
provider/source receipts live in Library/Caches/BergamotArtwork; games/saves are
unchanged. Indexes cache for seven days; unmatched attempts retry after a day.
Work runs off the UI thread and stops starting new lookups during gameplay.

Libretro thumbnail repository names are matched locally. Ambiguous names are
left unmatched, and GBA cartridge serials resolve short filenames through the
Libretro no-intro catalog. No ROM bytes or directory listings are uploaded.
Sources: https://github.com/libretro-thumbnails/libretro-thumbnails and
https://github.com/libretro/libretro-database ; DS banner structure reference:
https://github.com/devkitPro/ndstool/blob/master/source/banner.h .

The standalone Simulator artwork harness passed malformed-file rejection,
exact-versus-ambiguous matching, White icon/title extraction, Emerald cartridge
identity and canonical cover matching, live image download, and cache reuse.
Source: tests/ios/library_art.mm. Local log: artifacts/focus-review/artwork-test.log.

The final artwork/keyboard/layout app passed Release builds for iPhoneOS and
Simulator and was installed over Wi-Fi after the user confirmed their game was
saved. The iPhone Simulator library screenshot verifies downloaded Emerald art.

The final physical iPhone White run loaded and closed normally after 1,800 frames
(`artifacts/focus-review/phone-white-final-console.log`). This is a launch/runtime
smoke test, not a commercial DS in-game save round trip or sustained performance
measurement. The app returned to its menu normally.
