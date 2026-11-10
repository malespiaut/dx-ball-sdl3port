#!/bin/bash
# Cross-compile, test (under Wine) and package the Windows SDL3 game with
# MinGW-w64. SDL3 and FluidSynth come from their official Windows releases.
# Usage, from the repository root: ports/sdl3/ci/windows.sh x86_64|i686
set -euo pipefail
arch=$1
sdl_version=3.4.20
sdl_sha256=d2d3bf3fb97a90b22e908a306d7362631ef1bab229d988fe6669595dc29cd9ed
fluidsynth_version=2.6.2
case $arch in
    x86_64) fs_arch=x64; fs_sha256=808295345d43c9c428f3fa87d29fbf97a9926f356df95559ff2689f3bd4072d7 ;;
    i686) fs_arch=x86; fs_sha256=2a0f610a58f7cb1699c7faea597850e48ac8332d1873e4bcc9e9b3b308e01f2e ;;
    *) echo "unknown architecture $arch"; exit 2 ;;
esac

work=$(mktemp -d)
curl -fsSL -o "$work/sdl.tar.gz" \
    "https://github.com/libsdl-org/SDL/releases/download/release-$sdl_version/SDL3-devel-$sdl_version-mingw.tar.gz"
echo "$sdl_sha256  $work/sdl.tar.gz" | sha256sum -c -
tar -xzf "$work/sdl.tar.gz" -C "$work"
sdl=$work/SDL3-$sdl_version/$arch-w64-mingw32
fs_name=fluidsynth-v$fluidsynth_version-win10-$fs_arch-cpp11
curl -fsSL -o "$work/fs.zip" \
    "https://github.com/FluidSynth/fluidsynth/releases/download/v$fluidsynth_version/$fs_name.zip"
echo "$fs_sha256  $work/fs.zip" | sha256sum -c -
unzip -q "$work/fs.zip" -d "$work/fs"
fs=$work/fs/$fs_name

build=build/ci-windows-$arch
cmake -S ports/sdl3 -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    --toolchain "$PWD/ports/sdl3/toolchains/$arch-w64-mingw32.cmake" \
    "-DCMAKE_PREFIX_PATH=$sdl;$fs" -DCMAKE_CROSSCOMPILING_EMULATOR=wine
grep -q '^FluidSynth_DIR:PATH=/' "$build/CMakeCache.txt" || { echo "FluidSynth missing"; exit 1; }
cmake --build "$build"

stage=$work/dxball-sdl3-windows-$arch
cmake --install "$build" --prefix "$stage" --strip
# FluidSynth's zip also carries an SDL3.dll; the game ships the one it built against.
cp "$sdl/bin/SDL3.dll" "$fs/bin/libfluidsynth-3.dll" "$fs/bin/sndfile.dll" "$stage/"
ports/sdl3/ci/add-notes.sh "$stage"

export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml,winemenubuilder.exe=d"
export WINEPREFIX=$work/wine WINEPATH="Z:$stage"
wineboot --init >/dev/null 2>&1 || true
ctest --test-dir "$build" --output-on-failure --timeout 300
ports/sdl3/ci/check-package.sh wine "$stage/dxball.exe"

mkdir -p dist
(cd "$work" && zip -qr "$OLDPWD/dist/dxball-sdl3-windows-$arch.zip" "dxball-sdl3-windows-$arch")
