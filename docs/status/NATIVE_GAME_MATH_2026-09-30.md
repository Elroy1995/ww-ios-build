# Native game math validation

Twelve game entry points have certified native paths in `cmake/composite/native_game_math.c/.h`. Each retained entry passed **100,000 randomized cases with zero mismatches**, including at least 40,677 accepted comparisons and unchanged-state checks on every decline. Two entries intentionally cover only an early/simple subset of the function. Unsupported inputs continue through the original translation.

No game executable, Windows build/package script, or release was run. The main checkout, its build artifacts, and the decompilation were read-only. The two `J3DGetTranslateRotateMtx` overloads at **0x802DA64C and 0x802DA724 were left to the other session's `native_j3d` work**. None of its reserved files was created or edited here.

## Retained entries and measured results

Each row has 100,000 cases and zero mismatches. Accepted cases compare the complete CPU state and RAM against the personal game DLL; declined cases compare against the untouched input. Times are median nanoseconds per call from five alternating rounds of 200,000 calls on this machine, including identical CPU-reset overhead on both paths.

| Entry | Function/path | Accepted | Declined unchanged | Translation ns | Native ns | Speedup |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 0x80245674 | cXyz addition | 50,407 | 49,593 | 68.66 | 48.58 | 1.41x |
| 0x802456C4 | cXyz subtraction | 50,404 | 49,596 | 73.11 | 48.58 | 1.51x |
| 0x80245714 | cXyz scalar multiplication | 50,402 | 49,598 | 65.19 | 43.53 | 1.50x |
| 0x8024A8E0 | cM3d_Cross_AabCyl | 69,370 | 30,630 | 60.26 | 47.29 | 1.27x |
| 0x8000CD28 | mDoMtx_XrotS | 66,254 | 33,746 | 57.42 | 43.84 | 1.31x |
| 0x8000CDC8 | mDoMtx_YrotS | 66,255 | 33,745 | 57.39 | 42.85 | 1.34x |
| 0x8000CE68 | mDoMtx_ZrotS | 66,243 | 33,757 | 58.68 | 45.29 | 1.30x |
| 0x8024AE3C | cM3d_Cross_MinMaxBoxLine, early returns | 53,070 | 46,930 | 212.32 | 101.04 | 2.10x |
| 0x80256888 | J3DUClipper sphere clip | 59,516 | 40,484 | 167.10 | 118.43 | 1.41x |
| 0x802569D0 | J3DUClipper box clip | 54,952 | 45,048 | 453.84 | 244.48 | 1.86x |
| 0x802F072C | J3DGetKeyFrameInterpolationS, bounded tables | 40,677 | 59,323 | 202.91 | 158.23 | 1.28x |
| 0x802F0954 | J3DAnmTransformKey::calcTransform, zero/one-key channels | 50,038 | 49,962 | 171.99 | 76.00 | 2.26x |

Totals for retained entries: **1,200,000 cases, 677,588 accepted, 522,412 unchanged declines, zero mismatches**. An additional 100,000 cases verify that the dropped component-wise cXyz multiplication entry (0x80245760) is unsupported and declines unchanged.

The box-line native reproduces slab rejects and endpoint-inside returns before 0x8024B0D0. It evaluates the prefix on a private CPU copy and commits the original prologue's stack bytes only on success. Paths reaching bevel/plane work decline without changing CPU or RAM.

The transform native accepts only channels whose key count is zero or one, across all nine scale/rotation/translation channels. Any multi-key channel declines the entire function before its first store. The separate signed-short interpolation native implements binary search and the Hermite helper for strictly increasing key times, at most 256 keys, and the expected signed-short GQR5 mode and conversion constant. Invalid counts, modes, time ordering, and unsafe ranges decline.

## Exactness and scope

The Windows translation is the operational reference. Arithmetic uses `inline_fp.h`, including the interpreter's multiplier precision, single rounding, NI behavior, paired-single halves, comparison flags, and the last arithmetic's FPRF. The larger natives reproduce nested SDK/helper effects directly, including scratch registers, paired spill/restore effects, stack stores, LR/CTR/CR/XER, reservations, basic-block cycle charges, and the last access's observation suffix. They do not call a nested native that could decline after an earlier store.

Preflight requires ordinary aligned RAM without overlapping guest aliases or a write journal, disjoint output/input/stack ranges where needed, the expected FP/GQR settings, nearest rounding, and enough turn/deadline budget for the complete accepted path. Float inputs are conservatively bounded: zero or normal single values with magnitude in [2^-60, 2^30); applicable double operands are also bounded. Unsafe, nonfinite, denormal, huge, overlapping, or budget-limited inputs decline. Rotation setters read the guest sin/cos tables and guest constants; they do not use host trigonometric functions.

The comparison harness randomizes every GPR, both halves of every FPR, CR/XER/LR/CTR/FPSCR, reservation state, and guest data. Cases cover signed zeros, normal values, denormals, huge values, NaNs/infinities, non-single double operands, aliasing, unaligned/non-RAM/mirror/end-of-RAM addresses, write journaling, guest aliases, FP/GQR settings, and expired or interior turn/deadline limits. Key tables include 1..256 keys and invalid layouts. All 24 MiB start identical; pages outside the randomized writable region are read-only. Every writable byte is compared per case and the entire RAM image after each entry, so unexpected writes outside the region fail immediately. The DLL's existing natives are explicitly disabled before loading it.

`scripts/windows/native_game_math.py` checks **19 translated-fragment SHA256 certificates** before adding any of the 12 entry hooks. These include the subtraction's two chunk fragments, all emulated SDK/helper dependencies, and the box clipper's extracted zeroing loop. Every present base/mod variant must match. Certification also rejects host-watched internal PCs. Whole-body changes disable even a partial native. Only whitespace and known `fast_blocks.py` scaffolding are excluded from the hashes. Changed or missing dependencies remove an obsolete hook on rerun.

## Targets left translated

| Target | Reason no native was retained |
| --- | --- |
| cXyz component-wise multiply, 0x80245760 | A candidate compared exactly, but repeated benchmarks ranged roughly 0.87x..1.14x with no dependable gain. The implementation and hook were removed. |
| cXyz division/cross/normalization siblings | Inspected; reciprocal/cross/normalization and nested effects were not certified in a retained implementation. |
| cSPolar::Val, 0x80254214 | Its sqrt/frsqrte refinement, nested atan table routines, angle constructors/formalization, and float-to-integer state require a larger composite proof. Those effects were not certified; no native was implemented. |
| mDoMtx X/Y/ZrotM | Their nested PSMTXConcat adds a separate spill frame, f14/f15/f31 state, and absolute constants. These composite effects were not certified atomically; only the setters were retained. |
| cM3d_CalcPla, 0x8024A6F0 | Subtraction/cross/magnitude/reciprocal/scale/dot calls can encounter degenerate or quantized intermediate results after earlier stack/output stores. Reusing a later leaf that can decline would violate atomic fallback. No complete preflight and composite implementation was retained. |
| Remaining box-line paths | Bevel/plane paths were not certified; the entry native declines at the prefix boundary. |
| Remaining calcTransform paths | Multi-key float and short interpolation combinations were not certified as a whole caller; entry preflight rejects them. |
| GroundCrossGrpRp/GroundCrossRp/ChkGrpThrough/MakeBlckMinMax/WallCorrect* | Stretch collision work was reviewed but not implemented. Recursive scene traversal, data-dependent writes, virtual calls, and internal deadlines still need separate certification. |
| Both J3DGetTranslateRotateMtx overloads | Explicitly reserved for the other session. |

These deferred paths are not claimed to be impossible to optimize. They remain translated because this change does not establish their required exactness.

## Reproduction commands and provenance

Run from `E:\Github\Wind-Waker-Recomp-natives`. The following is the exact native-test compilation command used from PowerShell (the VS tools setup runs in its child shell):

```powershell
New-Item -ItemType Directory -Force .native-study
cmd /d /c '"E:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && "E:\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang.exe" -O2 -march=x86-64-v3 -ffp-contract=off -Icmake/composite -IE:/Github/Wind-Waker-Recomp/ref/recompcore/GXRuntime/include -IE:/Github/Wind-Waker-Recomp/ref/recompcore/Source/Core/Core/PowerPC/StaticRecomp tests/native_game_math_test.c cmake/composite/native_game_math.c E:/Github/Wind-Waker-Recomp/build/windows/app/gxruntime_build/gxruntime.lib -o .native-study/native_game_math_test.exe'
.native-study/native_game_math_test.exe E:/Github/Wind-Waker-Recomp/build/windows/BlueWake-test/gGZLE01_recomp.dll 100000 > .native-study/final_results.txt
```

Result: exit 0; all counts and benchmark medians are in the table above. The same standalone recipe is recorded at the top of `tests/native_game_math_test.c`. This loads the personal game module, never the application executable.

The source/hook test used the installed embedded Python because the user's Python installation was not executable in this sandbox:

```powershell
& E:/ComfyUI-MiniMax-H3/ComfyUI_windows_portable/python_embeded/python.exe tests/native_game_math_source_test.py --reference E:/Github/Wind-Waker-Recomp/build/windows/composite-src --copy .native-study/hooks --diff tests/native_game_math_hooks.diff
& E:/ComfyUI-MiniMax-H3/ComfyUI_windows_portable/python_embeded/python.exe scripts/windows/native_game_math.py .native-study/hooks
```

Results: `19 fragment certificates, 12 hooks, build order, idempotence and all rejection checks passed`; CLI rerun: `native game math: 12/12 certified entries, 0 new hooks in 0 chunks`. The test modifies only worktree copies. It exercises the actual `fast_blocks.transform` after inserting hooks, then certifies the resulting source again. [The recorded diff](../../tests/native_game_math_hooks.diff) shows every hook insertion in the six affected chunks.

All six copied, hooked chunks also passed this syntax check (exit 0):

```powershell
cmd /d /c '"E:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && "E:\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang.exe" -O2 -march=x86-64-v3 -ffp-contract=off -DDOLRECOMP_CPU_HEADER=\"core/cpu.h\" -Icmake/composite -IE:/Github/Wind-Waker-Recomp/build/windows/composite-src -IE:/Github/Wind-Waker-Recomp/ref/recompcore/GXRuntime/include -fsyntax-only .native-study/hooks/chunks_dol/chunk_0002_text1_800096E0.c .native-study/hooks/chunks_dol/chunk_0144_text1_802416E0.c .native-study/hooks/chunks_dol/chunk_0145_text1_802456E0.c .native-study/hooks/chunks_dol/chunk_0146_text1_802496E0.c .native-study/hooks/chunks_dol/chunk_0149_text1_802556E0.c .native-study/hooks/chunks_dol/chunk_0187_text1_802ED6E0.c'
```

Python source parsing and `git diff --check` also passed. Full application compilation and gameplay/FPS measurement were deliberately not performed under the task's restrictions. Scratch generators, copied chunks, and test binaries were removed before the final documentation commit; the committed diff contains only hook additions, not copied chunk bodies.

| Reference | Revision / SHA256 |
| --- | --- |
| BlueWake base commit | `5ca26dd293f4447d81177dfd4db22dbcf7ed216c` |
| tww decompilation | `09de0609ecdb6d30dd012e2258f755afdac1cb56` |
| RecompCore | `825f103bb13b5eb232492f2437b2eefdf670f2a1` |
| DolRecomp profile pin | `b8b534591cba8ca7cd43943a655ee6e2591cf5de` |
| Composite profile digest | `54f54434c3f9c899d43a96373dc0b4c1aed0e50db8b820b9698dfa76571a770a` |
| Personal comparison DLL | `BABE50D2C74398156A71BC37773BA26CD1894B60A49629D0865D0C9EFF845D4E` |
| gxruntime.lib | `1CE82297FEC63EA9372E8223FC49C395082564DF48F2D719093B9F7F29724154` |

Compiler: Visual Studio's clang 22.1.3, revision `e9846648fd6183ee6d8cbdb4502213fcf902a211`, x86_64 Windows MSVC target. The test module/library remain in the user's main checkout and are not included in the deliverable.

## Expected game-thread benefit

For a function with sample share `s`, eligible-call fraction `p`, and measured translation/native times `T/N`, the estimate is `s * p * (1 - N/T)` percentage points of game-thread time. The provided shares and these benchmark paths give about **0.81 percentage points** from covered cXyz arithmetic, AabCyl, YrotS, both clippers, and short-key interpolation if their calls are eligible. Box-line contributes about **0.66 * p_box** and simple transform about **0.32 * p_transform** additional points. With every covered call eligible, the indicative total is about **1.87 percentage points**; no separate share was provided for XrotS/ZrotS. This uses 0.51% for the listed cXyz group and the addition timing as its representative cost.

The random test's acceptance rates are deliberately affected by adversarial cases and are not gameplay eligibility estimates. These microbenchmarks exercise one accepted path per entry and omit the incremental cost of the generated enable/entry check. Partial-path frequency, actual branch costs, declines, and the other half of the critical path can reduce the benefit. No measured FPS improvement or guaranteed saving is claimed. Native/declined counts are reported per entry at exit when the existing native opt-in is enabled; they can establish eligibility during later authorized gameplay profiling.

## Integration order and affected compilation units

The changes to `CMakeLists.txt`, `module_export.c`, and `scripts/windows/build.py` are separate commented additions. Existing lines were not removed or reordered. `RIGHTS_AND_LICENSES.md` was not edited, allowing the other session's small additions to merge independently.

After importing these commits, the builder's source preparation order is:

1. `scripts/windows/global_guest_cpu.py`
2. `scripts/windows/inline_save_restore_gpr.py`
3. `scripts/windows/chunk_headers.py`
4. `scripts/windows/direct_calls.py`
5. `scripts/windows/native_skin.py`
6. **`scripts/windows/native_game_math.py COMPOSITE_SRC`**
7. `scripts/windows/fast_blocks.py`
8. `scripts/mods/prepare_simulation_60hz.py`
9. `scripts/mods/prepare_native_math.py` (whole-chunk manifest remains last among these steps)

`build.py` contains the new source step and its log output, and includes the new C/header/script in its PGO cache key. Use the builder's actual `COMPOSITE_SRC` root; run the game-math step after direct calls and before fast-block copies and whole-chunk manifests. The other session's J3D preparation should remain at its chosen compatible point; this hook does not alter either of its function bodies.

`cmake/composite/CMakeLists.txt` adds `native_game_math.c`. `module_export.c` enables the entry hooks and exit report with the existing `BLUEWAKE_NATIVE_MATH` switch, which the Windows app sets to 1 by default (`windows/src/win_entry.c`), so they are on in the Windows release. No new command-line option or game setting is introduced.

The six DOL chunks changed by the entry hooks and therefore needing recompilation are:

- `chunk_0002_text1_800096E0.c`: X/Y/Z rotation setters.
- `chunk_0144_text1_802416E0.c`: cXyz addition/subtraction entry.
- `chunk_0145_text1_802456E0.c`: cXyz scaling entry; also certifies subtraction tail.
- `chunk_0146_text1_802496E0.c`: AabCyl and box-line.
- `chunk_0149_text1_802556E0.c`: sphere and box clippers.
- `chunk_0187_text1_802ED6E0.c`: short-key interpolation and simple transform.

Matching mod variants, if any, are certified and hooked too. `chunk_0195_text1_8030D6E0.c` is an SDK certificate dependency and receives no hook/change from this script. The new native source and `module_export.c` also compile; generated source should be prepared normally before compilation. This report supplies integration instructions without running the prohibited build script.

## In the Windows 0.1.1 build (2026-09-30)

Merged with the recovered J3D matrices and built by `scripts/windows/build.py` from 1e97dca:

- The builder finishes the source twice (in the mods step, then at "the last source steps"). The first
  pass certified 10 of 12 entries: the `xyz_sub_entry` fragment (chunk 0144) and `clip_zero_loop` (chunk
  0149) did not match before `fast_blocks.py`, their certificates having been taken from a finished tree.
  The second pass, on the finished chunks, certified all 12 and added those two hooks, the form the hook
  tests used. Every build ends the same way; certifying the two fragments in their earlier form as well
  would let the first pass hook them too.
- A headless run of the Outset load route with Link running (2,900 retraces), native/declined:
  cXyz add 573,835/1,521, sub 2,081,050/5,370, scale 332,538/770; AabCyl 3,606,614/9,917; XrotS
  595,549/1,179, YrotS 1,229,723/2,508, ZrotS 116,213/240; box-line 1,290,066/30,066; sphere clip
  1,342,649/18,991; box clip 80,457/12,794; key interpolation 135,971/508,431; simple transform
  102,371/363,515. The J3D matrices: 488,187/2,275. The module's exit reports reach stderr only with
  `BLUEWAKE_SESSION_LOG=0` (the session log is closed before they run).
- The frames are 0.1.0's byte for byte on the Outset route (19 captures with Smooth Motion off, one of them
  taken two retraces later with the same image; 62 real and in-between frames with it on).
- On the i9's efficiency cores, unpaced with Smooth Motion off at Outset's spawn view: 35.0 game frames a
  second, against 34.4 with 0.1.1's renderer changes alone and 29.7 for 0.1.0, within this report's
  estimate of about 2 percent of the game thread.

## Commits

The worktree's Git index was outside the sandbox's writable root, so the three commits were made in a
temporary repository and delivered as a bundle; they have since been imported onto the local branch
`game-natives` (based on 5ca26dd), and the bundle removed:

1. `Add bit-exact native game math paths and comparison harness`
2. `Gate native game math entries on verified translations`
3. `Document native validation results and integration`

They contain the source, tests and this report: no game module, copied chunks, assets, binaries or scratch
generators. Nothing was pushed.
