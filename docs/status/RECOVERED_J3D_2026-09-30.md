# Recovered game code in the Windows module

The practical first step is to replace bounded hot functions with recovered
source compiled as native C, retaining translated code for the rest of the
game. This change implements both `J3DGetTranslateRotateMtx` overloads from
`E:\Github\tww`:

| Entry | Recovered function | Guest cycles |
| --- | --- | --- |
| `0x802DA64C` | transform information to rotation/translation matrix | 54 |
| `0x802DA724` | rotation angles and translation arguments to matrix | 48 |

The formulas come from zeldaret/tww revision
`09de0609ecdb6d30dd012e2258f755afdac1cb56`,
`src/JSystem/J3DGraphBase/J3DTransform.cpp` (CC0 1.0). Attribution is recorded in
`RIGHTS_AND_LICENSES.md`. The implementation is
`cmake/composite/native_j3d.c`; compiling these formulas requires no C++ game
object model or external tww checkout.

## What is preserved

The boundary still uses guest addresses and the game's sine/cosine tables.
The native reproduces output bytes, all scratch GPRs, both halves of the FPRs,
FPSCR, XER, reservations, return PC, cycle charges and the last memory access's
cycle suffix. Each single-precision arithmetic operation retains its rounding
boundary, including NI handling; floating-point contraction stays disabled.

Unsupported state, memory, floating-point rounding modes, table values, a
write journal, overlapping transform input/output, or an observable deadline
inside the routine make it decline before mutating state. The original
translated routine then runs.

`scripts/mods/prepare_native_j3d.py` verifies both original function bodies
before installing entry hooks in chunk `802D96E0`, including mod variants.
This catches direct and intra-chunk calls as well as dispatcher entry. It is
idempotent and validates every variant before writing any file. Its manifest
records whole-file hashes; CMake refuses a subsequently changed chunk.

The Windows builder runs this step after `fast_blocks.py`, before simulation
and native-math certification. It includes the native source and preparation
script in the PGO fingerprint. The native is enabled with the existing
certified native-math path. `BLUEWAKE_NATIVE_J3D=0` disables it independently;
shutdown logs report native/declined calls for each overload. Other builders
compile the portable helper but do not enable routing automatically in this
change; ARM and those source preparation paths need their own validation.

## Verification

On Windows 11, i9-13900KF, clang 22.1.3, x86-64-v3 and `-ffp-contract=off`:

- 100,000 randomized cases: 36,716 identical native transform-info calls and
  37,883 identical native transform-angles calls. The remaining 25,401 cases
  declined without changing CPU state or the RAM test area.
- Every accepted case also ran through a separately linked module with its
  entry hooks enabled: all 74,599 calls matched the untouched original module.
- Cases cover random registers and translation bit patterns, denormals,
  signed zero, out-of-range/non-finite table data, angle shifts, aliasing,
  unaligned output, reservation clearing, NI, budgets, deadlines, FP-disabled
  and exception state, and write journals.
- Three preparation tests passed (idempotence/variants, changed bodies,
  altered hooks/missing entries). CMake configured with the certified source
  and rejected a deliberately altered chunk. Existing native-math, native-GPR
  and simulation source certification tests also passed.
- A 1,500-retrace headless run from the local Outset save stopped normally:
  190,071 native calls and 908 translated fallbacks. This checks runtime
  integration; it is not a graphics or FPS measurement.

Build and rerun the differential test from an x64 Visual Studio developer
environment (with the same runtime ABI as the original module):

```powershell
clang -O2 -march=x86-64-v3 -ffp-contract=off -Icmake/composite -Iref/recompcore/GXRuntime/include -Iref/recompcore/Source/Core/Core/PowerPC/StaticRecomp tests/native_j3d_test.c cmake/composite/native_j3d.c build/windows/app/gxruntime_build/gxruntime.lib -o build/windows/j3d-audit/native_j3d_test.exe
build/windows/j3d-audit/native_j3d_test.exe ORIGINAL.dll 100000 1000000 ROUTED.dll
python tests/native_j3d_source_test.py
```

The local experiment is `build/windows/j3d-audit/gGZLE01_recomp.dll`. It reuses
the existing module's unchanged objects and recompiles only the hooked chunk,
module entry and native helper, keeping the baseline module and Claude's
builds untouched. Normal builds use the integrated Windows builder.

An isolated million-call benchmark with ordinary sine/cosine values measured
87.1 versus 27.8 ns/call for transform-info (3.14x), and 123.5 versus 27.4
ns/call for transform-angles (4.50x). The translated measurement includes
module dispatch; these are leaf-call results, not whole-game speedups. Claude's
Outset profile attributed about 0.35% of game-thread samples to transform-info,
so this replacement alone is expected to save only a fraction of a percent
there. The tester's 25 FPS issue is not established as fixed.

## Next source conversions

The hand decompilation makes the algorithms available, but its GameCube
object layouts, 32-bit pointers, SDK calls, globals and REL loading are not
directly compatible with the host's 64-bit runtime. Its matching percentage
does not mean that the same percentage can be dropped into a host build.
Small recovered routines can be expressed in C without porting all those
dependencies. Larger conversions should follow measured hotspots and retain
exact guest behavior at their boundaries; a complete native source port is
a separate architecture project.

Work was coordinated with the active Claude session: this change owns both
J3D matrix overloads; Claude owns renderer optimization and its separate
`codex/game-natives` worker owns other math, animation and collision targets.
