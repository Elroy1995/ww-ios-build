# Native 60 Hz bottlenecks — 2026-09-29

The experimental mode is primarily limited by the main game thread on this
Apple M3 Max. Reaching 60 real gameplay updates and renders per second needs
substantial reductions in translated game work. Lowering resolution does not
close the gap.

## Measured performance

Same private timing-patched module and Release host, from the isolated
`true-60hz` worktree based on `2c9f16c`; RecompCore `b4af144` plus the then-uncommitted
presentation diagnostics (subsequently checkpointed as `fbb5943`). Interpolation is
off, the experimental simulation mode is on, and saved settings are bypassed.
The tests load a copy of the Outset card, stop scripted button presses, and
leave Link stationary. Fortress uses the existing test warp at retrace 900:
`MajyuE:0:0:0`. Its room was also inspected in the running app.

| Scene / backend | Actual updates per second | Mean interval | 95th percentile interval | Main-thread CPU busy |
| --- | ---: | ---: | ---: | ---: |
| Outset, Aurora 640×480 | 39.03 | 25.62 ms | 26.92 ms | 99.1% |
| Outset, Aurora 1920×1440 | 38.55 | 25.94 ms | 27.83 ms | 100% |
| Outset, headless | 40.30 | 24.82 ms | 26.67 ms | 100% |
| Fortress exterior, Aurora 640×480 | 34.72 | 28.80 ms | 30.44 ms | 100% |

These rates come from elapsed wall time between retraces 1800 and 2600, after
loading and profiling. At retrace 2400, all four runs report 60 main-loop
iterations, 60 player integrations and 60 scene collision passes per 60
retraces. Rendered frame counters agree with the wall-time rates. Those are
actual new game frames, not interpolated frames; the simulation runs slower
than real time when the host misses its budget.

Headless still executes the guest's drawing and display-list construction;
it removes the Aurora renderer, not all graphics-related game CPU work. The
roughly 1% resolution difference is within ordinary run variation. These
are sequential desktop measurements with other applications running, not
thermally controlled benchmarks or device results.

The Outset view reports about 13,700 draws per frame; this Fortress spawn
reports about 14,350. This is not a claim to have covered every Fortress
camera angle, including the previously documented 17,500-draw stress view.

At 60 FPS the budget is 16.67 ms. Outset needs about **9 ms / 35% less frame
time**, and this Fortress view about **12 ms / 42% less**. Expressed as
throughput, those are approximately 1.54× and 1.73× improvements.

## Where the CPU time goes

Eight-second native stack samples were captured after gameplay began, before
the timed measurement window. Sampling temporarily slows the game, especially
in Fortress; neither the FPS shown during capture nor those retraces are used
for the performance table.

The Outset 640×480 sample, with disjoint main-thread attribution:

| Owner | Approximate share of main-thread samples |
| --- | ---: |
| Translated game function bodies, exclusive of called helpers | 67.2% |
| PowerPC arithmetic / paired-single / memory helpers | 12.5% |
| Block-boundary service, including its control hooks and native actor search | 9.7% |
| Dispatch loop itself | 4.1% |
| Remaining host work | 6.6% |

Rounding accounts for the total. The headless sample independently reports
69.2%, 13.2%, 10.3%, and 3.5% for the first four groups. Parent and child
inclusive samples are not added together. The new simulation adapter functions
are negligible in these captures; the expense is running the game work twice
as often, not the cost of multiplying movement by a half step.

The graphics worker is waiting in about 44% of Outset samples and 46% of
Fortress samples. Its busiest work is `GxCoreState::build_draw_plan_into`,
followed by packet/vertex assembly and memory copying/comparison. This is a
secondary optimization target and will matter more as the game thread gets
faster. These waiting shares are sampling observations, not GPU timestamp
measurements or an exact worker budget at 60 FPS.

## Optimization order

1. **Optimize complete hot game routines, starting with collision and model
   work.** Native samples repeatedly rank the chunks containing spatial
   collision, GX state construction, matrices and J3D animation near the top.
   A separate guest-PC capture in Fortress names `cM3d_Cross_MinMaxBoxLine`,
   `cBgW::GroundCrossRp`, `GroundCrossGrpRp`, `PSMTXConcat`, `PSMTXMultVec`,
   `J3DModel::calcWeightEnvelopeMtx`, and `recursiveCalc` as concrete candidates.
   Port or specialize whole loops so they avoid repeated emulated-register,
   big-endian memory, paired-single and dispatch work. Merely moving identical
   translated statements into a native wrapper has already failed in prior
   register-helper experiments. First benchmark one bounded collision or
   matrix/model routine against recorded inputs before expanding the port.

2. **Reduce CPU draw preparation and repeated static work.** Hot chunks include
   J3D display-list generation and GX attribute, texture, TEV and transform
   setup. The Fortress guest sample also identifies `dKyr_drawStar` and
   `wave_move`. Cache unchanged draw state and immutable topology; invalidate
   on the data that actually changes. Keep simulation and visible animated
   results current every tick. Skipping every other physics or animation
   update would not satisfy this mode's purpose.

3. **Restore a cheap common path through dispatch hooks.**
   `runtime/host/src/main.c:1731` calls mouse-camera and jump adapters at every
   translated block boundary. The mouse adapter handles just two guest entry
   addresses (`mouse_camera.c:654`), yet accounts for about 2.3% of main-thread
   samples including its calls. Gate rare addresses before calling adapters,
   and keep the ordinary edge-service return free of unnecessary register
   saves. Preserve interrupt/deadline checks and controller behavior. The
   entire edge service plus dispatcher is only about 14% here: even removing
   all of it could not supply the required 35–42% reduction.

4. **Then reduce graphics-worker state rebuilding.** Profile dirty-state
   caching around `GxCoreState::build_draw_plan_into`, vertex layout/topology
   assembly and repeated packet copies. Preserve ordering and guest-memory
   invalidation. The current worker already runs separately, so simply
   proposing another render thread would miss the existing architecture.

5. **Retest compiler tuning on the final hot paths.** This host build is
   Release `-O3` without host PGO; the private module uses the existing
   profile-guided module objects and timing-patched replacements. A fresh
   rendered 60 Hz host profile and representative module training are useful
   experiments. Verify actual profile coverage before rebuilding everything.
   Prior notes already reject blindly retraining covered Outset chunks,
   blanket FP fast paths, native register-save wrappers, and larger cycle
   caps as solutions. Compiler tuning has no measured gain in this report.

Guest-PC sampling reads the last stored guest PC. Compiler-elided stores and
dispatch boundaries bias its counts; it identifies routines to investigate,
not reliable percentages of execution time. In particular, prominent
`__save_gpr` / `__restore_gpr` entries are not evidence to repeat the refuted
register-wrapper optimization. No single measured routine accounts for the
whole performance gap.

## Using more CPU cores

This is a worthwhile part of the optimization plan. The current graphics FIFO
already has a dedicated worker (`aurora_graphics.cpp:486`), while the translated
game executes serially through a shared CPU state and guest memory. More cores
do not automatically accelerate that instruction stream. Extracting native
work with explicit inputs and outputs creates useful parallelism.

| Candidate | Useful job boundary | Required change |
| --- | --- | --- |
| Model animation, envelope matrices and vertex transforms | Independent models or batches of independent output matrices | Replace global J3D scratch state with a per-job context; finish before the model's draw or gameplay consumer. |
| Collision queries | Independent read-only queries against a stable world snapshot | Give each query its own result/scratch storage; preserve query ordering and deterministic tie-breaking; merge before dependent actor movement. |
| Visual particles / environment geometry | Independent particles or output vertex ranges after their inputs are fixed | Separate random-state advancement, callbacks and shared emitter updates from arithmetic jobs. This is an opportunity to investigate, not yet a measured large owner. |
| Draw-plan construction / vertex decoding | Batches using immutable snapshots of render state and referenced memory | Parse state-changing commands in order, parallelize independent construction, then submit completed results in original order. |

The source shows why this is a refactor rather than a thread-count setting.
`J3DModel::calc` writes `j3dSys`'s current model and flags and invokes callbacks;
`J3DJoint.cpp` mutates global `J3DSys::mCurrentMtx` and `mCurrentS`. Calling
those existing functions simultaneously would corrupt the shared context.
Collision recursion also updates a query's best-hit state (`mNowY`), and
moving backgrounds change the world it reads. Arbitrary actor updates cannot
be launched concurrently while preserving the original dependency order.

Start with a persistent, bounded worker pool and coarse jobs, not a thread per
actor or a task per tiny matrix multiply. Capture inputs, execute independent
jobs, wait at the actual dependency boundary, and apply results deterministically
within the same simulation tick. Avoid a blanket wait after every small task,
which would leave the critical path effectively serial. Keep controller input,
events, actor-order-dependent interactions and render submission ordered.

An idealized four-core calculation illustrates the scale required:

| Share of today's main-thread work made perfectly parallel | Ideal total speedup | Outset from 39 FPS | Fortress from 34.7 FPS |
| --- | ---: | ---: | ---: |
| 25% | 1.23× | 48 FPS | 43 FPS |
| 50% | 1.60× | 62 FPS | 56 FPS |

These are mathematical upper bounds, not predicted results. Queueing, copying,
imbalanced jobs, synchronization and competition with the existing graphics
worker reduce the gain. At four cores, parallelism alone would require about
47% of current Outset work or 56% of Fortress work to be parallelizable even
with zero overhead. A combination of faster native routines, caching and
parallel batches is the stronger plan. Model matrices are a sensible first
prototype; the current profile does not establish that matrices alone are
large enough to reach 60.

## Correctness and acceptance

Collision and native floating-point changes must preserve contacts, jumps,
movement, actor interactions and interrupt/device timing. Compare recorded
inputs and game state against the original implementations, then verify the
rendered Outset and Fortress routes. Native-float implementations may differ
from the console's paired-single rounding; this needs an explicit compatibility
boundary and fallback, not global unsafe math flags.

Performance is separate from the remaining 60 Hz timing conversion. Running
onset, ladders, combat, sailing, ropes, moving platforms and actor-specific
timers still need validation or fixes. A faster incorrect simulation is not
finished 60 Hz support.

A separate Fortress repeat, using the same host in a temporary app bundle for
window inspection, crashed at retrace 1599 before reaching the measurement
window. It is excluded from the table. Its macOS crash report identifies the
existing graphics worker in `dawn::native::metal::BindGroupLayout::AllocateBindGroup`,
called from `aurora::gfx::gxcore::submit_draw_plan`, with an invalid mutex
address (`0x180`). Repeated texture uploads appeared immediately beforehand.
The trigger has not been isolated; this is not evidence that the proposed job
system fails, since it has not been implemented. Investigate resource lifetime
and reproduce this failure before expanding renderer concurrency. The first
Fortress run and all three Outset runs completed normally through retrace 2700.

Local evidence is in ignored `build/profile-60hz/`: per-run launch settings,
frame/performance logs, native samples, the Fortress guest-PC capture, and
analysis scripts. Native sample PIDs were checked against the actual game
executable, not its launcher or `/usr/bin/time`. No optimization or gameplay
source changes were made during this profiling pass, and no private build was
published or committed.

## Implementation checkpoint, later September 29

The initial game/host changes are pushed at `8435ec7` on
`elliotttate/Wind-Waker-Recomp:codex/native-60hz`. The separate shared Mac
checkout was preserved and snapshotted at `78d389e` on
`codex/mac-source-checkpoint-20260929`. The renderer's existing local changes
were preserved at `fbb5943` in `elliotttate/RecompCore`, followed by `f7154b5`
which serializes its texture-layout cache access across the pipeline compiler
and FIFO worker. The builder now pins that reproducible source commit.

The cache had a concrete concurrent read/insertion race in an
`absl::flat_hash_map`. The fix also ties its entries to the owning GPU device.
This removes that race; it does not prove that it was the cause of the earlier
Fortress crash. Two short Fortress runs completed; longer stress testing is
separate from performance acceptance.

The mouse-camera adapter now checks its two entry addresses inline before
making an out-of-line call. A new test-only input switch prevents physical
controller or mouse activity from changing a rendered benchmark's camera or
route. Earlier runs with different draw counts or a live-input takeover are
excluded from optimization comparisons.

Three native SDK matrix leaves are available behind `BLUEWAKE_NATIVE_MATH=1`
and remain off by default. They validate bounded inputs and RAM ranges, retain
paired-single rounding, full register results, reservations, stack stores and
guest cycle charges, and use the original code near device deadlines or for
unsupported inputs. Source-body certification covers all generated variants;
CMake rejects stale certification. The test compared **12,000 full CPU states
and RAM results** with an unoptimized personal module, including in-place
products, signed zeros, retained NaN lanes and deadline boundaries.

An isolated microbenchmark measured approximately 50/12 ns for matrix copy,
173/22 ns for concatenation and 75/15 ns for vector transformation
(original/native). These are kernel timings, not frame-rate results.

| Paired live test | Native math off | Native math on | Interpretation |
| --- | ---: | ---: | --- |
| Outset, first implementation | 39.61 FPS | 38.76 FPS | No reliable frame-rate gain; total process instructions fell about 2.5%, CPU cycles were essentially unchanged. |
| Fortress, revised RAM/validation path | 34.55 FPS | 35.88 FPS | Small measured improvement in this pair; total process instructions fell about 2.0%, cycles about 1.4%. Requires repetition. |

Each pair used the same host/module, fixed test input and matching rendered
draw counts. All 48 Outset player-state records and all 52 Fortress records
matched within their respective pairs. The Fortress pair still reports 60
physics/player updates per 60 retraces, while taking longer than one second
of wall time to execute them. Desktop activity and thermal variation remain
limitations of FPS comparisons. Whole-process counters include the graphics
worker and startup, rather than isolating just the simulation window.

The direct cycle-domain, simulation-timing and native-math tests pass, as do
source-preparation drift/variant tests. CMake accepts the real native manifest
and rejects a modified one. This local build configures `BUILD_TESTING=OFF`,
so these results are from executing the test binaries directly, not CTest.
Steady real-time 60 FPS and full-game timing compatibility remain unachieved.
