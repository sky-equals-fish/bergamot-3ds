# Bergamot patches to Azahar

The 3DS core is Azahar at the commit pinned in [`dependencies.json`](../dependencies.json),
plus these patches. `tools/bootstrap.py` applies them in order to a fresh
checkout. They are Bergamot's work and responsibility and have not been
submitted upstream.

| Patch | What it changes | Why |
| --- | --- | --- |
| `0001-libretro-frame-synchronization` | Flushes the Vulkan worker before handing a frame to the host, keeps the scheduler's wait/signal semaphores on the libretro path, declares Metal export for the output image and command queue, adds a small C bridge (`ctr_bridge.h`) for title metadata, CIA install, performance counters and guest-memory reads | Correct, zero-copy Vulkan → Metal frame handoff and host integration |
| `0007-azahar-no-jit-build` | Adds `BERGAMOT_NO_JIT`: excludes Dynarmic, Oaknut, Xbyak and the PICA shader JIT from the build and forces CPU/shader JIT off on every platform | No runtime code generation anywhere, Mac included |
| `0008-azahar-libretro-async-shaders` | Exposes the renderer's existing async pipeline compilation as the libretro option `citra_async_shader_compilation` (default off) | Lets a host opt into compiling new pipelines on worker threads |
| `0009-azahar-libretro-shader-preparation` | Reports disk shader-cache progress to the host (`ctr_set_shader_progress`) and adds `ctr_wait_shader_cache()`, which waits until every cached shader and pipeline is built | "Preparing shaders": play starts only once everything the cache knows is ready |
| `0010-azahar-batch-cro-cache-invalidation` | Batches instruction-cache invalidation during CRO (code module) loading: one pass per run of touched 4 KiB pages instead of one full block-cache scan per relocation | Removed 0.7–0.9 s freezes when games load modules (measured on a Mac host) |
| `0011-azahar-restore-encrypted-content` | Restores the NCCH decryption Azahar removed upstream (Citra's implementation: key slots, seeds, ExeFS and RomFS counters), lets CIA installs decrypt with the user's keys, and makes ECC verification fail cleanly when `keys.txt` has no ECC root key | Encrypted `.cia` and `.3ds` files play with the user's own `keys.txt` |
| `0012-azahar-moltenvk-pipeline-builds` | On MoltenVK, skips the "build only if cached" pipeline probe (MoltenVK compiles instead of failing fast, which serialized every build on the render thread), so pipelines build on the worker threads; enables extended dynamic state on Apple through the libretro device; adds `BG_PIPE_LOG` (per-pipeline build log) and `BG_NO_EDS` diagnostics | Preparing a cached game's shaders took 11 s instead of 21.5 s (Mac); depth, cull and stencil variants share a pipeline |

## Using the patches without Bergamot

Apply them to the pinned Azahar commit:

```sh
git clone https://github.com/azahar-emu/azahar.git && cd azahar
git checkout 6b4e7f2e694514fdf2f72dc20368609b7cd4912e
git submodule update --init --recursive
for p in 0001-libretro-frame-synchronization 0007-azahar-no-jit-build \
         0008-azahar-libretro-async-shaders 0009-azahar-libretro-shader-preparation \
         0010-azahar-batch-cro-cache-invalidation 0011-azahar-restore-encrypted-content \
         0012-azahar-moltenvk-pipeline-builds; do
  git apply "/path/to/bergamot-3ds/patches/$p.patch"
done
```

Patches 0007, 0008 and 0010 are independent of Bergamot's host and may be
useful on their own: 0010 in particular applies to any Azahar build that uses
FastInterp. 0001 and 0009 extend the libretro core with a C bridge that this
host uses; the patched core is a matched pair with `ctr::Runtime`, not a
drop-in core for arbitrary libretro frontends.
