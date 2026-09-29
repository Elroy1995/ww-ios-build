# Optimization lessons from Dusklight

Source inspection on September 29, 2026: Dusklight
`b245c6bef8b4a370afb2453104a585dd41c97676`, with Aurora
`08122911e8621acb7ded6563813b264bec1494b5`. The supplied local checkout was
read without modification. This is an architecture comparison, not a Dusklight
performance benchmark. BlueWake measurements and their limitations are in
[the native 60 Hz profile](NATIVE_60HZ_PROFILE_2026-09-29.md).

## The largest relevant difference

Dusklight builds the game's recovered C/C++ directly for the host. Its
`CMakeLists.txt:309–374` collects game, actor, SSystem and JSystem sources and
links native Aurora implementations of the console SDK. Its main loop calls
`fapGm_Execute()` directly (`src/m_Do/m_Do_main.cpp:285–336`). Matrix operations
are ordinary C functions in `extern/aurora/lib/dolphin/mtx/mtx.c`.

BlueWake instead executes translated instructions against a PowerPC CPU state
and big-endian guest RAM, including cycle/deadline bookkeeping, register state
and dispatch. Our main-thread sample attributes about 67% to translated bodies
and another 12.5% to arithmetic/memory helpers. Replacing substantial, measured
hot routines with bounded native implementations is therefore the most
relevant lesson. This is a structural inference from the code and BlueWake's
profile; it does not quantify how much faster Dusklight would be on this Mac.

There is no ready-made true-60-Hz patch to transfer. Dusklight's
`src/dusk/game_clock.h:6` fixes each simulation step at 1/30 second. Its main
loop separates simulation from presentation and interpolates between
snapshots. `set_sim_rate()` changes the game clock's speed, not the duration
of an integration step. Using that as BlueWake's physics fix would speed up
gameplay rather than preserve normal gameplay at 60 smaller steps per second.

## Useful techniques and what BlueWake needs

| Technique found in Dusklight | BlueWake comparison | Priority / next experiment |
| --- | --- | --- |
| Native game/SDK routines, direct calls and native data access | Translated collision, animation, J3D draw preparation and SDK math dominate the game thread. Three certified native matrix leaves already exist as an opt-in experiment. | Highest. Capture real inputs to one whole collision or model loop; compare original/native output, guest-visible effects and full frame time. Tiny leaf speedups alone have not closed the gap. |
| Dirty flags reuse pipeline configuration, texture bindings and uniforms | `GxCoreState::build_draw_plan_into` rebuilds substantial derived state per draw. BlueWake already reuses vector capacity, caches textures/pipelines and deduplicates uniform uploads later in submission. | Medium. Cache the earlier derived state separately by its actual dependencies. Measure saved worker time and game-thread barrier time, not just fewer allocations. |
| GPU vertex pulling: upload raw vertex bytes and referenced arrays; shaders decode formats | BlueWake's GXCore path decodes vertices on the CPU into float arrays before upload. | Medium, larger change. Prototype one common vertex format behind an option, with the current decoder as the reference/fallback. Preserve indexed arrays, endian conversion, skinning, NBT, matrix indices and interpolation correspondence. |
| Merge adjacent compatible draws before submission | Dusklight joins compatible vertex/index ranges when graphics state is clean. BlueWake still reports roughly 13,700–14,350 draws in the measured scenes. | Medium. Instrument eligible consecutive draws first. Preserve primitive boundaries, EFB copies, render-pass transitions, blend/depth behavior and ordering. Draw count alone does not measure time saved. |
| FIFO processing, render encoding and pipeline compilation run separately | BlueWake already has a FIFO translation worker, Aurora render worker, pipeline compiler and cache writer. | Existing architecture. Adding duplicate workers is not the main-thread fix. Improve ownership and reduce work before widening renderer concurrency. |
| Original OS thread APIs map to native threads | Dusklight has a native `OSThread` side table and native DVD/audio support. BlueWake's translated game shares CPU state, guest memory and device scheduling. | Selective longer-term port. Move a bounded service or pure batch with explicit ownership; do not run the same guest CPU state on multiple cores. |
| Named workers and cache-affinity abstraction | Dusklight's shared-cache placement is implemented on Linux and Windows; its fallback used on macOS returns no cache domain. | Diagnostic benefit only on this Mac. It is not an Apple-Silicon performance-core switch. |

The renderer differences are concrete, but they are secondary for the current
60 Hz shortfall. BlueWake's measured graphics worker waits in roughly 44–46%
of samples, and headless execution still takes about 25 ms per game frame.
Headless removes the host renderer but still executes the game's draw
preparation. Making that worker infinitely fast would not remove the remaining
translated game work. Its idle share is not an exact upper bound on potential
improvement: synchronization, memory traffic and worker scheduling also matter.

## Relevant implementation locations

Paths in this list are relative to the supplied Dusklight checkout:

- `extern/aurora/lib/gx/command_processor.cpp:382–448`: cached array uploads,
  pipeline configuration, texture bindings and uniform ranges. Invalidation
  includes vertex format, line mode, render-target layout and texture binding
  generation; a single generic "state unchanged" flag would be insufficient.
- `extern/aurora/lib/gx/command_processor.cpp:489–550`: raw vertex uploads and
  adjacent compatible draw merging.
- `extern/aurora/lib/gx/shader.cpp:1780–2024`: shader-side raw attribute fetch,
  integer/float format conversion, storage buffers and endian handling.
- `extern/aurora/lib/gx/fifo.cpp:26–115`: dedicated FIFO worker. The inspected
  revision publishes at a draw batch size of **one**, not a large batched
  physics/job system.
- `extern/aurora/lib/gfx/render_worker.cpp`: bounded render queue (capacity
  256), explicit synchronization and reusable frame slots.
- `extern/aurora/lib/gfx/frame.cpp:650–709`: record work into frame packets,
  enqueue encoding/submission, release slots after consumption.
- `extern/aurora/lib/thread.cpp:234–255`: unsupported-platform affinity
  fallback; macOS does not receive the shared-cache placement hint.
- `src/dusk/OSThread.cpp` and `src/m_Do/m_Do_dvd_thread.cpp`: native OS-thread
  mapping and DVD service thread.
- `libs/JSystem/src/J3DGraphAnimator/J3DJoint.cpp:197–227` and
  `J3DModel.cpp:256`: shared current-matrix/current-model state. Even in this
  native port, these routines cannot simply become concurrent model jobs.

Corresponding BlueWake/RecompCore locations:

- `cmake/composite/module_export.c`, `native_math.c`, and generated dispatch:
  native routine integration and compatibility guards.
- `ref/recompcore/GXRuntime/graphics/gxcore/src/gxcore.cpp:577`: derived draw
  state construction; `:1109` begins the per-vertex CPU decoding loop.
- `ref/recompcore/GXRuntime/graphics/aurora/lib/gfx/gxcore_draw.cpp`: existing
  GPU pipeline, texture and uniform-upload caches.
- `ref/recompcore/GXRuntime/backends/aurora/aurora_graphics.cpp:360–545`:
  existing FIFO worker, hand-off, frame ownership and presentation barriers.

## Work sequence for native 60 Hz

1. Keep the opt-in timing mode and existing 30 Hz mode separate. Finish the
   remaining gameplay timing checks independently of performance work.
2. Remove overhead around native replacements. Resolve them once through the
   existing program-counter cache rather than testing every translated block.
   Continue comparing complete CPU/RAM results against the original routines.
3. Target larger measured units: collision traversal/query math, envelope
   matrix batches and repeated J3D/GX setup. Prefer immutable input snapshots
   and explicit output buffers. Preserve floating-point behavior and guest
   device deadlines at the boundary.
4. Introduce a persistent worker pool only when a native batch is large enough
   and its dependencies are explicit. Finish jobs before their same-tick
   consumers; keep actor interactions, events and result ordering deterministic.
5. Add derived graphics-state caching, then evaluate compatible draw merging
   or GPU vertex pulling using actual worker/barrier measurements and rendered
   comparisons. Do not replace BlueWake's Aurora fork wholesale: its retail
   FIFO frontend, GXCore compatibility fixes and frame interpolation differ.

None of these source observations establishes steady native 60 FPS. The target
remains complete physics/gameplay updates and a newly rendered frame within
16.67 ms, with unchanged game speed and verified behavior in demanding scenes.
