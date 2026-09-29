# Native 60 Hz on Windows — 2026-09-29

Native 60 Hz (`BLUEWAKE_SIMULATION_60HZ=1`, Smooth Motion off) runs at 60
updates a second on Outset on the Windows build (Core i9-13900KF, GeForce
RTX 5090, x86-64-v3, local PGO). This records what got it there, how
each change is kept exact, and how to measure it.

## Measurements

Unpaced (`BLUEWAKE_WALL_PACE=0`), the Outset load route (Link standing after
loading) unless marked *running*: the same route with the stick held from
retrace 900 (up the pier, off it, swimming, turning). Windowed runs present
through Aurora and top out at the display's 60 Hz; headless runs
(`BLUEWAKE_RENDERER=headless`) measure the game thread alone.

| Build | Change | Updates/s |
| --- | --- | ---: |
| baseline | merged native 60 Hz work, batch 2 | 50.5 |
| resolver cache | host graphics resolutions cached | 54.9 |
| edge filter, indirect calls | `direct_calls.py`, `dispatch_loop.h` | 55.8–56.1 |
| gather-pipe batching, fallback continuation | `gather_pipe*.h`, RecompCore 0112 | 56.6–56.9 |
| natives | `native_skin.c`, `native_vec.c`, native-first matrix calls | 57.0 (worker-bound) |
| worker | GX core register version, resolver for every size, batch buffer kept (RecompCore 0113) | 59.7–60.0 |
| *running*, windowed | same | 60 median, slowest seconds 57.5–59.4 |
| *running*, headless | same | 63.2 |
| *running*, headless | prepaid block copies (`fast_blocks.py`) | 66.0 (slowest 62.6) |
| *running*, headless | PGO training with Link running | 66.3 (no measurable change) |

At the worker step the game thread had overtaken the render worker: drain
waits at draw-done rose to 48–71 ms a second (from 5–29) and the game thread
idled; the worker fixes took them to 18–38.

## Changes and how each stays exact

- **Edge filter and direct calls** (`scripts/windows/direct_calls.py`,
  `cmake/composite/direct_calls.h`): a boundary skips the host's edge service
  only when the host is quiet and does not watch the address; a direct or
  indirect call, or an interpreted instruction's continuation, only under the
  same test (`bw_direct_call_ready`). Every continuation target sets
  `cycle_block_prepaid` before reading it, as a dispatch entry does.
- **Gather-pipe batching** (`cmake/composite/gather_pipe*.h`): pipe stores
  collect in 256 bytes and reach Aurora in one call; any other hardware
  access, an interpreted instruction, a quantised paired single to the
  hardware, and every edge-service call or return to the host hands the batch
  over first. Only where the backend takes the FIFO as a byte stream (GX core,
  no trace). `BLUEWAKE_GATHER_PIPE_BATCH=0` turns it off.
- **Natives** (`cmake/composite/native_skin.c`, `native_vec.c`): each declines
  unless its result is certain, and runs only where the translated body is the
  one it was compared against (hashes in `native_skin.py`, `direct_calls.py`).
  `tests/native_skin_test.c`, `tests/native_vec_test.c` compare every byte of
  the CPU state and RAM against the personal module's translation.
- **Prepaid block copies** (`scripts/windows/fast_blocks.py`): each block gets
  a copy entered once it has charged all its cycles, without the per-instruction
  prepaid tests, suffix selects and the pc stores of instructions that call
  nothing; refunds continue in the original. `tests/fast_blocks_test.c` runs
  the same functions through a module without and one with the copies, with
  deadlines inside the functions and small turn budgets: 30,000 cases
  identical. The certified SDK leaves keep their blocks.
- **Render worker** (RecompCore 0113, `main.c`): the GX core's derived-pipeline
  cache compares a register version instead of copying 2 KB of registers per
  draw; the graphics resolver answers every size for an address no alias holds.
- **PGO training** (`build.py`): after player control Link runs (and rolls,
  and runs into the lookout's railings) instead of standing, so movement,
  collision and animation code is trained hot. No measurable change on the
  Outset running route, which runs elsewhere; kept because play is movement.
- **Viewport** (RecompCore 0114, from the native 60 Hz line): the retail XF
  viewport origin decoded with its 342 bias
  ([VIEWPORT_OFFSET_2026-09-29.md](VIEWPORT_OFFSET_2026-09-29.md)).

## Measuring

The scripts used live outside the repo; the essentials:

- Windowed measurements need the display on. With the display asleep or the
  session locked, Windows throttles presents to about 12.8 fps and every
  windowed run reads 12.8 regardless of the build.
- A build's training playback also runs the game; measure after the builder
  finishes.
- Measure with Link moving, not only standing: the pad script's stick form is
  `retrace:buttons:length:stick_x:stick_y`.

## Next

Still the game thread when running: collision (`GroundCrossGrpRp`,
`cM3d_Cross_MinMaxBoxLine` about 2.6% each, the `WallCorrect` and
`GroundCross` family), the host's native actor search (2%), J3D animation.
GPU vertices (`DOL_GXCORE_GPU_VERTICES=1`) are untested since the worker
changes.
