#!/usr/bin/env python3
"""The Windows download: the app folder the builder made, without the game.

  python scripts/windows/package_release.py VERSION [--app build/windows/BlueWake] [--out build/windows/release]

Copies the app (BlueWake.exe, the game module, the runtime DLLs, Aurora's
pipeline seed, the DSP files) and nodtool.exe (which unpacks a Dolphin .rvz on
the player's first launch) into WindWakerRecomp/, adds a README and the
licenses, and writes WindWakerRecomp-VERSION-Windows-x64.zip and its .sha256.
Nothing from the disc goes in: not the disc image, main.dol, the RELs or any
save. The first launch asks for the player's own disc and prepares it
(windows/src/win_disc.c). The script refuses to write the zip if a file from
the disc, a save or a debug database would be in it.
"""
import argparse
import hashlib
import json
import re
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAME = "WindWakerRecomp"
# Never in a download: the disc and anything taken from it, saves, debug
# databases and the builder's optimization profiles.
PRIVATE = re.compile(r"\.(iso|gcm|rvz|wia|gcz|ciso|nfs|wbfs|dol|rel|arc|card|gci|sav|raw|pdb|profdata|profraw)$", re.I)
APP_FILES = re.compile(r"\.(exe|dll)$", re.I)
DEPS = ROOT / "build/windows/app/_deps"
# name in the download: where the text is (the first that exists)
LICENSES = {
    "BlueWake-GPL-3.0.txt": [ROOT / "LICENSE"],
    "RecompCore-COPYING.txt": [ROOT / "ref/recompcore/COPYING"],
    "Aurora-MIT.txt": [ROOT / "ref/recompcore/GXRuntime/graphics/aurora/LICENSE"],
    "nodtool-MIT.txt": [ROOT / "windows/licenses/nod-MIT.txt"],
    "Dawn.txt": [ROOT / "windows/licenses/Dawn-BSD-3-Clause.txt"],
    "DirectXShaderCompiler.txt": [ROOT / "windows/licenses/DirectXShaderCompiler-LICENSE.txt"],
    "SDL3-zlib.txt": [DEPS / "sdl3_prebuilt-src/licenses/SDL3/LICENSE.txt"],
    "zlib.txt": [DEPS / "zlib-src/LICENSE.md", DEPS / "zlib-src/LICENSE"],
    "libpng.txt": [DEPS / "png-src/LICENSE"],
    "Dear-ImGui-MIT.txt": [DEPS / "imgui-src/LICENSE.txt"],
    "fmt.txt": [DEPS / "fmt-src/LICENSE"],
    "FreeType.txt": [DEPS / "freetype-src/LICENSE.TXT"],
    "xxHash.txt": [DEPS / "xxhash-src/LICENSE"],
    "zstd.txt": [DEPS / "zstd-src/LICENSE"],
    "Abseil-Apache-2.0.txt": [DEPS / "abseil-cpp-src/LICENSE"],
    "Tracy.txt": [DEPS / "tracy-src/LICENSE"],
}

README = """Wind Waker Recomp {version} for Windows (BlueWake)

The Legend of Zelda: The Wind Waker (GameCube, USA), statically recompiled to
run natively on Windows x64 with Direct3D 12.

You need your own copy of the game: the GameCube USA disc (GZLE01, revision 0)
as a disc image, an .iso or .gcm file or a Dolphin .rvz. None is included.

Start
  1. Unpack this whole folder anywhere and run BlueWake.exe.
  2. The first time, BlueWake asks for your disc image. It checks that it is
     the USA disc, prepares it once (a few seconds; an .rvz is first unpacked
     to an ISO, about 1.4 GB, in %APPDATA%\\BlueWake) and remembers it.
  3. Later launches start the game straight away. To use another disc image:
     F1 (settings), Sound and files, "Choose another disc image".

Needs Windows 10 or 11 (64-bit), a Direct3D 12 GPU, and a CPU with AVX2
(Intel Haswell, AMD Zen or newer).

Smooth Motion: 60 FPS by default, drawn from the game's 30 with in-between
frames. F10 turns it off and on; the settings choose 60 or 120 FPS (120 on a
display of 100 Hz or more). "60 Hz gameplay" in the settings (experimental)
runs the game itself at 60.

Keyboard: W A S D control stick, T F G H C-stick, arrow keys D-pad,
J K U I = A B X Y, E R Q = L R Z, Return START, Space jump, Shift sprint.
Mouse: click the game, then move the mouse to turn the camera (Esc gives the
mouse back); the wheel zooms. Game controllers work too.
F1 or Esc settings, F11 or Alt+Enter fullscreen, F10 Smooth Motion, F9 frame rate.
BlueWake.exe --help lists the command-line options.

Saves, settings, the prepared disc and session logs: %APPDATA%\\BlueWake

Source and documentation: https://github.com/elliotttate/Wind-Waker-Recomp
(docs/WINDOWS.md). BlueWake's code is under the GNU GPL, version 3 or later;
the licenses of it and of the libraries it uses are in licenses\\.

BlueWake is an unofficial project, not affiliated with or endorsed by
Nintendo. The Legend of Zelda: The Wind Waker is Nintendo's; play it from a
disc you own.
"""


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("version", help="the release's version, e.g. v0.1.0")
    parser.add_argument("--app", type=Path, default=ROOT / "build/windows/BlueWake")
    parser.add_argument("--out", type=Path, default=ROOT / "build/windows/release")
    parser.add_argument("--nodtool", type=Path, default=ROOT / "build/tools/nodtool/bin/nodtool.exe")
    args = parser.parse_args()

    app = args.app
    for need in ("BlueWake.exe", "gGZLE01_recomp.dll", "BuilderProvenance.json"):
        if not (app / need).is_file():
            sys.exit(f"{app / need} is missing: run scripts/windows/build.py first")
    provenance = json.loads((app / "BuilderProvenance.json").read_text())
    if provenance.get("source_modified"):
        sys.exit("the app was built from a checkout with uncommitted changes: commit, rebuild, then package")
    if not args.nodtool.is_file():
        sys.exit(f"{args.nodtool} is missing: the builder makes it the first time it converts a .rvz")

    stage = args.out / NAME
    shutil.rmtree(stage, ignore_errors=True)
    stage.mkdir(parents=True)
    for f in sorted(app.iterdir()):
        if f.is_file() and (APP_FILES.search(f.name) or f.name in ("initial_pipeline_cache.db",
                                                                    "BuilderProvenance.json")):
            shutil.copy2(f, stage / f.name)
    (stage / "dsp").mkdir()
    for name in ("dsp_rom.bin", "dsp_coef.bin"):
        shutil.copy2(app / "dsp" / name, stage / "dsp" / name)
    shutil.copy2(args.nodtool, stage / "nodtool.exe")
    (stage / "licenses").mkdir()
    for name, sources in LICENSES.items():
        source = next((s for s in sources if s.is_file()), None)
        if source is None:
            sys.exit(f"no license text for {name} (looked in {', '.join(str(s) for s in sources)})")
        shutil.copy2(source, stage / "licenses" / name)
    (stage / "README.txt").write_text(README.format(version=args.version).replace("\n", "\r\n"), newline="")

    # Nothing private, and nothing unexpected.
    files = sorted(p for p in stage.rglob("*") if p.is_file())
    bad = [p for p in files if PRIVATE.search(p.name) or "game" in p.relative_to(stage).parts[:-1]]
    if bad:
        sys.exit("refusing to package: " + ", ".join(str(p.relative_to(stage)) for p in bad))

    zip_path = args.out / f"{NAME}-{args.version}-Windows-x64.zip"
    zip_path.unlink(missing_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in files:
            z.write(p, f"{NAME}/{p.relative_to(stage).as_posix()}")
    digest = sha256(zip_path)
    (args.out / (zip_path.name + ".sha256")).write_text(f"{digest}  {zip_path.name}\n", newline="\n")
    total = sum(p.stat().st_size for p in files)
    print(f"{zip_path} ({zip_path.stat().st_size >> 20} MB; {len(files)} files, {total >> 20} MB unpacked)")
    print(f"sha256 {digest}")
    print(f"built from {provenance['source_commit']}; module {provenance['module_sha256']}")


if __name__ == "__main__":
    main()
