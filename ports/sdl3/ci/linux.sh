#!/bin/bash
# Build, test and package the Linux SDL3 game inside Debian 12 (glibc 2.36),
# for the image's architecture. SDL3 is built from source and shipped beside
# the game; FluidSynth comes from the player's system (libfluidsynth3).
# (Debian 12's FluidSynth CMake package also requires the fluidsynth tool.)
# Usage, from the repository root: docker run ... debian:bookworm ports/sdl3/ci/linux.sh ARCH
set -euo pipefail
arch=$1
sdl_version=3.4.20
sdl_sha256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    build-essential cmake ninja-build pkg-config python3 curl ca-certificates file \
    libfluidsynth-dev fluidsynth \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
    libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev libwayland-bin wayland-protocols \
    libdecor-0-dev libegl-dev libgl-dev libgles-dev libdrm-dev libgbm-dev \
    libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev >/dev/null

work=$(mktemp -d)
curl -fsSL -o "$work/sdl.tar.gz" \
    "https://github.com/libsdl-org/SDL/releases/download/release-$sdl_version/SDL3-$sdl_version.tar.gz"
echo "$sdl_sha256  $work/sdl.tar.gz" | sha256sum -c -
tar -xzf "$work/sdl.tar.gz" -C "$work"
cmake -S "$work/SDL3-$sdl_version" -B "$work/sdl-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/opt/sdl3 -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF >/dev/null
cmake --build "$work/sdl-build"
cmake --install "$work/sdl-build" >/dev/null

build=build/ci-linux-$arch
cmake -S ports/sdl3 -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/sdl3
grep -Eq '^(FluidSynth_DIR:PATH=/|FLUIDSYNTH_FOUND:INTERNAL=1)' "$build/CMakeCache.txt" ||
    { echo "FluidSynth missing"; exit 1; }
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure

stage=$work/dxball-sdl3-linux-$arch
cmake --install "$build" --prefix "$stage" --strip
cp -a /opt/sdl3/lib*/libSDL3.so.0* "$stage/"
ports/sdl3/ci/add-notes.sh "$stage"
ports/sdl3/ci/check-package.sh "$stage/dxball"
mkdir -p dist
tar -C "$work" -czf "dist/dxball-sdl3-linux-$arch.tar.gz" "dxball-sdl3-linux-$arch"
