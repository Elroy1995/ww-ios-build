# True 60 Hz gameplay and rendering: feasibility investigation

Date: 2026-09-29. This is a source investigation and implementation proposal; no gameplay changes or new runtime performance measurements were made.

**Conclusion:** true 60 Hz appears technically feasible in this architecture, but requires a game-wide timing conversion and additional CPU headroom. The target is 60 fresh simulation states and rendered frames per second, with movement, collisions, animation, and gameplay evaluated every 16.67 ms while preserving real-time game speed. Interpolation's existing 60 FPS result does not establish either of those requirements.

## Sources actually inspected

- Outer BlueWake checkout: `31b8a722fee33457585df336093f70eea07f6382`.
- Current interpolation host's CMake configuration points at `build/mac-interp/bluewake`, revision `2c9f16c8aefbccbdc009d754bd453282b5034c9a`, with existing uncommitted changes. This is the implementation examined below, rather than the older outer checkout.
- Its RecompCore: `b4af1443578b0a34dc89a419f3c1a298ef553950`.
- Local decompilation reference: `/Users/briantate/Documents/GitHub/tww`, revision `1f9e8cb25913e7abdce0457ca0d2288cb8453aae`, upstream `zeldaret/tww`. Source paths beginning `src/` below refer to this reference. These functions identify owners and candidate hook locations; any actual patch must verify instructions and behavior against the supported personal GZLE01 build.
- Existing measurements: the nested checkout's `docs/status/CURRENT.md`, particularly lines 25–34 and 182–243. These are prior recorded tests, not tests rerun during this investigation.
- Primary external comparison: [Meowmaritus's unfinished Wind Waker 60 FPS hack](https://github.com/Meowmaritus/Wind-Waker-60FPS-Hack). Its author documents doubled script timing, Niko's failed platform demonstration, an Earth Temple stone-slime timer problem, and a Molgera entry softlock. These demonstrate incomplete timing coverage, not that a full conversion is impossible.

Existing dirty files in both BlueWake checkouts were left untouched. The only added file is this report.

## What the current loop does

1. `src/m_Do/m_Do_main.cpp:463` reads input, runs game-frame audio work, and calls `fapGm_Execute()`.
2. `src/f_ap/f_ap_game.cpp:78` calls process management and advances the global counter.
3. `src/f_pc/f_pc_manager.cpp:267` handles creation/deletion, executes processes, draws processes, and runs the scene/overlap/camera callback.
4. `src/d/d_s_play.cpp:1352` sets the display interval to `(OS_BUS_CLOCK / 4) / 30`. Opening/name scenes also select 30 Hz; logo/menu paths select 60 Hz. A mode must handle scene changes rather than modifying the initial interval only.
5. `src/JSystem/JFramework/JFWDisplay.cpp:248` waits at `beginRender`; `waitForTick` at line 347 uses the OS timebase or retrace queue.
6. RecompCore's `GXRuntime/graphics/aurora/lib/gfx/frame_interp.hpp:3` describes replaying each completed frame with blended transforms. The submission path in `lib/gfx/common.cpp:1456` chooses the extra interpolated presentation. It does not execute another game tick.

The host already has **60 video retraces per second** (`runtime/host/src/main.c:1121` in the nested checkout). Raising this to 120 is not the gameplay conversion. Game tick count, video retrace count, and frames presented must be measured separately.

**A particularly important coupling:** `src/d/d_s_play.cpp:286`, the scene's *draw* callback, runs collision processing, clears moving-background flags, advances vegetation, moves the background collision system, and calculates particles. These operations are not confined to actor `execute` methods. Any split between simulation and rendering must preserve their ordering and run the physical updates at 60 Hz.

## Required changes

| Area | Concrete evidence | Work needed |
| --- | --- | --- |
| Scheduling | Scene-specific `setTickRate`; `JFWDisplay::waitForTick` | An explicit 60 Hz simulation mode, correct scene pacing, pause/resume and catch-up behavior, fresh frame submission, and interpolation disabled for this mode. |
| Shared movement | `src/f_op/f_op_actor_mng.cpp:454`: gravity added to velocity; line 469: velocity added directly to position | Introduce timestep-aware integration, acceleration, rotation, terminal velocity handling, and movement contributions with clearly defined units. |
| Link | `src/d/actor/d_a_player_main.cpp:2463`: separate gravity and position integration; `posMove` at 2487; `execute` at 11177 | Convert Link's own movement, root motion, slopes, ice, jumps, swimming, ropes, ladders, knockback, and moving-platform attachment. Shared actor hooks alone miss this path. |
| Collision and combat | Scene draw calls collision `Move`; Link registers attack/target shapes and performs background correction in its own code | Update registration, resolution, hit flags and moving-platform transforms every tick. Preserve hit-once behavior, invulnerability duration, attack windows and event ordering. |
| Animation | `src/JSystem/J3DGraphAnimator/J3DAnimation.cpp:145` increments the animation frame by `mRate`; `checkPass` at 24 predicts using the same rate | Advance by the effective half-step and make frame-crossing tests use that same step. Audit morph/blend timers, root motion and animation-triggered sounds/attacks. |
| Timers and AI | `cLib_calcTimer` decrements integers; Link's damage timer decrements directly at line 5057; enemy files contain private counters and random decisions | Preserve durations while evaluating state every tick. Introduce fractional time/deadlines or explicitly controlled legacy counter boundaries; cover inline and direct counters. Audit random decision cadence and per-frame probabilities. |
| Cutscenes and events | `src/d/d_demo.cpp:632` declares 1/30-second frames; line 699 calls `forward(1)`; JStudio's forward API accepts an integer | Preserve the authored timeline, sequence waits, triggers and music synchronization. Full 60 Hz cutscene motion needs fractional timeline evaluation or an adapted sequence scheduler; passing `0.5` to the existing integer API cannot work. |
| Particles and environment | JPA particle age adds 1, position adds velocity, air resistance multiplies velocity; emission/child spawning use frame counters | Convert age, motion, damping and emission timing together; prevent repeated emissions when a fractional frame truncates to the same integer. Audit sea/waves, cloth/ropes, weather, vegetation and material animation. |
| Input, UI and audio | Main loop reads controllers and calls `mDoAud_Execute`, whose game-frame process and load timer run on each call | Sample input at 60 Hz with each edge consumed once; retime repeats, rumble, fades and game-frame audio automation. Keep audio sample production and playback tied to real time. |
| Existing mods | Sprint changes speed and animation rate; BetterWW patches sailing, braking, rolling, grapple timing and other values | Apply one coherent timestep conversion to the modified values. Test sprint, jump, mouse camera, fast transitions and BetterWW combinations for double scaling or timing conflicts. |

### Why a global “multiply everything by 0.5” is insufficient

A practical conversion can retain velocities in original game units and use a normalized step `h = 30 / 60 = 0.5` at integration sites: `v += acceleration * h`, then `position += v * h`. If instead storing velocity as distance per new tick, velocity constants halve and acceleration constants become one quarter. Mixing these conventions breaks jumps and falls.

Even the first convention changes the original semi-implicit integration trajectory: two half steps under constant acceleration produce a different position from one old full step. Jump height, airtime, ledge behavior and collision outcomes need explicit acceptance tolerances and possibly compatibility adjustments. Byte-for-byte 30 Hz state hashes are not a suitable universal oracle for a genuinely different physics step.

Other values need different treatment:

- For unclamped exponential approach with old coefficient `a`, the corresponding half-step coefficient is `1 - sqrt(1 - a)`. For multiplicative damping `d`, it is `sqrt(d)`. Clamps, angle rounding, minimum steps and fixed spatial tolerances require separate review.
- An already computed displacement from an animation or moving platform may already describe a half-step. Scaling it again makes movement too slow. Collision penetration corrections are not velocities and should not be halved indiscriminately.
- Doubling timer initializers can overflow small integer fields and misses authored data or later assignments. Half-step counters require correct expiration transitions and actor-lifetime handling.
- Enemy code sometimes casts animation time to an integer and compares it with a frame number. For example, `src/d/actor/d_a_mo2.cpp:1477` does this before a randomized sound. At half-frame increments the condition can become true twice. Frame-crossing or once-only event handling is necessary.
- A probability tested every new tick changes behavior even when the visible animation has the correct speed. Random-call ordering and decision rates need an explicit policy; exact RNG stream parity may not be compatible with the new simulation.

## How broad is the audit?

A narrow lexical census of the local decompilation found 441 actor `.cpp` files and 416 GZLE01 REL configuration directories. Among the actor files:

- 194 `cLib_calcTimer` matches across 54 files.
- 101 shared `fopAcM_posMove`, `posMoveF`, or `calcSpeed` matches across 72 files.
- 2,266 smoothing/chase helper matches across 167 files.
- 245 direct `current.pos` compound assignments across 61 files.

These are search matches, not a count of necessary patches or a complete semantic inventory. They establish that useful shared hooks exist, and that substantial actor-specific paths remain. Unnamed fields, custom vectors, inline helpers and authored data are missed by this census.

## Implementation route in BlueWake

The inspected application executes translated native code from the player's disc. Editing the reference decompilation alone does not change that application. Runtime writes to instruction memory also do not replace already translated machine code.

The newer checkout already has a suitable starting mechanism: `mods/betterww/options.txt` describes DOL/REL instruction sites and native hooks; `scripts/mods/game_options.py` resolves them for translation; `runtime/host/src/game_options.c` implements native behavior. A dedicated simulation mode can extend this mechanism with validated hook sites and regenerated local modules. Larger or hotter functions may merit native replacements, but a wholesale engine rewrite is not a prerequisite for the first prototype.

Required engineering around those hooks:

- Verify supported executable/version and original instructions; resolve REL sites through the existing module machinery.
- Give each hook a documented unit, owner, and execution frequency. Avoid global floating-point scaling or blanket gating of actors.
- Keep original 30 Hz behavior available. Initially choose the mode at startup, since changing timestep in the middle of a timer, jump, cutscene or loaded state needs an explicit conversion/reset policy.
- Track fractional state outside guest structures where changing layout would break translated code. Such state must follow actor creation/deletion, address reuse, scene transitions and save-state restoration.
- Extend patch composition: the current option parser rejects two sites at one address, so overlapping timing and BetterWW hooks need deliberate combined behavior.

### Preserve the real-time clocks

`runtime/host/src/main.c:1120` declares a 486 MHz guest CPU cycle domain; timebase conversion is 12 CPU cycles per timebase tick. VI and audio DMA consume that cycle domain (`main.c:5988`), and the wall pacer runs from retraces (`main.c:4700`). The cycle-budget/deadline machinery is in `runtime/host/src/cycle_domain.c`.

The prototype must measure both host execution time and charged guest work per new simulation tick. More translated work can exceed the modeled guest budget even on a fast host. If that is limiting, separate or rescale guest CPU execution throughput while preserving VI, timebase/decrementer, audio sample and interrupt timing coherently. Simply speeding up the shared clock or using the existing black-screen fast-forward path would not establish correct real-time 60 Hz gameplay. A virtual CPU throughput change also does not make the host execute code faster.

## Performance requirement

The nested checkout's September 29 notes report the Fortress exterior presenting at 60 FPS with interpolation while the emulation thread is 94% busy. Earlier recorded Smooth Motion tests report 81–84% in Outset. Those observations justify an early performance gate; they are not new benchmarks or proof of a particular 60 Hz ceiling.

True 60 Hz roughly doubles frequency-dependent actor work, skeletal evaluation, guest draw-list generation and GX translation. It removes interpolation matching/blending, and the renderer already performs up to 60 presentations. Therefore neither “twice the GPU cost” nor “free because we already render 60” is a sound estimate. Fixed-rate audio/device work also does not necessarily double.

Measure CPU simulation, translated draw generation, GX worker, render encoding, GPU and waits separately. Sustained tick and presentation deadlines must fit the 16.67 ms cadence with headroom; overlapping worker times should not simply be added. Profile fresh on the intended Mac, then iPad/iPhone with a thermal soak. Potential work includes better translated-code generation, targeted native hot functions, actor-search/collision improvements and render submission reuse; choose from profiles rather than repeating already-refuted compiler tweaks.

## Staged delivery and acceptance

1. **Measure the baseline.** Copy a known save; record input against elapsed simulation time. Log actual game ticks, collision passes and fresh GX frames separately from retraces and presentations. Capture movement, jump apex/airtime, timer durations, damage events, animation crossings and audio timing at 30 Hz. Profile Outset and Fortress.
2. **Build a bounded true-60 prototype.** Add a startup-only experimental mode, retime the limiter, shared movement/animation helpers and Link's independent paths. Use Outset locomotion, a jump, wall/ground contact, water entry and one combat actor. Disable renderer interpolation. Require fresh input, integration, collision and output on both half-steps. This milestone is a limited prototype, not full-game support.
3. **Finish shared subsystems.** Timers, hit handling, camera smoothing, moving backgrounds, particles, environmental simulation, audio game-frame work and sequence timing. Preserve the existing process order until evidence supports changing it. Prove sound and wall-clock speed stay correct.
4. **Audit actor and event coverage.** Cover enemies/bosses, projectiles, ropes, moving platforms, sailing, room changes, menus and mod combinations. Track every exceptional or unconverted path. Any remaining 30 Hz physics path means the full target remains incomplete.
5. **Validate progression and hardware.** Test the known Niko, Earth Temple and Molgera regressions, major bosses, ending, save/reload and a full progression route. Measure sustained 60 simulation updates and 60 fresh presentations, frame-time tails, audio stability and target-device thermals. Thirty-Hz fallback scenes must be reported explicitly.

At equal elapsed time, compare durations and gameplay outcomes exactly where meaningful, and trajectories with agreed tolerances. Add repeatable input/state checkpoints and manual checks for platforming, combat and camera feel. The original byte-identical route remains valuable for the unchanged 30 Hz mode.

**Recommended next milestone:** the instrumented Outset/combat prototype plus a fresh Fortress cost profile. It will establish whether the shared conversion strategy is viable and expose the first real CPU limit before committing to the full actor/event audit. Broad completion is a substantial engine-retiming project; a dependable schedule needs evidence from that prototype.
