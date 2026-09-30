#!/usr/bin/env bash
# Package the Mac release: "Wind Waker Recomp.app", zipped, with a build record.
#
#   scripts/mac/package_release.sh VERSION [HOST] [MODULE]
#
# HOST is a release host built for macOS 15 against the static libraries of
# scripts/mac/build_release_deps.sh (build/mac-release/host/host/bluewake_host);
# MODULE is the recompiled game module (build/mac-interp/composite-options/
# gGZLE01_recomp.dylib, the one with Better Wind Waker's settings and the
# widescreen code). The app contains no game data: its launcher
# (scripts/mac/release/launcher.m, AppKit) asks for the player's own disc at the
# first launch. Output: build/mac-release/dist/WindWakerRecomp-VERSION-macos-arm64.zip.
set -euo pipefail
ROOT=$(dirname "$(git -C "$(dirname "$0")" rev-parse --path-format=absolute --git-common-dir)")
SRC=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
[ $# -ge 1 ] || { sed -n '2,12p' "$0"; exit 2; }
VERSION=$1
HOST=${2:-$ROOT/build/mac-release/host/host/bluewake_host}
MODULE=${3:-$ROOT/build/mac-interp/composite-options/gGZLE01_recomp.dylib}
DEPS=$ROOT/build/mac-release/deps
HOST_DEPS=$ROOT/build/mac-release/host/_deps
DIST=$ROOT/build/mac-release/dist
MACOS_TARGET=15.0
NAME="Wind Waker Recomp"
APP=$DIST/$NAME.app
ZIP=$DIST/WindWakerRecomp-${VERSION%%-*}-macos-arm64.zip

die() { echo "package_release: $*" >&2; exit 1; }
[ -x "$HOST" ] || die "no host at $HOST"
[ -f "$MODULE" ] || die "no game module at $MODULE"
# Only system libraries, and nothing newer than the release's macOS.
if otool -L "$HOST" | tail -n +2 | grep -vE '^\s+(/System/Library/|/usr/lib/)' | grep -q .; then
    die "the host links non-system libraries; build it with scripts/mac/build_release_deps.sh"
fi
for binary in "$HOST" "$MODULE"; do
    minos=$(otool -l "$binary" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; exit}')
    [ "$(printf '%s\n%s\n' "$minos" "$MACOS_TARGET" | sort -V | tail -1)" = "$MACOS_TARGET" ] ||
        die "$binary needs macOS $minos, newer than $MACOS_TARGET"
done

rm -rf "$APP" "$ZIP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources/licenses"

# The launcher, with the iPad app's disc preparation.
# nod's C objects claim macOS 15.5; they are compression code without OS calls.
[ -f "$DEPS/nod/lib/libnod.a" ] || die "no nod in $DEPS/nod; run scripts/mac/build_release_deps.sh"
xcrun clang -O2 -arch arm64 -mmacosx-version-min=$MACOS_TARGET -Wall -fobjc-arc \
    -I "$SRC/apple/ios/src" -I "$DEPS/nod/include" "$SRC/scripts/mac/release/launcher.m" \
    "$SRC/apple/ios/src/disc_import.c" "$DEPS/nod/lib/libnod.a" -framework Cocoa \
    -framework UniformTypeIdentifiers -Wl,-w -o "$APP/Contents/MacOS/$NAME"
cp "$HOST" "$APP/Contents/MacOS/bluewake_host"
cp "$MODULE" "$APP/Contents/Frameworks/gGZLE01_recomp.dylib"

# The icon, from the project's artwork.
ICONSET=$DIST/AppIcon.iconset
rm -rf "$ICONSET"
mkdir -p "$ICONSET"
for size in 16 32 128 256 512; do
    sips -z $size $size "$SRC/apple/ios/resources/BlueWave.png" --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z $double $double "$SRC/apple/ios/resources/BlueWave.png" --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
rm -rf "$ICONSET"

cat >"$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "https://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>en</string>
  <key>CFBundleDisplayName</key><string>$NAME</string>
  <key>CFBundleName</key><string>$NAME</string>
  <key>CFBundleExecutable</key><string>$NAME</string>
  <key>CFBundleIdentifier</key><string>io.github.elliotttate.windwakerrecomp</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${VERSION%%-*}</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>$MACOS_TARGET</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>GCSupportsControllerUserInteraction</key><true/>
</dict>
</plist>
EOF

# Licenses and notices.
L=$APP/Contents/Resources/licenses
cp "$SRC/LICENSE" "$L/WindWakerRecomp-GPL-3.0.txt"
cp "$SRC/RIGHTS_AND_LICENSES.md" "$L/"
cp "$SRC/ref/recompcore/COPYING" "$L/RecompCore-COPYING.txt"
cp "$SRC/ref/recompcore/LICENSES/GPL-2.0-or-later.txt" "$L/Dolphin-GPL-2.0-or-later.txt"
cp "$SRC/ref/recompcore/GXRuntime/graphics/aurora/LICENSE" "$L/Aurora-LICENSE.txt"
cp "$SRC/ref/recompcore/LICENSES/BSD-3-Clause.txt" "$L/Dawn-BSD-3-Clause.txt"
cp "$HOST_DEPS/sdl-src/LICENSE.txt" "$L/SDL3-LICENSE.txt"
cp "$HOST_DEPS/imgui-src/LICENSE.txt" "$L/DearImGui-LICENSE.txt"
cp "$HOST_DEPS/xxhash-src/LICENSE" "$L/xxHash-LICENSE.txt"
cp "$HOST_DEPS/tracy-src/LICENSE" "$L/Tracy-LICENSE.txt"
cp "$DEPS/src/abseil-cpp-20260817.0/LICENSE" "$L/Abseil-LICENSE.txt"
cp "$DEPS/src/fmt-12.2.0/LICENSE" "$L/fmt-LICENSE.txt"
cp "$DEPS/src/zstd-1.5.7/LICENSE" "$L/zstd-LICENSE.txt"
cp "$DEPS/src/libpng-1.6.58/LICENSE" "$L/libpng-LICENSE.txt"
cp "$DEPS/src/freetype-2.14.3/LICENSE.TXT" "$L/FreeType-LICENSE.txt"
cp "$DEPS/src/freetype-2.14.3/docs/FTL.TXT" "$L/FreeType-FTL.txt"
cp "$DEPS/nod/LICENSE-MIT" "$L/nod-LICENSE-MIT.txt"
cp "$DEPS/nod/LICENSE-APACHE" "$L/nod-LICENSE-APACHE.txt"
cat >"$L/THIRD_PARTY_NOTICES.md" <<'EOF'
# Third-party notices

Wind Waker Recomp is GPL-3.0-or-later (WindWakerRecomp-GPL-3.0.txt; RIGHTS_AND_LICENSES.md).
The app also contains:

| Component | License | Text |
| --- | --- | --- |
| RecompCore compatibility runtime, derived from Dolphin | GPL-2.0-or-later, as a whole compatible with GPL-3.0 | RecompCore-COPYING.txt, Dolphin-GPL-2.0-or-later.txt |
| Aurora renderer | see its license | Aurora-LICENSE.txt |
| Dawn (WebGPU on Metal), statically linked | BSD-3-Clause, The Dawn & Tint Authors | Dawn-BSD-3-Clause.txt |
| SDL 3 | zlib | SDL3-LICENSE.txt |
| Dear ImGui | MIT | DearImGui-LICENSE.txt |
| xxHash | BSD-2-Clause | xxHash-LICENSE.txt |
| Tracy client | BSD-3-Clause | Tracy-LICENSE.txt |
| Abseil 20260817.0 | Apache-2.0 | Abseil-LICENSE.txt |
| {fmt} 12.2.0 | MIT | fmt-LICENSE.txt |
| Zstandard 1.5.7 | BSD-3-Clause | zstd-LICENSE.txt |
| libpng 1.6.58 | libpng-2.0 | libpng-LICENSE.txt |
| FreeType 2.14.3 | FreeType License | FreeType-LICENSE.txt, FreeType-FTL.txt |
| nod v2.0.0-alpha.10 (disc image reading, in the launcher) | MIT OR Apache-2.0 | nod-LICENSE-MIT.txt, nod-LICENSE-APACHE.txt |
| Widescreen code from Dolphin's GZLE01 game settings | GPL-2.0-or-later | Dolphin-GPL-2.0-or-later.txt |
| Better Wind Waker's settings, reimplemented after WideBoner/betterww | MIT | https://github.com/WideBoner/betterww |

The Legend of Zelda: The Wind Waker is Nintendo's. Frameworks/gGZLE01_recomp.dylib is code recompiled
from the game; the app contains no disc image, game files, textures, audio or saves, and runs only
with your own legally obtained disc (GZLE01, USA revision 0).
EOF

# What it was built from.
sha() { shasum -a 256 "$1" | awk '{print $1}'; }
cat >"$APP/Contents/Resources/BUILD.json" <<EOF
{
  "version": "$VERSION",
  "built": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "macos_minimum": "$MACOS_TARGET",
  "architecture": "arm64",
  "wind_waker_recomp": "$(git -C "$SRC" rev-parse HEAD)",
  "recompcore": "$(git -C "$SRC/ref/recompcore" rev-parse HEAD)",
  "host_sha256": "$(sha "$HOST")",
  "game_module_sha256": "$(sha "$MODULE")",
  "launcher_sha256": "$(sha "$APP/Contents/MacOS/$NAME")",
  "static_libraries": "scripts/mac/build_release_deps.sh: abseil 20260817.0, fmt 12.2.0, zstd 1.5.7, libpng 1.6.58, FreeType 2.14.3, nod v2.0.0-alpha.10 (launcher)",
  "supported_disc": "GZLE01 USA revision 0 (main.dol SHA-1 8d28bab68bb5078c38e43f29206f0bd01f7e7a67)"
}
EOF
cp "$APP/Contents/Resources/BUILD.json" "$DIST/BUILD.json"

# Ad hoc signatures, innermost first.
codesign --force --sign - "$APP/Contents/Frameworks/gGZLE01_recomp.dylib"
codesign --force --sign - "$APP/Contents/MacOS/bluewake_host"
codesign --force --sign - "$APP/Contents/MacOS/$NAME"
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"

(cd "$DIST" && ditto -c -k --sequesterRsrc --keepParent "$NAME.app" "$(basename "$ZIP")")
echo "$(sha "$ZIP")  $(basename "$ZIP")" | tee "$ZIP.sha256"
echo "packaged: $ZIP"
