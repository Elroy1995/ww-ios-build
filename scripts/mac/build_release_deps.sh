#!/usr/bin/env bash
# Static libraries for the Mac release host, from checksum-pinned source, for
# macOS $MACOS_TARGET (default 15.0) on Apple Silicon. Homebrew's own copies
# are built for the Mac they were installed on (macOS 27 here), so a host
# linked against them runs only there.
#
#   scripts/mac/build_release_deps.sh [PREFIX]
#
# PREFIX defaults to build/mac-release/deps/install in the main checkout.
set -euo pipefail
ROOT=$(dirname "$(git -C "$(dirname "$0")" rev-parse --path-format=absolute --git-common-dir)")
MACOS_TARGET=${MACOS_TARGET:-15.0}
WORK=$ROOT/build/mac-release/deps
PREFIX=${1:-$WORK/install}
JOBS=${JOBS:-$(sysctl -n hw.ncpu)}
SDK=$(xcrun --sdk macosx --show-sdk-path)
export MACOSX_DEPLOYMENT_TARGET=$MACOS_TARGET
mkdir -p "$WORK"/{downloads,src,obj} "$PREFIX"

fetch() {
    local url=$1 sha=$2 archive="$WORK/downloads/${1##*/}"
    [ -f "$archive" ] || curl -fL --retry 2 --connect-timeout 20 -o "$archive" "$url"
    if [ "$(shasum -a 256 "$archive" | awk '{print $1}')" != "$sha" ]; then
        echo "checksum mismatch: $archive" >&2
        exit 1
    fi
    case $archive in
    *.zip) ditto -x -k "$archive" "$WORK/src" ;;
    *) tar -xf "$archive" -C "$WORK/src" ;;
    esac
}

fetch https://github.com/abseil/abseil-cpp/releases/download/20260817.0/abseil-cpp-20260817.0.tar.gz \
    f7e05179df39c45434cad433f5783840bb3788ef322976f9138bc6b72b3a107d
fetch https://github.com/fmtlib/fmt/releases/download/12.2.0/fmt-12.2.0.zip \
    a2f4a8d51178f954e4c339007f77edd76ba0cb2e36f87a48e5a5403d9be5878f
fetch https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz \
    eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
fetch https://downloads.sourceforge.net/project/libpng/libpng16/1.6.58/libpng-1.6.58.tar.xz \
    28eb403f51f0f7405249132cecfe82ea5c0ef97f1b32c5a65828814ae0d34775
fetch https://downloads.sourceforge.net/project/freetype/freetype2/2.14.3/freetype-2.14.3.tar.xz \
    36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f

common=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOS_TARGET" "-DCMAKE_OSX_SYSROOT=$SDK"
    "-DCMAKE_INSTALL_PREFIX=$PREFIX" "-DCMAKE_PREFIX_PATH=$PREFIX" -DBUILD_SHARED_LIBS=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local"
    "-DZLIB_LIBRARY=$SDK/usr/lib/libz.tbd" "-DZLIB_INCLUDE_DIR=$SDK/usr/include")

build() {
    local name=$1 source=$2
    shift 2
    cmake -S "$source" -B "$WORK/obj/$name" "${common[@]}" "$@" >"$WORK/obj/$name.configure.log"
    cmake --build "$WORK/obj/$name" -j "$JOBS" >"$WORK/obj/$name.build.log"
    cmake --install "$WORK/obj/$name" >"$WORK/obj/$name.install.log"
    echo "$name: built for macOS $MACOS_TARGET"
}

build abseil "$WORK/src/abseil-cpp-20260817.0" -DCMAKE_CXX_STANDARD=20 -DABSL_PROPAGATE_CXX_STD=ON \
    -DABSL_BUILD_TESTING=OFF -DBUILD_TESTING=OFF
build fmt "$WORK/src/fmt-12.2.0" -DFMT_TEST=OFF -DFMT_DOC=OFF
build zstd "$WORK/src/zstd-1.5.7/build/cmake" -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_STATIC=ON \
    -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_TESTS=OFF
build libpng "$WORK/src/libpng-1.6.58" -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF \
    -DPNG_FRAMEWORK=OFF
build freetype "$WORK/src/freetype-2.14.3" -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON \
    -DFT_DISABLE_BZIP2=ON -DFT_REQUIRE_PNG=ON -DFT_REQUIRE_ZLIB=ON
# nod (MIT OR Apache-2.0), prebuilt for Apple Silicon: the launcher unpacks
# compressed disc images (Dolphin's RVZ, WIA, GCZ, CISO and others) with it.
NOD=$WORK/nod
nod_archive=$WORK/downloads/libnod-macos-arm64.tar.gz
[ -f "$nod_archive" ] || curl -fL --retry 2 --connect-timeout 20 -o "$nod_archive" \
    https://github.com/encounter/nod/releases/download/v2.0.0-alpha.10/libnod-macos-arm64.tar.gz
[ "$(shasum -a 256 "$nod_archive" | awk '{print $1}')" = 878fa0afb92175c555ec949322c263886b38a304499a43c12861261e5be61e87 ] ||
    { echo "checksum mismatch: $nod_archive" >&2; exit 1; }
rm -rf "$NOD" && mkdir -p "$NOD" && tar -xzf "$nod_archive" -C "$NOD"
for license in LICENSE-MIT LICENSE-APACHE; do
    curl -fsSL -o "$NOD/$license" "https://raw.githubusercontent.com/encounter/nod/v2.0.0-alpha.10/$license"
done
echo "nod: v2.0.0-alpha.10 in $NOD"
echo "prefix: $PREFIX"
