<!-- Root README of the standalone bergamot-3ds source repository. Links are
     relative to that repository's root; tools/prepare_source_release.py copies
     this file to README.md when exporting the 3ds component. -->
<p align="center">
  <img src="docs/art/fuitlet.png" width="160" alt="Fuitlet, the Bergamot mascot: a round green leaf-sprout character with a white headband">
</p>

<h1 align="center">Bergamot 3DS core</h1>

<p align="center">
  <strong>A no-JIT Nintendo 3DS runtime for Apple Silicon Mac, iPhone and iOS Simulator, built on Azahar, usable from any app.</strong>
</p>

<p align="center">
  <a href="docs/THREEDS_SOURCE_PACKAGE.md">Integration guide</a> ·
  <a href="#built-on-azahar">Built on Azahar</a> ·
  <a href="#how-it-works">How it works</a> ·
  <a href="docs/RELEASE_READINESS.md">What is verified</a> ·
  <a href="LICENSE">License</a>
</p>

---

This repository is the 3DS core from [Bergamot](#about-bergamot), split out so
other projects can use it. It contains **source**, not a prebuilt library: a
C++20 / Objective-C++ runtime, an optional Metal presenter, a UI-free CMake
entry point, pinned dependency manifests, the two Bergamot patches to Azahar,
build tools, an original homebrew smoke test and the notices that go with it.

What makes it different from running Azahar on a Mac:

- **No JIT, on every platform.** Guest ARM11 code runs through Azahar's
  FastInterp, which dispatches precompiled native handlers from decoded blocks
  held in ordinary data memory. Dynarmic, the PICA shader JIT and their code
  generators are excluded from the build. There is no JIT entitlement, no
  debugger attachment, no AltServer step and no executable-memory workaround,
  and the same binary policy applies to the Mac build.
- **Zero-copy frames into Metal.** Azahar renders PICA through Vulkan on
  MoltenVK. The core exports its output `VkImage` as an `MTLTexture` through
  `VK_EXT_metal_objects`, and the host presents that texture directly. No
  per-frame readback to CPU memory.
- **A host contract you can build a product on.** One worker thread owns the
  core; input setters are thread-safe; callback exceptions are contained at the
  C boundary and rethrown to the host; saves and system data live under a root
  you choose; GPU presentation has explicit ownership and completion rules.
- **It is checked outside its own UI.** An independent example host builds
  against `Bergamot::ThreeDS` and runs an original ARM11 probe, and a Mac test
  interposer fails the run if anything requests executable memory.

## Built on Azahar

This core **is Azahar**. [Azahar](https://github.com/azahar-emu/azahar) and its
contributors wrote the ARM11 CPU emulation, the kernel and HLE services, the
loaders, the audio, and the PICA graphics pipeline. Every game that runs here
runs because of their work.

Is it a fork? Not in the GitHub sense: this repository does not contain Azahar's
source tree. [`dependencies.json`](dependencies.json) pins one upstream commit,
`tools/bootstrap.py` fetches it and applies the two patches in
[`patches/`](patches). In the copyright sense, yes: the built core is a modified
Azahar, a derivative work licensed like upstream under **GPL-3.0-or-later**, and
everything Bergamot adds is GPL-3.0-or-later as well.

| Patch | What it does |
| --- | --- |
| `0001-libretro-frame-synchronization.patch` | Flushes the Vulkan worker before handing a frame to the host, preserves the scheduler's wait and signal semaphores on the libretro submission path, declares Metal object export for the output image and the command queue, and adds a small C bridge for title metadata, CIA installation and real performance counters |
| `0007-azahar-no-jit-build.patch` | Adds `BERGAMOT_NO_JIT`: removes Dynarmic, Oaknut, Xbyak and the PICA shader JIT sources and link dependencies, forces CPU and shader JIT settings off on every platform, and disables the built-in key blob |

The patches are Bergamot's responsibility and have not been submitted upstream.
Graphics translation uses [MoltenVK](https://github.com/KhronosGroup/MoltenVK)
(Apache-2.0). See [NOTICE.txt](NOTICE.txt) and
[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).

## Quick start on a Mac

Requirements: an Apple Silicon Mac, full Xcode (opened once), Git, Python 3,
network access for the first bootstrap.

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
python3 tools/bootstrap.py --simulator       # pin Azahar + submodules, apply patches, fetch MoltenVK, build the probe
export PATH="$PWD/.venv/bin:$PATH"
python tools/build_core.py macos             # build/macos/bin/Release/azahar_libretro.dylib
cmake -S systems/examples/three-ds -B build/standalone-3ds -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/standalone-3ds -j 4
build/standalone-3ds/independent-three-ds artifacts/arm11-probe.3dsx "$(mktemp -d)"
```

The last command runs 180 frames of an original ARM11 homebrew program through
the core with no UI and ends with `Standalone 3DS consumer passed`. Omit
`--simulator` if you only need Mac and physical iOS. Never point a probe at a
real save folder.

For iPhoneOS and Simulator libraries:

```sh
python tools/build_core.py ios
python tools/build_core.py ios-simulator
```

then configure `systems/3ds` with the matching `CTR_PLATFORM`, SDK and
toolchain as shown in the [integration guide](docs/THREEDS_SOURCE_PACKAGE.md).
Device and Simulator dylibs are both arm64 and are **not** interchangeable.

## Link it into your app

```cmake
cmake_minimum_required(VERSION 3.25)
project(My3DSHost LANGUAGES CXX OBJCXX)
add_subdirectory("/absolute/path/to/bergamot-3ds/systems/3ds" bergamot)
target_link_libraries(YourHost PRIVATE Bergamot::ThreeDS)        # runtime + Vulkan/MoltenVK handoff
target_link_libraries(YourHost PRIVATE Bergamot::ThreeDSMetal)   # optional: Metal presenter helpers
```

```cpp
#include <runtime.hpp>
#include <metal_presenter.h>

// On your emulation worker thread, with app-owned persistent storage:
ctr::Runtime runtime(storage_root, /*vulkan=*/true, /*scale=*/2);   // scale 1..4
runtime.set_gpu_presenter([&](VkDevice device, VkImage image, unsigned w, unsigned h) {
    id<MTLTexture> texture = CTRExportTexture(device, image);     // zero-copy
    [presenter present:texture on:metalLayer];                    // your CAMetalLayer
});
runtime.set_audio_sink([&](const int16_t* stereo, size_t frames) { /* copy into your audio queue */ });
runtime.load(game_path);
const auto av = runtime.av_info();          // fps and sample rate for pacing
while (!runtime.stopped()) {
    runtime.step();                         // one guest frame
    // sleep until the next frame deadline derived from av.timing.fps
}
runtime.unload();
```

Embed the matching `azahar_libretro.dylib` in your bundle's Frameworks directory
and sign it with your own identity. Buttons use libretro joypad IDs through
`set_button`; Circle Pad, C-Stick and touch use signed normalized coordinates.
The [integration guide](docs/THREEDS_SOURCE_PACKAGE.md) spells out the full
contract and the opt-in queue-ordered presentation mode.

## How it works

```text
Guest ARM11 / Thumb / VFP code  ──►  Azahar FastInterp: decoded basic blocks (data) ──► precompiled handlers
Guest PICA command lists        ──►  Azahar renderer ──► Vulkan ──► MoltenVK ──► MTLTexture
MTLTexture                      ──►  Bergamot Metal presenter ──► CAMetalLayer drawable
```

- **CPU.** FastInterp caches decoded basic blocks and dispatches existing native
  handlers with computed goto. Nothing is written to executable pages. Static
  instruction timing from upstream's tick tables is kept so guest time is unchanged.
- **Graphics.** The host negotiates a Vulkan device through the libretro
  hardware-render interface, requests `VK_EXT_metal_objects`, and receives the
  core's render-ready semaphore each frame. Three presentation modes exist:
  **serialized** (default: CPU waits for the render fence, presents, waits for
  Metal before the core reuses its image), **deferred** (Metal completion wait
  moves to the image-reuse boundary) and **queue-ordered** (the presenter commits
  on MoltenVK's own `MTLCommandQueue`, so Metal's in-order queue orders render,
  present and reuse on the GPU and the CPU no longer waits inside the frame).
  Queue-ordered is opt-in until matched gameplay scenes are measured; it halved
  per-frame emulation-thread work in iPhone 15 Pro boot comparisons.
- **Lifecycle.** One core instance per process (upstream state is global), one
  title per `Runtime`. Construct, load, step, unload and destroy on the same
  thread. Errors raised inside libretro callbacks are stored and rethrown from
  `step()` so the core never unwinds through C frames.
- **Storage.** The data root you pass gets `system/` and `saves/`; Azahar's
  usual layout lives below them. Installed CIAs and updates use the core's own
  title-management path through the C bridge.

The full technical write-up, including the measured numbers and their
limitations, is in [`docs/`](docs). Start with
[THREEDS_SOURCE_PACKAGE.md](docs/THREEDS_SOURCE_PACKAGE.md),
[PRESENTATION_SYNC_EXPERIMENT.md](docs/PRESENTATION_SYNC_EXPERIMENT.md) and
[RUNTIME_AUDIT.md](docs/RUNTIME_AUDIT.md).

## What is verified, and what is not

| Verified | Not yet |
| --- | --- |
| Compile-time no-JIT core builds for macOS, iPhoneOS and Simulator | Complete binary audit of every linked component for executable-memory use |
| Independent Mac host runs the original ARM11 probe with the executable-memory denial interposer active | Renewed physical-device gameplay validation after the compile-time JIT exclusion |
| Serialized, deferred and queue-ordered presentation produce byte-identical 4× captures under Metal API validation | Matched warm-cache gameplay benchmarks at 3× and 4×; sustained thermal and audio behaviour |
| Lifecycle tests: ownership, malformed titles, callback fault injection, sequential sessions, guest clock | Universal title compatibility; the probe is a smoke test, not an ARM conformance suite |
| Alpha Sapphire plus update boots on iPhone 15 Pro at 4× near 100% guest speed in all modes | Smooth sustained 4× gameplay; stalls remain |

[RELEASE_READINESS.md](docs/RELEASE_READINESS.md) keeps the evidence boundaries
current. Mac and Simulator results do not establish iPhone performance.

## Bring your own content

No games, console keys, firmware, saves or Apple signing material are in this
repository, and the tools do not download them. Supply titles you have the right
to use. Encrypted content needs your own `keys.txt`; encrypted CIAs are rejected,
use a decrypted export from your console. See [USER_FILES.md](docs/USER_FILES.md).

## About Bergamot

Bergamot is the iPhone and Mac app this core was built for: a pixel-styled
front end for 3DS, DS and GBA with a game library, cover art, touch controls and
Fuitlet, the companion pictured above. The app, its DS and GBA adapters and the
broader no-JIT systems research live in the main Bergamot repository, which is
being prepared for publication separately.

## License

Bergamot's code is **GPL-3.0-or-later**, see [LICENSE](LICENSE). Azahar is
GPL-3.0-or-later; MoltenVK is Apache-2.0; libretro headers are MIT. Distributing
a binary built from this source requires shipping the exact corresponding
sources, including the pinned dependencies and these patches. Nintendo 3DS and
game names are used to identify compatibility; Bergamot is independent and not
affiliated with Nintendo or the upstream projects.
