# Experimental 60 Hz gameplay

The Display menu has a **60 Hz gameplay (experimental)** option. It is off by
default and takes effect at the next launch. Smooth Motion is disabled while
this mode is running; its saved preference is retained for a normal launch.

This is an initial gameplay timing conversion, not a completed full-game
60 Hz compatibility patch. Dialogues, cutscenes, pause menus, fades and scene
transitions keep the original clock. Combat, sailing, ropes, moving platforms,
vegetation and actor-specific timers still need individual testing and fixes.
The comparison also exposes remaining running-start and ladder timing
differences. Use a separate memory card while testing.

## Personal builds

The builder prepares the timing sites after generating the mod variants.
Rebuild the personal game module and the host from this checkout. An older
module leaves the setting unavailable; explicitly requesting it through the
environment fails with a rebuild message instead of silently doing nothing.
No translated game source or personal game binary belongs in Git or a release.

For an existing C translation, the equivalent preparation step is:

```sh
python3 scripts/mods/prepare_simulation_60hz.py build/device/composite-src
```

Then configure and rebuild `cmake/composite` normally. The script validates every
instruction before changing any file, covers matching mod variants, and can be
rerun safely. The manifest records the changed files' hashes; CMake checks them
before advertising support through the module API. Adding/rebuilding mods after
this step requires preparing the timing sites again.

For the Mac test launcher, `SIMULATION=1` enables the mode. The underlying setting
is `BLUEWAKE_SIMULATION_60HZ=1`; `0` or an absent value retains the original game.
Saved Mac settings override launch variables, so use `BLUEWAKE_SETTINGS=none`
for a controlled comparison. `COMPOSITE` selects the rebuilt personal module.

## What changes

The original main loop continues to execute the player, actors, collision and
drawing together. During ordinary gameplay the display wait requests half the
original interval. There is no extra interpolated image between these updates.
The translated CPU gets twice the instruction budget per hardware-clock cycle
in this mode, avoiding a console-speed scheduling limit of 46–56 updates per
simulated second. VI, audio, DSP, timebase and deadlines retain their original
clock; fractional instruction cycles carry across dispatch boundaries. This
does not make the host CPU faster or guarantee real-time 60 FPS.

Timing adapters convert common actor movement and gravity, Link's separate
velocity integration and animation-derived speed, animation advancement and
crossing checks, shared approach functions, shared integer countdowns, the
authored frame counter, game-frame audio service, and particle integration.
Velocities remain in original game units; integration uses a half step.
Link's vertical integration preserves the original semi-implicit trajectory;
simply halving gravity and movement made the standing jump about 12% higher.
Particle emission bursts and integer timers retain their authored cadence.

Spatial collision corrections and animation-root displacements are not globally
halved. They are already distances. Treating every vector addition as movement
would halve collision pushback and apply the half step twice to root motion.

The timing module resets its phase on a state load and checks the gameplay gate
again at the next main-loop iteration. Native timing remains the default for all
generated instruction sites.

## Verification

`bluewake_simulation_timing_test` checks equal elapsed-time movement, 60 separate
integrations, 30 authored timer ticks, approach convergence, scene exclusions,
default behavior and state-load reset. The source preparation checks instruction
identity and rejects incomplete inputs before writing.

With `BLUEWAKE_SIMULATION_LOG=1`, `[simulation]` logs report per 60 retraces:

- `ticks`: main-loop iterations;
- `half`: iterations admitted to the half-step gameplay clock;
- `player`: Link velocity integrations;
- `scene`: actor collision passes.

These counters must be assessed alongside rendered frame timing and wall-clock
performance. A display counter by itself does not establish 60 simulation ticks
per second. Full-game acceptance also requires comparing movement, jumps,
attacks, invulnerability, timers, sailing, ropes, particles and scripted events
against the original mode, including demanding scenes such as Forsaken Fortress.

## Local results, 2026-09-29

Built and tested on an Apple M3 Max, from the isolated `true-60hz` checkout.
The private module contains 53 verified timing sites in 15 translated chunks.

| Check | Observed result |
| --- | --- |
| Original mode | All 201 player-state records through retrace 1500 matched the unmodified module exactly. |
| Gameplay cadence | Typically 60 main-loop iterations, Link integrations and actor collision passes per 60 retraces; original mode reports 30. Counters can differ by one at interval boundaries. |
| Actual performance | About 45–50 FPS headless; about 35–45 FPS rendered in the tested Outset views. The main game thread is CPU-bound. This is not sustained real-time 60 FPS. |
| Standing jump comparison | Original apex Y=170.1, revised mode Y=170.4, from Y=145.0. Both landed 19 retraces after the jump began in the neutral-stick comparison. |
| Movement comparison | Running onset still differs. The ladder route reached roughly Y=660 versus Y=600 at retrace 1200. This remains a timing regression to fix. |
| Display menu | The experimental checkbox is visible, the next-launch requirement is shown, and Smooth Motion is disabled while the mode runs. Saving retains the normal mode's interpolation preference. |
| Compatibility | Explicitly requesting the mode with an old module is rejected before boot. |
| Automated checks | Timing and clock-domain tests pass, including fractional CPU-cycle accounting, deadline conversion, jump trajectory, original defaults, scene gates and reset. Source preparation passes repeatability, variant coverage, drift rejection and FP guard ordering checks. CMake accepts the real manifest and rejects a stale one. |
| iOS | Changed shell sources pass syntax checks with the pinned runtime headers; no device build or device playtest was performed. |

The host and private game module were rebuilt locally. This work adds the
optional experimental path; full-game timing correctness and the performance
work needed for steady 60 FPS remain unfinished. The initial source was committed and pushed to `codex/native-60hz`; no personal
build was published. The existing development checkout's controls/settings work was
preserved as the starting point of this isolated checkout.

A subsequent controlled profiling pass measured approximately 39 FPS rendered
and 40 FPS headless in the same Outset view, and 35 FPS at the tested Fortress
spawn. See [the native 60 Hz profile](status/NATIVE_60HZ_PROFILE_2026-09-29.md)
for the measurements, CPU owners and proposed optimization order.

## Native math experiment

`BLUEWAKE_NATIVE_MATH=1` enables bounded native implementations of three SDK
matrix routines in a newly rebuilt, certified GZLE01 module. This developer
switch is separate from the gameplay-rate option and remains off by default.
It preserves the original paired-single rounding and register/memory results,
uses the original guest cycle charges, and falls back for exceptional inputs,
quantized memory, write observers or a nearby device deadline. It does not skip
physics, animation, drawing or alternate simulation ticks.

The builder runs `scripts/mods/prepare_native_math.py` before compiling. For a
manual composite build, run that script on the generated source folder first;
CMake verifies its hashes. Edited SDK bodies or variants must be reviewed and
recertified rather than silently running a replacement for different code.

Build `bluewake_native_math_test` and pass the path to an **unoptimized personal
module** for full CPU-state and memory comparisons. No disc contents or
translated bodies are included in the test. An optional second argument
`--bench` compares isolated leaf throughput; that result does not establish
whole-game performance.

For reproducible rendered routes, `BLUEWAKE_TEST_INPUT_ONLY=1` disables physical
controller, keyboard/mouse controls and the settings shortcut in that test
process. Scheduled test input still runs. Normal launches retain live input.
The Mac launcher accepts these developer flags through `EXTRA_ENV`, because it
starts the game with a clean environment.
