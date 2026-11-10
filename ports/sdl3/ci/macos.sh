#!/bin/bash
# Build, test and package the macOS SDL3 game with Homebrew's SDL3 and
# FluidSynth; dylibbundler copies every non-system library into libs/.
# Usage, from the repository root on macOS: ports/sdl3/ci/macos.sh
set -euo pipefail
arch=$(uname -m)
brew install --quiet sdl3 fluidsynth dylibbundler ninja

build=build/ci-macos-$arch
cmake -S ports/sdl3 -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$(brew --prefix)"
grep -Eq '^(FluidSynth_DIR:PATH=/|FLUIDSYNTH_FOUND:INTERNAL=1)' "$build/CMakeCache.txt" ||
    { echo "FluidSynth missing"; exit 1; }
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure

work=$(mktemp -d)
stage=$work/dxball-sdl3-macos-$arch
cmake --install "$build" --prefix "$stage" --strip
# The game loads FluidSynth at run time, so bundle it explicitly; the core
# library follows dxball into libs/.
fluidsynth=$work/libfluidsynth.3.dylib
cp "$(brew --prefix fluidsynth)/lib/libfluidsynth.3.dylib" "$fluidsynth"
chmod u+w "$fluidsynth"
dylibbundler -cd -of -b -x "$stage/dxball" -x "$fluidsynth" \
    -d "$stage/libs" -p @executable_path/libs/ -s "$stage" >/dev/null
mv "$fluidsynth" "$stage/libs/"
rm "$stage/libdxball_core.dylib"
# Rewritten load commands invalidate signatures; arm64 needs a valid one.
codesign --force --sign - "$stage"/libs/*.dylib "$stage/dxball"

# Every library the package loads must be a system one or ship in libs/.
for file in "$stage/dxball" "$stage"/libs/*.dylib; do
    for dependency in $(otool -l "$file" |
            awk '/cmd LC_(LOAD|LOAD_WEAK|REEXPORT)_DYLIB/ { getline; getline; print $2 }'); do
        case $dependency in
            /usr/lib/* | /System/*) ;;
            @executable_path/libs/*) [ -e "$stage/libs/${dependency#@executable_path/libs/}" ] ||
                { echo "$file needs missing $dependency"; exit 1; } ;;
            *) echo "$file needs unbundled $dependency"; exit 1 ;;
        esac
    done
done

ports/sdl3/ci/add-notes.sh "$stage"
ports/sdl3/ci/check-package.sh "$stage/dxball"

mkdir -p dist
tar -C "$work" -czf "dist/dxball-sdl3-macos-$arch.tar.gz" "dxball-sdl3-macos-$arch"
