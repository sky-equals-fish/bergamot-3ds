# Public release preparation — November 7, 2026

Status: local source previews under review. No repository, release, tag or binary
has been published. The initial public product scope is 3DS, DS and GBA; the
standalone reusable package focuses on 3DS. A working development install is not
itself a certified public release.

## Completed safeguards and current evidence

- Separate UI-free 3DS CMake entry point and original-homebrew consumer. The API is
  compiler/toolchain-coupled C++ source, not a stable C ABI/XCFramework.
- Pinned upstream revisions and local patches; proper Azahar attribution. The
  contribution is Apple/no-JIT integration and runtime/presentation engineering,
  not a newly invented emulator or native recompilation of commercial titles.
- Source exporter uses explicit component allowlists, refuses private-content
  extensions and signing-key markers, excludes Git history, vendor/research trees,
  games, saves, firmware, keys, downloaded art, signing files and imported skins.
  Every exported file and archive receives a SHA-256 receipt.
- Private White N3DS artwork is now off by default in app packaging; its supplied
  README expressly records that no redistribution permission was supplied.
  Original pixel artwork remains. Cached cover art is never a release input.
- GPL-3.0 text, notices for Azahar/MoltenVK/mGBA/melonDS, and collected dependency
  license texts accompany source previews and the updated app bundle.
- Runtime lifecycle/error containment, save safety, pixel conversion, frame pacing,
  GPU handoff ownership and independent host checks passed during this work.
  See RUNTIME_AUDIT.md for exact scope, limitations and local evidence pointers.
- iPhone/Simulator UI builds and short physical 3DS/GBA/DS launches passed before
  the later compile-time JIT removal. Those tests do not certify the rebuilt core.

## No-JIT hardening in this release pass

The previous core disabled CPU/PICA shader JIT at runtime but linked unused native
code generators. `0007-azahar-no-jit-build.patch` adds a compile-time exclusion for
Dynarmic CPU code and PICA shader JIT, removes code-generator link dependencies,
and forces the core's CPU/shader JIT settings off on Mac as well as iOS. It retains
the header-only robin-map dependency needed by the renderer. Built-in console
key blobs remain disabled. Normal public Metal driver shader compilation is a
separate operation from executing generated guest CPU code.

Fresh per-platform builds, graph/symbol inspection and original-homebrew checks
must pass before this change is promoted. A macOS diagnostic interposer rejects
executable-memory requests on tested paths; it cannot prove every unexercised path
or replace physical-device checks. Results from this pass are appended below.

## Binary-release gates still required

- Archive exact corresponding source for every shipped GPL/LGPL/MPL component,
  including local changes and required dependency/submodule sources/build scripts.
  A GitHub link or version pin by itself is not the complete binary-source offer.
- Verify release entitlements, architecture, deployment target, dependency hashes,
  unsigned/signed packaging procedure and a fresh install using the recipient's
  signing identity. Development provisioning material must not enter a public zip.
- Exercise saves/reopen for real DS gameplay, more 3DS titles and error cases,
  background/foreground, controller transitions, low storage and interrupted imports.
- Measure repeatable cold/warm routes and sustained physical-device thermals/audio.
  Short 4x boot samples still had stalls; smooth sustained 4x is not established.
- Review rights for branding, any nonoriginal asset and content-related documentation.
  User-provided-content notices are not a guarantee against legal claims. Public
  asset availability is not automatically permission to redistribute it.

For source-only publication, keep these limitations visible. Do not label this
preview flawless, universal, App Store approved, or legally risk-free.

## License and content references

The GNU GPL FAQ explains corresponding-source access for binary downloads:
https://www.gnu.org/licenses/gpl-faq.html#SourceAndBinaryOnDifferentSites
The US Copyright Office describes the separate anti-circumvention rules and
limited exemptions: https://www.copyright.gov/dmca/ . These sources support
release precautions, not a jurisdiction-independent legal conclusion.

See USER_FILES.md for user-facing setup and THREEDS_SOURCE_PACKAGE.md for the
standalone source SDK. No Nintendo game or key data is required to run the original
automated probe.

## Latest local evidence — November 7

Compile-time no-JIT core builds completed for macOS, iPhoneOS and Simulator.
The original ARM11 probe passed both the separate three-ds consumer and the
existing independent headless host with the macOS executable-memory denial
interposer active. The three-ds example now supplies its build-tree core rpath.
Logs are under artifacts/focus-review/no-jit-*-guard.log in the frontend
workspace. Build-graph/symbol review and renewed physical 3DS gameplay testing
remain pending; these narrow host checks do not establish complete binary
removal or sustained iPhone performance. The updated phone app was installed
for DS layout testing and White reopened, not for a new 3DS performance claim.

## GitHub documentation preparation — November 7

The frontend README now describes the three-system scope and includes actual
Simulator Home/controller screenshots, including the final 151-unit DS D-pad.
The standalone guide documents checksum verification, Mac/iPhoneOS/Simulator
builds, CMake integration, embedding, signing and the runtime contract. The
source exporter includes the app gallery and gives each archive a working
root README. Documentation links and source-manifest hashes were checked.

The documented standalone iPhoneOS and Simulator library builds passed. A
freshly extracted 3DS source archive generated the original probe, built its
independent Mac consumer and ran it successfully, using locally cached pinned
dependencies and the existing core build. This is not a fresh network bootstrap
or a new signed-device 3DS gameplay check. Evidence is in frontend
artifacts/focus-review/readme-archive-validation.json, readme-archive-consumer.log
and readme-sdk-{ios,simulator}.log. No remote repository or release was changed.

## Public 3DS source repository — November 8

The 3DS component was exported with `tools/prepare_source_release.py --component 3ds`
and published as the public source repository
https://github.com/sky-equals-fish/bergamot-3ds with a fresh history. It is a
source publication only: no binary release, tag, prebuilt library or signed app.
The main Bergamot repository stays private because its history contains material
without redistribution permission. The binary-release gates above still apply.
