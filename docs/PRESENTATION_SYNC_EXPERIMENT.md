# Presentation synchronization experiment — 2026-11-07

Status: implemented as opt-in; synchronous presentation remains the default.
The signed iPhone build installed successfully. Physical-device benchmark launch
was blocked by the phone being locked. The user then confirmed the phone is out
of battery and charging; the device comparison is pending. No phone performance
result is claimed.

## Change

The existing path waits for Vulkan rendering, submits the Metal display pass,
and immediately waits for that pass to finish. The experiment keeps the Vulkan
producer fence, but defers the Metal completion wait until the core next asks to
reuse its output image. CPU work can overlap that display pass. It does not
replace Vulkan/MoltenVK, change game timing, add CPU frame copies, or implement
a direct Metal PICA renderer.

Azahar's two frame records share one output image. Consequently the experiment
still advertises one synchronization index. `wait_sync_index` drains the previous
Metal completion before `GetRenderFrame` reuses or recreates the output image.
Pause, readback and unload also drain it. A completion token holds the command
buffer strongly and must finish all source reads even when reporting an error.
Callback errors are surfaced at the runtime boundary. Cores that request an
explicit release semaphore retain a synchronous release path.

## Validation

- Lifecycle test passed: deferred completion, wait before the next presentation,
  final completion on unload, no duplicate completion, and injected completion
  failure contained at the C callback boundary with subsequent stepping stopped.
- Sanitized homebrew probe completed with Metal API and shader validation enabled.
- Homebrew 4x synchronous/deferred captured images match byte for byte.
- Release host probe and signed physical iPhone app builds passed.
- Eight Alpha Sapphire plus update boot runs completed, 1,200 frames per run,
  using an isolated APFS copy of the prepared runtime data.

## Host observations

These are unpaced Mac M3 Pro offscreen runs, not physical iPhone performance.
The order at each scale was sync / deferred / deferred / sync. Shader caches,
startup timing and host load were not fully controlled. The captures differ
between some repeated runs, so this is not a matched-scene gameplay benchmark.
Do not infer a percentage speedup or compare scales from these timings.

| Scale | Mode | Elapsed seconds | Step p95 ms |
| --- | --- | ---: | ---: |
| 3x | sync | 7.032 | 10.108 |
| 3x | deferred | 5.755 | 9.628 |
| 3x | deferred | 5.187 | 8.429 |
| 3x | sync | 5.848 | 9.887 |
| 4x | sync | 4.003 | 5.694 |
| 4x | deferred | 6.982 | 11.490 |
| 4x | deferred | 7.653 | 12.730 |
| 4x | sync | 6.319 | 11.804 |

The deferred path reduces the measured synchronous presentation wait, but total
frame time is mixed, especially at 4x. There is insufficient evidence to enable
it by default or claim that lag above 3x is fixed. High-resolution rendering
still processes more pixels regardless of this scheduling change.

Raw results: `artifacts/sync-host-results.json`, `sync-alpha-*.log` and `.ppm`.
Safety evidence: `artifacts/sync-lifecycle.log`, `sync-homebrew-*.log` and `.ppm`.
Build evidence: `artifacts/sync-release-build.log`, `sync-device-build.log` and
`sync-device-install.json`.

## Reproduction and next measurement

Host: `ctr-metal-probe GAME ISOLATED_DATA OUTPUT.ppm 1200 4 --deferred`;
use `--sync` for baseline. Use a Release, non-sanitized build for timing.

Phone: launch with `--ctr-deferred-present` to opt in; omit it for baseline.
Existing `--ctr-game`, `--ctr-scale` and `--ctr-steps` flags remain available.
Metrics in Documents/Runtime/metrics-*.jsonl include the mode plus cumulative
producer-wait, presenter and consumer-wait milliseconds and handoff frame count.
Use differences between samples for per-interval costs. These are CPU wall-time
waits, not independently measured GPU execution durations.

Compare repeatable game scenes on the 15 Pro at 3x and 4x in alternating order,
recording frame pacing, emulation speed, audio drops and thermal state. A boot
smoke test alone does not establish sustained gameplay performance. Retain the
default until correctness and a repeatable device benefit are demonstrated.

## November 7 handheld-focus device follow-up

The updated no-JIT app was installed over the paired iPhone 15 Pro's local-network
connection. Two 1,800-callback-frame Alpha Sapphire boot runs used 4x: synchronous
first, then deferred. Each produced 30 one-second samples over approximately
30.1 seconds. This was not a controlled warm-cache ABBA or sustained gameplay test.

| Observation | Synchronous | Deferred |
| --- | ---: | ---: |
| Median sampled guest speed | 99.985% | 99.998% |
| Worst individual runtime step | 163.769 ms | 51.641 ms |
| Median of per-window step-work p95 | 17.027 ms | 16.287 ms |
| Producer CPU wait / handoff frame | 7.006 ms | 8.147 ms |
| Presenter CPU time / handoff frame | 2.572 ms | 1.000 ms |
| Dropped audio frames | 0 | 0 |
| Maximum reported thermal state | nominal | nominal |

Step work includes runtime and presentation waits, excluding pacing sleep. These
CPU timings are not GPU execution time. Boot animation game FPS must not be
reported as sustained gameplay FPS. Both modes still hitch; order/cache effects
prevent attributing their difference to deferred presentation. It remains opt-in.
Local evidence is in the front-end worktree's `artifacts/focus-review/`:
`phone-sync-4x.jsonl`, `phone-deferred-4x.jsonl`, and
`phone-short-comparison.json`. Private runtime data is excluded from Git.

The next evidence gate remains matched gameplay at 3x/4x with warm caches and
alternating modes, including sustained thermal and audio/pacing observations.

## November 8 queue-ordered handoff — opt-in, Mac evidence only

Status: implemented as a third opt-in mode; synchronous presentation remains
the default. Mac evidence and a first physical iPhone boot comparison are below;
matched gameplay scenes are still pending.

### Change

Both earlier modes wait on the CPU every frame: the producer fence wait
(`vkWaitForFences` after submitting the core's render-ready semaphore) and,
for deferred mode, the Metal completion wait at the core's image-reuse
boundary. On the phone those were about 7–8 ms and 1–2.5 ms per 4x frame.

The queue-ordered mode (`ctr::VulkanBackend::HandoffSync::QueueOrdered`)
removes both per-frame waits without changing image ownership:

- The host exports MoltenVK's own `MTLCommandQueue` from the core's `VkQueue`
  through `VK_EXT_metal_objects` (`CTRExportCommandQueue`) and creates the
  Metal presenter on it (`initWithCommandQueue:`). The core patch chains
  `VkExportMetalObjectCreateInfoEXT` (command queue) onto device creation
  whenever the host negotiated the extension; the texture export is unchanged.
- Metal executes command buffers on one queue in commit order, so the core's
  rendering, the presenter's draw and the core's next write to the shared
  output image are ordered on the GPU. `wait_sync_index` therefore no longer
  drains on the CPU in this mode.
- The render-ready semaphore is still consumed by one submission per frame;
  its fence and the previous Metal completion are retired at the start of the
  next presentation, one guest frame later. That bounds run-ahead to one
  presented frame and throttles only when the GPU is the bottleneck.
- The mode is refused unless MoltenVK reports synchronous queue submits
  (`vkGetMoltenVKConfigurationMVK`), which ordering by commit depends on. The
  pinned 1.4.1 static library reports it; the lifecycle test asserts it.
- Pause, readback, unload and destruction still drain fully. Completion
  errors are contained at the callback boundary and stop stepping.

Host flags: `ctr-metal-probe … --queue-ordered`; app `--ctr-queue-ordered-present`
or the Settings tile `3DS / GPU HANDOFF` (`SYNC EACH FRAME` or `QUEUE ORDERED`, stored as
`BGQueueOrderedPresent`, read at launch).
Metrics add `presentation_mode` and `image_reuse_waits` / `handoff_reuse_waits`.

### Validation

- ASan/UBSan and Release lifecycle tests pass: queue-ordered handoff never
  runs more than one presented frame ahead, performs zero image-reuse waits
  while the serialized deferred path performs one per reused frame, drains on
  unload exactly once, and contains a throwing completion with stepping stopped.
- Sanitized `ctr-metal-probe` homebrew runs at 4x with Metal API and GPU
  validation enabled: sync, deferred and queue-ordered captures match byte for
  byte (`artifacts/queue-ordered-handoff/homebrew-4x-*.ppm`).
- The rebuilt no-JIT macOS core applies the regenerated
  `0001-libretro-frame-synchronization.patch`; `0007` is unchanged.
- Alpha Sapphire plus update booted 1,200 frames in every mode at 3x and 4x on
  isolated APFS clones of one prepared data root (`bench_handoff.py`). The 4x
  sync and queue-ordered captures both show the language-select screen; final
  frames differ in animation phase between runs of every mode.

### Mac host observations

Release M3 Pro offscreen boot runs, order per scale sync / deferred /
queue-ordered / queue-ordered / deferred / sync. These are not phone results,
not matched gameplay, and the boot sequence is lighter than gameplay. Waits
are CPU wall time totals over 1,200 frames.

| Scale | Mode | Elapsed s | Step p95 ms | Producer wait ms | Consumer wait ms | Reuse waits |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 3x | sync | 3.351 | 2.888 | 1158.7 | 0 | 0 |
| 3x | deferred | 6.074 | 7.679 | 3549.7 | 110.1 | 1199 |
| 3x | queue-ordered | 1.300 | 1.701 | 0.2 | 157.5 | 0 |
| 3x | queue-ordered | 1.285 | 1.776 | 0.2 | 146.4 | 0 |
| 3x | deferred | 6.096 | 7.862 | 3543.7 | 105.1 | 1199 |
| 3x | sync | 2.844 | 3.670 | 1255.3 | 0 | 0 |
| 4x | sync | 3.422 | 3.574 | 1717.6 | 0 | 0 |
| 4x | deferred | 8.490 | 10.097 | 5455.6 | 98.0 | 1199 |
| 4x | queue-ordered | 1.715 | 2.074 | 0.2 | 373.7 | 0 |
| 4x | queue-ordered | 1.656 | 2.034 | 0.2 | 339.6 | 0 |
| 4x | deferred | 8.573 | 10.140 | 5515.2 | 71.9 | 1199 |
| 4x | sync | 3.411 | 3.546 | 1713.6 | 0 | 0 |

The per-frame producer wait disappears in queue-ordered mode on this host, and
the remaining consumer wait is the one-frame-late retirement that throttles
against the GPU. Deferred mode is slower than sync here because its reuse
wait lands before the core records the next frame. Do not infer a phone
speedup from this table; it demonstrates that the emulation thread no longer
serializes with the GPU each frame, which is a different claim.

Raw results: `artifacts/queue-ordered-handoff/handoff-results.json` with per-run
logs, captures, the benchmark script and `SHA256SUMS`.

### iPhone 15 Pro boot comparison, November 8

The signed app with the rebuilt no-JIT iPhoneOS core was installed over the
paired connection (`device-install.json`; games, saves and runtime data on the
phone were untouched). Four 1,800-callback-frame Alpha Sapphire plus update 4x
boot runs were launched with `devicectl`, order sync / queue-ordered /
queue-ordered / sync, each about 30 seconds with 30 one-second samples. This is
the boot and language-select sequence, not gameplay, and the shader cache was
warm from earlier sessions.

| Observation | sync 1 | queue-ordered 1 | queue-ordered 2 | sync 2 |
| --- | ---: | ---: | ---: | ---: |
| Median sampled guest speed | 99.98% | 99.99% | 99.99% | 99.73% |
| Median of per-window step-work p50 | 15.334 ms | 5.589 ms | 5.671 ms | 16.575 ms |
| Median of per-window step-work p95 | 17.922 ms | 7.028 ms | 7.081 ms | 18.497 ms |
| Worst individual runtime step | 217.888 ms | 48.801 ms | 79.030 ms | 82.032 ms |
| Producer CPU wait / handoff frame | 7.451 ms | 0.001 ms | 0.002 ms | 8.546 ms |
| Presenter CPU time / handoff frame | 2.304 ms | 1.188 ms | 1.273 ms | 2.198 ms |
| Consumer CPU wait / handoff frame | 0 | 0.029 ms | 0.010 ms | 0 |
| Pacing resets | 6 | 2 | 3 | 83 |
| Dropped audio frames | 0 | 0 | 0 | 0 |
| Maximum reported thermal state | fair | fair | fair | nominal |

The emulation thread's per-frame work roughly halves in both orders, which is
the expected effect of no longer waiting for the GPU inside each frame. Guest
speed and pacing were already correct in sync mode here, so this is a headroom
result, not a game-FPS result. The second sync run shows more pacing resets and
a lower median speed; the phone had been running for three consecutive runs by
then, so it is not attributed to the mode. Step intervals stay near the paced
16.7 ms in every run. Raw files: `phone-*-metrics-*.jsonl`, `phone-runs-pair*.json`,
`phone-launch-*.json` and `phone-comparison.json` in
`artifacts/queue-ordered-handoff/`.

### Next evidence gate

Compare matched warm-cache 3x/4x gameplay scenes (field and battle) on the
iPhone 15 Pro, alternating sync and queue-ordered, with sustained thermal,
audio and pacing observations, and check for visual regressions in play.
Keep synchronous presentation as the default until that comparison shows a
repeatable benefit with no visual regressions. The core recreates its output
image only on size changes, which the current hosts fix at launch; Metal's
retained command-buffer references cover the exported texture in that path.

## November 10: queue-ordered becomes the Bergamot 3DS app default

Matched warm-cache runs in the standalone app on the iPhone 15 Pro (Auto and 4×, order
sync / queue / queue / sync) and on the Mac are recorded in [STATUS.md](STATUS.md). The app
now defaults to queue-ordered and falls back to the per-frame wait when MoltenVK does not
report synchronous queue submits. `BGQueueOrderedPresent` stores only an explicit choice.
Still open from the gate above: field and battle scenes, sustained thermal runs, and a
visual comparison during play.
