# BlueWake on Windows

BlueWake also builds as a native Windows x86-64 program. As on the Mac, you build it yourself from your own disc:
the game's code is translated from that disc during the build, so **the folder you build is yours alone: never
share or upload it.**

The Windows build is the same static recompilation as the iOS app: the same translator, the same pinned runtime
(RecompCore, GXRuntime, Aurora) and the same generated game source, verified against the same digest. Only the
host around it is different: Direct3D 12 through Dawn instead of Metal, SDL3 input and audio, and a Windows entry
point in place of the iOS app shell.

## Status

Verified on 2026-09-28 on one PC (Intel i9-13900KF, 64 GB, NVIDIA RTX 5090, Windows 11), Visual Studio 2026's
clang 22, from a Redump-verified `.rvz`, at commit 79b7e16 plus the Windows port:

- The builder runs end to end from the `.rvz`: the generated game source has the verified digest (`54f54434`,
  the same as the macOS builder's), and the mods match the Mac's counts (widescreen 22 chunks, Better Wind
  Waker's options 15, 40 with 16:10 and the options together).
- Boot to control on Outset through the title, file creation and the opening, headless and in the window
  (Direct3D 12), with the scripted route the Mac builder trains on; saves are written to the memory card.
- The keyboard reaches the game through SDL (key messages posted to the window read as A at the title).
- Widescreen 16:9 renders 1280x720 with the HUD at the edges; Better Wind Waker's options load (instant text
  patched 4,411 messages, as on the Mac); Smooth Motion draws the in-between frames.
- Speed, with the game paced in real time: full speed throughout the title and prologue (227 seconds, none
  below), the game thread 11 percent busy at the median.
  The opening on Outset (Link waking, the lookout) is the heaviest stretch measured: median full speed, but
  the game thread is 94 to 99 percent busy and 32 of 128 seconds dipped below full speed (lowest 79 percent).
  The builds that reach 30 FPS on the iPad use optimization profiles (docs/BUILDER.md), which this port does
  not have yet.

Not yet tried on Windows: a game controller, audio on other output devices, the HD texture packs, the later
game, and other PCs (AMD CPUs and GPUs, Vulkan).

## What you need

- Windows 10 or 11 on an x86-64 PC. The game module is compiled for `x86-64-v3` by default (AVX2, FMA, BMI2,
  MOVBE: Intel Haswell, AMD Zen or newer); the builder drops to an older level on older CPUs.
- A GPU with Direct3D 12
- [Visual Studio 2022 or newer](https://visualstudio.microsoft.com/) (Community is fine) with the
  **Desktop development with C++** workload and the **C++ Clang Compiler for Windows** component
- [Python 3.10+](https://www.python.org/), [Git](https://git-scm.com/), and CMake 3.25+ and Ninja
  (`pip install cmake ninja` works)
- Your disc image of *The Legend of Zelda: The Wind Waker*, GameCube USA (`GZLE01`, revision 0). An `.iso` or
  `.gcm` works directly. A Dolphin `.rvz` (or `.wia`, `.gcz`, `.ciso`, `.nfs`) is converted to an ISO with
  [nodtool](https://github.com/encounter/nod), which the builder compiles from crates.io the first time; that needs
  [Rust](https://rustup.rs). You can instead convert it in Dolphin (right-click the game, **Convert File...**,
  format ISO).
- About 15 GB of free disk space (the converted disc, the generated source and the compiled module)

## Build

From a normal terminal in the checkout:

```bash
python scripts/windows/build.py "D:\Games\The Legend of Zelda - The Wind Waker (USA).rvz"
```

The builder finds Visual Studio itself (no developer prompt needed), fetches the pinned RecompCore and DolRecomp
into `ref/`, checks and converts the disc, extracts and translates the game, checks the generated source against
the verified digest, adds the mods, compiles the game module and the app, and writes the app folder
`build\windows\BlueWake`. Each stage prints its progress; full logs are in `build\windows\logs`. Rerunning the
same command reuses finished work.

Options (`--help` lists all):

| Option | |
| --- | --- |
| `--source-only` | Stop after generating the source: checks your tools, disc and translation in a few minutes |
| `--no-mods` | Skip the mods (widescreen 16:9 and 16:10, Better Wind Waker's options) |
| `--jobs N` | Parallel compile jobs (default: the cores, as far as free memory allows) |
| `--march LEVEL` | CPU level for the game module (default `x86-64-v3`) |
| `--console` | Build `BlueWake.exe` as a console program |
| `--out DIR` | Build directory (default `build\windows`) |

## Play

Run `build\windows\BlueWake\BlueWake.exe`.

| | |
| --- | --- |
| Control stick | W A S D |
| C-stick | T F G H |
| D-pad | arrow keys |
| A, B, X, Y | J, K, U, I |
| L, R, Z | E, R, Q |
| START | Return |
| Camera | Click the game, then move the mouse (Esc releases it) |
| Fullscreen | F11 |
| Smooth Motion | F10 |
| Frame rate | F9 |

Game controllers work through SDL (Xbox, PlayStation, Switch Pro and others). The title screen wants A to reach
the file menu. The mouse turns the game's own camera around Link and tilts it, and a left click is A; a
cutscene, door, Z-target or first-person view takes the camera back.

Command-line options (`BlueWake.exe --help`):

| Option | |
| --- | --- |
| `--widescreen` | 16:9: the widescreen mod (a wider camera, culling and HUD) with a 16:9 picture |
| `--aspect 16:10` | 16:10 instead (`4:3` is the game's own) |
| `--smooth` | Smooth Motion: 60 FPS, the renderer drawing a blended frame between each of the game's 30 |
| `--betterww` | Better Wind Waker's settings at their defaults (Swift Sail, instant text, faster climbing...) |
| `--options LIST` | Change them: `name,-name,...`, or `none,name,...` (names in `mods/betterww/options.txt`) |
| `--fullscreen` | Start in fullscreen |
| `--window WxH` | The window's size |
| `--scale N` | Render at N x 480 lines (0: the window's own pixels) |
| `--fps` | Show the frame rate |
| `--stretch` | Fill the window instead of keeping the game's aspect ratio |
| `--no-mouse-camera` | Keep the mouse out of the camera |
| `--lle-audio` | Run the DSP's own microcode instead of the high-level Zelda ucode |
| `--disc FILE` | Read another copy of the disc |

The mods need no extra files: they are compiled into your game module from your disc (see [MODS.md](MODS.md)).
The environment variables `scripts/mac/run_host.sh` documents (`BLUEWAKE_*`, `DOL_*`) work the same way, for
example `BLUEWAKE_MOUSE_SENSITIVITY` and `BLUEWAKE_MOUSE_INVERT_Y`.

## Your saves and logs

Everything that is yours lives in `%APPDATA%\BlueWake`, outside the build, so rebuilding or deleting the build
never touches it:

- `GZLE01.card`: the memory card with your saves
- `sram.bin`: the console's settings (sound mode and the like)
- `logs\session-*.log`: the newest eight sessions, one line a second of speed and timing plus anything that went
  wrong. Attach the relevant one to a bug report. If BlueWake crashes, the log says where.
- Aurora's pipeline cache, so later launches start drawing sooner

## How the port works

The Windows host is `windows/`: a CMake project that compiles the unchanged host (`runtime/host/src`), GXRuntime
with Aurora (prebuilt Dawn and SDL3 packages, as Aurora fetches them), and Dolphin's DSP from RecompCore, with
clang (GNU driver, MSVC ABI) from Visual Studio.

- **POSIX calls.** The host uses a handful: threads, clocks and sleeps, the environment, `dlopen`, directory
  listing. `windows/compat` provides them on Win32 (sleeps use a high-resolution waitable timer, since `Sleep`
  rounds up to the 15.6 ms tick). The header is force-included into BlueWake's own sources and GXRuntime's C
  runtime only, never into third-party code.
- **The game module** is `gGZLE01_recomp.dll`, built by the same `cmake/composite` project as the iOS dylib, and
  loaded the same way. On Windows it exports its entry points explicitly.
- **The same source, byte for byte.** Windows' C runtime and Python write text files with CRLF line endings,
  which would change the generated game source and its verified digest. The translator is linked with MSVC's
  `binmode.obj` (binary file mode by default), and the generators write `\n` explicitly. The source this builder
  generates has the same digest as the macOS builder's.
- **Compile time.** Each translated chunk is one very large function, and two LLVM passes are superlinear on
  them with clang 22 for x86-64 (measured with `-ftime-report`). The SLP vectorizer took 92 percent of a
  typical large chunk's time, and the largest chunks took over half an hour each; `-fno-slp-vectorize` brings
  them to a minute or two. The register coalescer took 95 percent of the worst remaining chunk's 44 minutes,
  joining copies into the context pointer's function-long live range again and again; capping that per range
  (`-mllvm -large-interval-freq-threshold=10`) brings it to about two minutes.
- **Memory.** A large chunk takes 1 to 3 GB in clang, so the builder runs as many compile jobs as free memory
  allows, not one per core, and retries a chunk that ran out of memory with fewer jobs.
- **The DSP** runs Dolphin's interpreter and its high-level Zelda ucode, as on iOS; the x64 DSP JIT is not built.
- **Floating point.** GXRuntime maps the guest's rounding and non-IEEE modes onto the x86 MXCSR, as it does onto
  the arm64 FPCR.
- **Stack.** Translated code recurses on the host stack as the game does on the GameCube's; the executable
  reserves 64 MB for the main thread (Windows' default is 1 MB).

- **Better Wind Waker's REL sites.** DolRecomp names a REL's option sites by its file name after the last `/`,
  and on Windows it joins a folder and a file with `\`, so the sites in `d_a_ship` and `d_a_agbsw0` would not
  match. The builder names the RELs' folder with `/` and a trailing `/` for that translation, which gives the
  15 option chunks the Mac build has, not 11.
- **Windows' own costs.** The C runtime's `getenv` locks and scans the whole environment, and GXRuntime reads
  a trace switch on every guest exception (4 percent of the game thread in a profile): its C sources and the
  module's runtime remember each call site's answer (`windows/compat/bw_getenv_cache.h`). Aurora paces frames
  with short sleeps, which Windows' default 15.6 ms timer tick would stretch; the app asks for 1 ms.
- **Profiling.** `BLUEWAKE_HOST_PROFILE=FILE` samples the game thread every millisecond and writes where it
  was, by module and offset, charging time in system code to the BlueWake function that called it.

Not done on Windows: the local optimization training and the bundled PGO profiles (both are built and measured
on Apple Silicon), and the iOS overlay menus (touch controls, controller remapping, the in-game mods and
save-management screens). Mods are chosen with command-line options instead.
