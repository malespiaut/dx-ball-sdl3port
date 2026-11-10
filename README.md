# DX-Ball SDL3 port

Native Linux, macOS and Windows builds of DX-Ball v1.07, made from the
readable C/C++ source reconstructed from the original executable. SDL3 replaces
DirectDraw, DirectSound and the WinMM MIDI player; the recovered game code is
unchanged.

<p align="center">
  <img src="resources/dxball-title.png" alt="DX-Ball title screen" width="900">
</p>

This fork contains only the SDL3 port. The reconstruction itself — its progress,
the REA workflow, and building the original Windows executable with Visual C++
4.0 — lives upstream at
[N0zoM1z0/dx-ball](https://github.com/N0zoM1z0/dx-ball).

## Download

Each [release](https://github.com/malespiaut/dx-ball-sdl3port/releases) has
ready-to-run packages:

| Platform | Package |
| --- | --- |
| Windows 10/11, 64-bit | `dxball-sdl3-windows-x86_64.zip` |
| Windows 10/11, 32-bit | `dxball-sdl3-windows-i686.zip` |
| Linux, 64-bit | `dxball-sdl3-linux-x86_64.tar.gz` |
| Linux, 32-bit | `dxball-sdl3-linux-i686.tar.gz` |
| macOS 15+, Apple Silicon | `dxball-sdl3-macos-arm64.tar.gz` |

The game data is not included. Copy the files of the English DX-Ball v1.07
game into the package folder, next to `dxball`. On Linux the names must be
lowercase. Music needs a General MIDI SoundFont named `soundfont.sf2` in the same
folder, and on Linux the system's FluidSynth; without them the game plays sound
effects only. `README-SDL3.txt` in each
package has the details.

## Build from source

Requirements: CMake 3.20+, Ninja, GCC or Clang, and SDL3 3.2+. FluidSynth is
optional and plays the music.

```bash
cmake -S ports/sdl3 -B build/sdl3 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/sdl3
ctest --test-dir build/sdl3 --output-on-failure
```

On Linux, `scripts/repo-python --stdlib scripts/run-linux.py` checks the
original files in `original/`, copies them with lowercase names to a working
folder and starts the game there. Windows builds cross-compile from Linux with
MinGW-w64 toolchain files in `ports/sdl3/toolchains/`. Linux and Windows build
for 32-bit as well as 64-bit. See the [SDL3 build guide](docs/BUILD.md#sdl3-build)
for each platform, the tests and the packaging scripts.

Pushing a tag runs the `SDL3 builds` workflow, which builds and tests every
platform and attaches the packages to that tag's release.

## Play

The window opens at twice the original 640×480 when the screen has room, and
it can be resized. `Alt+Enter` toggles full screen, `P` pauses and `Esc` leaves
the current mode. Rankings and edited boards are saved in the game folder.

## Contribute

[Report issues](https://github.com/malespiaut/dx-ball-sdl3port/issues/new) for
the port with your platform, package or commit, reproduction steps, and
expected versus actual behavior. Platform changes belong in `ports/sdl3/`
and the `src/sdl3_*` files: the recovered game sources and the root
`CMakeLists.txt` are hashed into the upstream verification records. Report
problems in the recovered game logic upstream.

## Read the code

- [Architecture](docs/ARCHITECTURE.md): entry, modes, a gameplay frame and platform services.
- [Source map](src/README.md): the modules behind each subsystem.
- [SDL3 build guide](docs/BUILD.md#sdl3-build): the platform layer and its tests.

DX-Ball was created by Michael P. Welch, with 3D graphics by Seumas McNally.
See [LICENSE](LICENSE).
