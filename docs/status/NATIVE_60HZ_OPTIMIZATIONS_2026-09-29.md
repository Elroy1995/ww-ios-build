# Native 60 Hz optimization implementation

The optional 60 Hz simulation mode is still experimental. These changes reduce
work while retaining a new physics tick and rendered frame on each gameplay
iteration. **Steady native 60 FPS has not been reached.** Fortress remains
limited by translated game work on the main thread.

This follows the [profile](NATIVE_60HZ_PROFILE_2026-09-29.md) and
[Dusklight comparison](DUSKLIGHT_OPTIMIZATION_COMPARISON_2026-09-29.md).
RecompCore implementation: `dc81247fe387c84bd9d679f2477c3db43558e01e`;
the builder pins that commit and patch 0105 preserves the source change.
No personal translated module, game data, or captured game image is distributed.

## Implemented paths

| Change | Behavior and boundary | Selection |
| --- | --- | --- |
| Derived graphics state | Reuses pipeline/shader state by exact register and relevant texture metadata values; a separate 16-entry cache reuses vertex layouts. Geometry, uniforms, mutable guest memory and cross-draw NBT still update each draw. | Derived cache on by default; `DOL_GXCORE_DERIVED_CACHE=0` disables it. |
| Adjacent draw merging | Joins contiguous vertex/index ranges with identical pipeline, texture bindings and uniform ranges, rebasing 16-bit indices. Viewport, scissor, pass changes and early-depth prepasses break merging. | On by default; `DOL_GXCORE_DRAW_MERGE=0` disables it. |
| GPU vertex decoding | Copies referenced raw attribute bytes into immutable packed records; shaders decode endian order, integer/float/color formats, matrix indices and NBT. Oversized records use the original decoder. Interpolation uses the original layout. | Opt in with `DOL_GXCORE_GPU_VERTICES=1`. |
| Native matrix arrays | Adds certified `PSMTXMultVecArray` to the three existing matrix leaves. Preserves accumulation order, final CPU/RAM state, reservations and cycles; unsupported input or an intervening device deadline falls back. | `BLUEWAKE_NATIVE_MATH=1`. |
| Persistent workers | Splits sufficiently large independent vector ranges across up to eight workers, with immutable matrix input and disjoint output. Joins before the same-tick consumer. Default is serial; Windows currently uses the serial fallback. | `BLUEWAKE_NATIVE_WORKERS=3` requests three workers. |
| Register-helper caller continuations | Certifies GPR save/restore bodies and changes eligible old-style calls to resume directly in their caller after a bounded native helper. RAM/exception/observer/budget guards retain the original fallback. | Prepare with `prepare_native_gpr.py`, rebuild, then `BLUEWAKE_NATIVE_GPR=1`. Default off; boundary/return/credit censuses disable it. |
| Actor-search budget checks | Replaces the nine monotonic block-budget checks per native no-match iteration with their equivalent final bounds. Guest cycle charge, node traversal and stopping points are unchanged. | Existing native actor-search path. |

The translator already contains direct cross-chunk calls through
`emit_cross_chunk_call`; this is not a missing command-line inlining switch.
The private composite used for these measurements predates that output pattern.
The GPR preparer upgrades its eligible calls without distributing translated
code. It found 6,899 eligible continuations across 251 chunks/variants.

The actor-search census needs care: enabling the boundary census selects the
full edge service and bypasses the existing native search. Its apparent 35%
share of boundaries therefore overstates the remaining ordinary-run cost.
The normal Fortress run still uses native search, with about 746,000 batches
covering 87.8 million nodes over the complete launch and measurement.

## Measurements and correctness

Personal Mac build, M3 Max, Aurora 640 by 480, fixed test input, interpolation
off, experimental simulation on. Load Outset, warp at retrace 900 to
`MajyuE:0:0:0`, then stand still. Timed window is retraces 1800 through 2600.
These are desktop measurements, not thermally controlled or iOS results.

| Rendered Fortress configuration | Actual FPS | Mean interval | 95th percentile |
| --- | ---: | ---: | ---: |
| Native matrix arrays and three requested workers | 36.17 | 27.65 ms | 29.63 ms |
| Plus derived state cache, draw merging and GPU vertices | 37.36 | 26.77 ms | 28.04 ms |

The graphics pair uses matching gameplay and draw content, but one run per
configuration does not establish the size of a small timing gain. The clearer
work reduction is about 14,400 submitted draws to 3,750 actual draws per frame,
roughly 74% fewer. The derived cache recorded 29.1 million hits and 1.08 million
misses (96.4% hits) in the optimized run.

The subsequent GPR experiment rebuilt the hot callers first: 380 prepared
sites in 15 chunks/variants, including eight main-DOL chunks. The other source
sites are prepared for a full rebuild but are not claimed as live-tested. All
four runs below use that same partial private module, the same host (including
the actor-search budget simplification), and the optimized graphics settings.
Only `BLUEWAKE_NATIVE_GPR` changes, in off/on/on/off order:

| GPR run | Actual FPS | Mean interval | 95th percentile |
| --- | ---: | ---: | ---: |
| Off A | 34.22 | 29.23 ms | 30.78 ms |
| On A | 36.29 | 27.55 ms | 29.37 ms |
| On B | 35.50 | 28.17 ms | 29.70 ms |
| Off B | 34.99 | 28.58 ms | 30.77 ms |

Pooling the two windows per mode gives approximately 34.60 versus 35.89 FPS,
a 3.7% increase. The pairwise gains vary substantially (6.1% and 1.4%); this is
a modest improvement, not evidence of steady 60. Each enabled launch reports
100,241,779 completed native calls and 23,294 guarded fallbacks. All 52 recorded
player-state lines agree between the compared runs. The rebuilt module is
different from the earlier graphics pair, so do not add their FPS changes or
interpret the earlier 37.36 result as a controlled comparison with GPR off.

An additional run with packed-vertex diagnostics recorded 26.8 million packed
draws and 12.1 billion fewer vertex-upload bytes over the complete 2,700-retrace
launch. This counter describes upload volume, not CPU time saved.

The worker experiment exposed only 120 array calls totaling 960 vectors, with
a maximum of eight vectors per call. No parallel batch was dispatched in
Fortress. Coarse model/envelope or independent collision batches still require
separate native implementations; adding workers to these small calls cannot
close the frame-time gap.

At captured frame 1600, the original CPU-decoded/cache-disabled/unmerged path,
the GPU/cache/merge path, and the GPR-enabled path produced exactly the same
640 by 480 RGBA image: zero differing pixels across 307,200 pixels. The RGBA
SHA-256 is `b18314ad8b00159579b43e2620dcbb2412088f46a20c25360f30a8f4f4a2394f`.
This validates this particular rendered state, not every camera, scene or
format. GPU decoding remains opt-in for wider validation.

Validation completed:

- 12,000 full CPU/RAM comparisons for the existing native matrix leaves.
- 360 array comparisons including in-place output, large worker batches,
  altered quantization, NaNs, short budgets and device deadlines.
- 2,880 complete-state comparisons across all 36 GPR entry points. The r14
  block-leader path retains suffix 1; precise suffix entries retain suffix 0.
- Actor-search deadline equivalence across 1.5 million nearby budget cases
  and signed-counter edge cases, compared with the original nine checks.
- Source certification/idempotence/drift rejection, cycle-domain and timing
  tests; GXCore fixtures with cache on and off; packed attributes decoded back
  through the original decoder; texture tests and three draw-merge tests.
- Rendered Fortress runs with 60 player integrations and 60 scene collision
  passes per 60 retraces, plus the exact framebuffer comparisons above.

A short FIFO trace could not replay independently because its initial state
was incomplete; it is not counted as a passing replay. The real framebuffer
capture supplied the rendered comparison instead.

## Repeating a rendered run

Use a personal, certified rebuilt composite and the local host. The existing
runner takes `COMPOSITE` and a final host-binary argument. For example, from the
checkout, with those two absolute paths set:

```sh
SIMULATION=1 RENDERER=aurora PACE=0 SCALE=1 WALK=1 WALK_LEN=1 WALK_Y=0 \
EXTRA_ENV='BLUEWAKE_TEST_INPUT_ONLY=1 BLUEWAKE_SETTINGS=none BLUEWAKE_SIMULATION_LOG=1 BLUEWAKE_FAST_FORWARD=0 BLUEWAKE_FADE_FRAMES=0 BLUEWAKE_FRAME_TIMING=1 BLUEWAKE_TEST_WARP=900:MajyuE:0:0:0 BLUEWAKE_NATIVE_MATH=1 BLUEWAKE_NATIVE_GPR=1 DOL_GXCORE_GPU_VERTICES=1 DOL_GXCORE_OPT_LOG=1' \
scripts/mac/run_host.sh "$PWD/build/fortress-check" 0 2700 load '' '' "$HOST_BINARY"
```

Compute throughput from the elapsed `frame-timing` timestamps at retraces 1800
and 2600: `800000000 / (end_us - start_us)`. An FPS overlay or the simulation
counter alone does not prove 60 updates per wall-clock second. Keep compiler
jobs out of the timed window and compare the same module, host and route.

Optional real-frame capture: add `DOL_GXCORE_CAPTURE_FRAME=1600` and
`DOL_GXCORE_CAPTURE_PATH=/absolute/private/output.pam`. The diagnostic waits for
GPU readback, so keep that frame outside the timed window. Native samples and
private builds also remain under ignored `build/`.

## Remaining acceptance work

The measured game thread still exceeds the 16.67 ms budget. Larger native
collision/model/UI routines and useful independent batches are needed; the
renderer changes and register helpers alone do not supply the missing time.
J2D picture/window drawing dominates the named functions in the previously
sampled hot `802D16E0` chunk; a chunk address alone should not be mislabeled as
J3D animation.

The known running-onset/ladder timing discrepancy and combat, sailing, ropes,
moving platforms and actor-specific timers still need route validation or
fixes. These performance changes do not complete that physics conversion.
