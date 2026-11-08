# Use the Bergamot 3DS core on Mac and iOS

The standalone package exposes Bergamot's Azahar-based 3DS integration without
its app UI, artwork, library database, DS or GBA cores. It is a **C++20 /
Objective-C++ source SDK**, currently compiler/toolchain-coupled rather than a
stable C ABI. Swift hosts need their own Objective-C++ bridge.

## Get the source

Clone the public repository:

```sh
git clone https://github.com/sky-equals-fish/bergamot-3ds.git
cd bergamot-3ds
```

It contains the runtime, optional Metal presenter, pinned dependency manifests,
local patches, build tools, notices and an independent original-homebrew example.
`SOURCE_MANIFEST.json` records a SHA-256 for every exported source file.

The same source builds for **Apple Silicon macOS**, **iPhoneOS arm64** and
**iOS Simulator arm64**. It contains no precompiled dylibs, XCFramework, signed
IPA, Apple signing material or Nintendo content. Dependencies are fetched by
bootstrap. Commands below run from the repository root. In the full Bergamot
repository, run them from `ctr-runtime` instead.

## Requirements and dependencies

Use an Apple Silicon Mac with full Xcode (including the SDK for your target),
Git, Python 3 and network access. Open Xcode once to complete its setup.
Bootstrap creates `.venv`, installs the pinned build tools, checks out the pinned
Azahar revision/submodules, applies the Bergamot patches, verifies MoltenVK
archive hashes and generates original ARM11 test homebrew.

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
python3 tools/bootstrap.py --simulator
export PATH="$PWD/.venv/bin:$PATH"
```

`--simulator` fetches the additional Simulator Metal libraries; omit it if you
only need Mac and physical iOS. Existing checkouts with a different source pin
are rejected rather than silently replaced. Never use a real save folder for
probe validation.

## Mac: build and run the independent example

```sh
python tools/build_core.py macos
cmake -S systems/examples/three-ds -B build/standalone-3ds -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/standalone-3ds -j 4
probe_storage=$(mktemp -d)
build/standalone-3ds/independent-three-ds \
  artifacts/arm11-probe.3dsx "$probe_storage"
```

Success ends with `Standalone 3DS consumer passed`. The example runs 180 steps
of original homebrew and checks its marker. It is a software-path smoke test,
not an interactive player, commercial-game benchmark or phone-performance test.
The example finds the core via a build-tree rpath.

## iPhoneOS and Simulator: build matching libraries

Build the core for each platform you need:

```sh
python tools/build_core.py ios
python tools/build_core.py ios-simulator
```

Then build the standalone runtime/presenter static libraries, with no app UI:

```sh
cmake -S systems/3ds -B build/sdk-ios -G Ninja \
  -DCTR_PLATFORM=ios -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 -DCMAKE_BUILD_TYPE=Release
cmake --build build/sdk-ios -j 4

cmake -S systems/3ds -B build/sdk-simulator -G Ninja \
  -DCTR_PLATFORM=ios-simulator -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 -DCMAKE_BUILD_TYPE=Release
cmake --build build/sdk-simulator -j 4
```

| Target | Dynamic core output | Standalone host library output |
| --- | --- | --- |
| Mac | `build/macos/bin/Release/azahar_libretro.dylib` | `build/standalone-3ds/bergamot/runtime/libctr-runtime.a` |
| iPhone / iPad | `build/ios/bin/Release/azahar_libretro.dylib` | `build/sdk-ios/runtime/libctr-runtime.a` |
| Simulator | `build/ios-simulator/bin/Release/azahar_libretro.dylib` | `build/sdk-simulator/runtime/libctr-runtime.a` |

`libctr-metal-presenter.a` sits alongside each static runtime library. These
static libraries are adapters, not self-contained cores: the matching Azahar
dylib, MoltenVK and Apple framework dependencies are still required. Device and
Simulator dylibs are **not interchangeable**, even though both use arm64.

## Link into your own application

Prefer the CMake entry point so include paths and transitive link dependencies
are carried together. Set the SDK/toolchain and `CTR_PLATFORM` when configuring
your parent project, as in the platform commands above.

```cmake
cmake_minimum_required(VERSION 3.25)
project(My3DSHost LANGUAGES CXX OBJCXX)
set(BERGAMOT_SOURCE "/absolute/path/to/bergamot-3ds")
add_subdirectory("${BERGAMOT_SOURCE}/systems/3ds" bergamot)
# Define YourHost as your executable or app bundle before linking it.
target_link_libraries(YourHost PRIVATE Bergamot::ThreeDS)
# For Metal output, also link:
target_link_libraries(YourHost PRIVATE Bergamot::ThreeDSMetal)
```

- Embed the matching `azahar_libretro.dylib` in your app's Frameworks directory.
  For iOS, use Xcode's **Embed & Sign** with your own identity and set the host
  runpath to `@executable_path/Frameworks`. On macOS app bundles the usual location
  is `Contents/Frameworks`, with `@executable_path/../Frameworks`.
- The modified core's Vulkan exports and Bergamot runtime are a matched pair.
  It is not advertised as a drop-in core for arbitrary libretro frontends.
- The source entry point links the platform's static MoltenVK slice and required
  Apple frameworks. If integrating manually in Xcode, reproduce the dependencies
  in [CMakeLists.txt](../CMakeLists.txt), including its public header search paths.
- Use your own app bundle identifier and provisioning. Do not copy development
  signing files from another installation. No JIT entitlement or enabling service
  is part of this design.

## Runtime contract

See [runtime.hpp](../src/runtime.hpp) and the
[complete smoke example](../systems/examples/three-ds/main.cpp).

```cpp
#include <runtime.hpp>

// On the emulation worker, with application-owned persistent storage:
ctr::Runtime runtime(storage_path); // software output; one instance per process
runtime.load(game_path);
for (unsigned i = 0; i < 180 && !runtime.stopped(); ++i) {
    runtime.step();
}
runtime.unload();
```

This bounded example omits audio, display, pacing and UI. A playable host must:

1. Construct, load, step, unload and destroy the runtime on the **same worker**.
   Keep one core instance per process. Catch errors on that worker and surface
   them on the UI thread. Keep the persistent data root across launches; the
   runtime creates `system` and `saves` beneath it.
2. Register frame/audio callbacks before stepping. Callback buffers are borrowed:
   consume or copy them before returning. Audio is interleaved signed 16-bit
   stereo; use `av_info()` for timing and sample rate. Provide bounded queues
   and pacing; `step()` is not a UI display timer.
3. Forward buttons using libretro button IDs and the thread-safe input setters.
   Touch/circle-pad/C-stick coordinates use signed normalized input values
   (`-32767` to `32767`); map touch from the actual fitted lower-screen bounds.
   Release held controls on interruption.
4. For GPU output, construct `Runtime(storage_path, true, scale)` with scale 1–4,
   and connect its GPU presenter to the Apple surface. The
   [Metal probe](../src/metal_probe.mm) demonstrates texture export, ownership,
   offscreen output and synchronous/deferred completion. Use the synchronous
   path first. Drain outstanding presentation before destroying graphics objects.
   The opt-in queue-ordered handoff (`HandoffSync::QueueOrdered`) requires a
   presenter that commits on MoltenVK's own `MTLCommandQueue`, exported with
   `CTRExportCommandQueue(device, runtime.gpu_queue())`; Metal's in-order queue
   then orders rendering, presentation and image reuse on the GPU, and the
   runtime only retires the previous frame's work one frame later. It is a
   measured experiment, not the default; see
   [the presentation experiment](PRESENTATION_SYNC_EXPERIMENT.md).
5. Pause stepping when appropriate, handle application audio/session lifecycle,
   and unload cleanly. The source SDK does not supply your app's UI or lifecycle.

## Execution model and current evidence

Azahar supplies ARM execution, console services, audio, loaders and PICA
rendering. Bergamot supplies Apple integration, no-JIT build policy, GPU handoff,
presentation, lifecycle and validation work. Guest ARM instructions execute
through precompiled interpreter handlers; graphics use PICA → Vulkan → MoltenVK
→ Metal. This is emulation, not native execution of a converted retail game.

The compile-time no-JIT core builds for Mac, iPhoneOS and Simulator. The Mac
independent consumers passed an original-homebrew test with an executable-memory
denial guard. Broader binary audit and renewed physical 3DS gameplay validation
remain open; the guard only covers exercised paths. The preview does not promise
universal compatibility or sustained smooth 4× performance.

## Content and redistribution

Supply your own compatible titles and any required keys, updates or system data.
See [user files](USER_FILES.md); importing keys is not a promise of encrypted-title
compatibility. No Nintendo games, keys, firmware, saves or signing files are in
this source archive.

Read [LICENSE](../LICENSE), [NOTICE.txt](../NOTICE.txt),
[third-party license texts](../THIRD_PARTY_NOTICES.txt), and
[release readiness](RELEASE_READINESS.md). The host integration is GPL-3.0-or-later
around credited upstream components. A separate archive does not remove those
obligations. Binary releases need their exact corresponding sources and notices;
this preview fetches dependencies rather than bundling a complete source offer.
