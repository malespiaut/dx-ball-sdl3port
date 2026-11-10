# Build and play

The current verified game uses the original Visual C++ 4.0 compiler and the
`vc40-restored-crt` profile. See [progress](PROGRESS.md) for completed checks and
[the build record](research/CURRENT_VC4_BUILD.md) for product identities.

Run commands from the repository root. Linux needs Python 3.11+, Wine with
32-bit support and the original English DX-Ball v1.07 archive. The
[workspace setup](SETUP.md) covers prerequisites and analysis tools.

## Build the current Windows game

```bash
scripts/bootstrap-python.sh
scripts/repo-python scripts/import-target.py /path/to/DX_Ball_Win_Preinstalled_EN.zip
scripts/repo-python scripts/bootstrap-tools.py
scripts/repo-python scripts/build-legacy.py --restored-crt
scripts/repo-python scripts/run-windows.py --profile vc40-restored-crt
```

Import verifies the archive and places the EXE/assets in `original/`. The build
writes `build/vc40-restored-crt/dxball.exe`, its map and `build.json`. It links
138 source objects, three integration objects and 19 original CRT members.
Reused objects are checked against compiler, flags, source/header inputs and
object hashes. The link receipt records each reuse or compilation.

The launcher copies the game and supplied data into
`build/runtime/vc40-restored-crt/`. To prepare that folder for Windows without
launching Wine:

```bash
scripts/repo-python scripts/run-windows.py --profile vc40-restored-crt --prepare-only
```

Keep the data files beside `dxball.exe` when running the prepared folder.

## Native Windows source build

The local Windows package has these entry points:

- `Build Reconstruction.cmd`: compile all 141 C/C++ units with English progress.
- `Play DX-Ball.cmd`: run the existing tested game.
- `Play Rebuilt DX-Ball.cmd`: run the new `build/game/dxball.exe`.

The package contains `reconstruction/src`, the pinned VC4 compiler/headers/libs,
support resources and `Build-Reconstruction.ps1`. Windows PowerShell 5.1 is
sufficient; no Python, WSL, Visual Studio installation or administrator rights
are needed on Windows. The normal launcher performs a full source compilation.
`Build-Reconstruction.ps1 -Incremental` reuses objects only after checking their
compiler/source/header/flag context and object hashes.

The build extracts exactly 19 approved original CRT members, links with
`/NODEFAULTLIB`, verifies the x86 PE output and prepares a separate playable
folder. It preserves the existing `game` folder and existing rebuilt score saves.
`build/build-report.json` and `build/logs` retain the result and compiler output.
The compiler processes run serially in a Windows job on one CPU, capped at 35%
of one CPU and 512 MiB job memory. Windows has no equivalent to the Linux
`MemoryHigh` reclaim setting; that Linux soft limit is not claimed here.

VC4 link 3.00 fails during Pass1 on this Windows 11 host without a compatibility
layer. The script sets `WIN7RTM` only for the linker child; compilation keeps its
audited context and no compatibility registry setting is changed. All 270
authored functions in the native objects match across 53 compiler closures.
The native image's code/data/imports/relocations match the tested Wine build;
only PE/resource build timestamps differ. See [the build record](research/CURRENT_VC4_BUILD.md).

To export a package from this prepared Linux workspace, first place the game,
assets and chosen `ddraw.dll`/`ddraw.ini` beside the destination, then run:

```bash
scripts/repo-python scripts/export-windows-build.py /path/to/package/reconstruction
```

The exporter requires the current verified build and pinned local toolchain.
It copies source and build inputs, with no precompiled game-function objects.
Tools and original assets remain local and are not published in Git.

## Windowed Windows play

The original game requests a 640×480 eight-bit exclusive DirectDraw mode.
The local Windows setup uses [cnc-ddraw v7.1](https://github.com/FunkyFr3sh/cnc-ddraw/releases/tag/v7.1.0.0)
for a 960×720, 4:3 GDI window with `windowed=true`, `fullscreen=false` and
`nonexclusive=true`. `maxfps=60` limits presentation; `maxgameticks=-1` leaves
game tick pacing to the reconstructed game. The EXE is unchanged by the wrapper.

`Ctrl+Tab` releases the cursor; clicking inside captures it again. The first
click can acquire capture, so a second click may be needed for the game action.
`Alt+Enter` toggles windowed/borderless output in this profile. `P` pauses the
game before switching away. The wrapper filters activation messages in windowed
mode, so this setup does not test the original exclusive surface-loss contract.

Original WAV and MIDI files have been restored after the temporary muted setup.
Restart the game to reload any silent buffers. Windows master volume, other
application volumes and the default device are not changed by the package.

## Headless Wine

Use a 640×480, 24-bit Xvfb host screen:

```bash
xvfb-run -a -s '-screen 0 640x480x24' \
  scripts/repo-python scripts/run-windows.py --profile vc40-restored-crt
```

The game requests an eight-bit DirectDraw mode. A 24-bit host screen renders
its palette correctly in the tested Wine environment; eight-bit Xvfb drops
filled glyph/title pixels in both original and rebuilt programs. See
[the display comparison](research/WINDOWS_DISPLAY_DEPTH.md).

For automated runs that must leave a Windows/WSLg desktop and audio alone, use
a private Xvfb display, unset `WAYLAND_DISPLAY`, disable `winewayland.drv`, and
set `PULSE_SINK=dxball_reconstruction_silent`. That sink must be a muted
`module-null-sink`; do not change the default sink or use the host display for
`xdotool` input. Follow [background execution](agent/BACKGROUND_PLAY.md) and the
serial resource limits in [AGENTS.md](../AGENTS.md).
The [playability record](research/PLAYABILITY_20261031.md) separates these checks
from manual Windows feedback and the historical focus failure.

## Linux SDL3 build

`ports/sdl3` builds a native 64-bit Linux game from the same recovered source.
The root project and its compiler recipes are unchanged. The port replaces only
the Win32, DirectDraw, DirectSound and WinMM import bindings that
`windows_adapter.c` supplies on Windows:

| File | Replaces |
| --- | --- |
| [sdl3_adapter.c](../src/sdl3_adapter.c) | Window, message queue, input, files, local/global memory, file mapping, clock, instance lock |
| [sdl3_draw.c](../src/sdl3_draw.c) | DirectDraw 1: 8-bit surfaces, Blt/BltFast, colour keys, palettes, flip chain |
| [sdl3_sound.c](../src/sdl3_sound.c) | DirectSound 1: static PCM buffers mixed into one SDL3 stream |
| [sdl3_midi.c](../src/sdl3_midi.c), [sdl3_fluidsynth.c](../src/sdl3_fluidsynth.c) | WinMM MIDI streams, rendered by FluidSynth |
| [sdl3_entry.c](../src/sdl3_entry.c) | `main`: binds the services and runs the original `WinMain` loop |

Requirements: CMake 3.20+, Ninja, GCC or Clang, SDL3 3.2+ and Python 3.11+.
FluidSynth with a General MIDI soundfont is optional; without it the music is
silent. The game uses `$DXBALL_SOUNDFONT`, or else the first soundfont it finds
among `/usr/share/soundfonts/FluidR3_GM.sf2`, `default.sf2` and the
`/usr/share/sounds/sf2` equivalents.

```bash
cmake -S ports/sdl3 -B build/sdl3 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/sdl3 --parallel 1
scripts/repo-python --stdlib scripts/run-linux.py
```

`run-linux.py` checks the assets in `original/` against
[assets.csv](../config/assets.csv) (English v1.07). It copies them to
`build/runtime/linux-sdl3/` with the lowercase names the game opens, then
starts `build/sdl3/dxball` there. Use `--assets DIR` for another folder.
`--allow-unverified` accepts other asset sets, such as v1.09, with a
warning. Rankings and edited boards in the runtime folder are kept between
runs unless you pass `--fresh`.

The window opens at 2× scale when the display has room for it, and it can be
resized with letterboxing. `Alt+Enter` toggles fullscreen. The mouse is captured
in relative mode while the window has focus, so the paddle keeps working when
the pointer reaches the edge on Wayland. `P` pauses and `Esc` leaves the current
mode, as in the original. Flip and WaitForVerticalBlank follow a fixed 60 Hz
clock; the original readme calls 60 FPS "the standard" speed. Surfaces are never
lost, so the unresolved exclusive-mode surface recovery does not apply to this
build.

### Tests

```bash
ctest --test-dir build/sdl3 --output-on-failure
```

Unit tests cover the layouts and COM slot indices the adapter relies on, and
check that C++ virtual calls reach C vtable slots. They also cover key and
message translation, files, memory, the instance lock, DirectDraw pixel results,
DirectSound mixing, and MIDI timing, tempo and callbacks. With FluidSynth they
also check that a note produces sound.
`dxball_test_sdl3_game` plays a headless session: splash, menu, a game with a
natural life loss, pause, resume and the return to the menu. It checks the
display mode, pause flag, score and lives that the game reports, as well as the
presented frames. It reads assets from `original/` by default, or from
`-DDXBALL_TEST_ASSETS=DIR`, and is skipped when they are absent.
The session is driven by `DXBALL_TEST_SCRIPT`, for example
`60:key=Escape;220:click;600:shot=game.bmp;900:quit`, where each number counts
display refreshes.

### 64-bit notes

Handles and pointers follow the host ABI. Game integers stay 32-bit. Every file
format is parsed with fixed-width fields. Callers still pass the i386 size
literals (108 for surface descriptions, 20 for sound buffer descriptions, 64
for MIDI headers), which are smaller than the LP64 records. The emulation never
validates or copies by those fields. The sanitizer build is clean:

```bash
F="-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer"
cmake -S ports/sdl3 -B build/sdl3-asan -G Ninja -DCMAKE_C_FLAGS="$F" \
  -DCMAKE_CXX_FLAGS="$F" -DCMAKE_EXE_LINKER_FLAGS="$F" -DCMAKE_SHARED_LINKER_FLAGS="$F"
```

`vptr` is excluded because the recovered C++ owners call DirectDraw through
virtual projections of C-built COM objects, by design. Without x87 (aarch64),
`balls.cpp` uses native floating point, so paddle-rebound angles at exact
threshold values can differ from the original. `long double` is also wider there.
Board index 50 is reached only after the final level, when the game is already
ending. It copies across adjacent globals, as on i386, but the bytes differ
because the LP64 explosion list is larger.

## Local delivery

`build/delivery/dxball-v1.07-vc40-reconstructed.zip` contains the tested EXE,
48 supplied assets and extraction instructions. Extract the whole folder and
run `dxball.exe`. Its adjacent manifest records all 50 members and validation.
Original assets and local build products are supplied separately from Git.

## Other builds

| Profile | Build | Launch |
| --- | --- | --- |
| VC4 with compiler CRT | `scripts/repo-python scripts/build-legacy.py --game-only` | `scripts/repo-python scripts/run-windows.py --profile vc40` |
| MinGW i686 | `scripts/repo-python scripts/build-windows.py` | `scripts/repo-python scripts/run-windows.py --profile windows-i686` |

The default `build-legacy.py` command also builds the two VC4 asset inspectors.
The MinGW game uses `dxball.exe` and `libdxball_core.dll`. Its compiler runtime
and pthread support are linked into the core DLL. DirectDraw/DirectSound imports
come from the platform SDK; the VC4 build generates its import libraries through
`scripts/directx_imports.py`.

To build the portable core and inspectors with CMake, Ninja and GCC or Clang:

```bash
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native --parallel 1
```

The native library and Windows game share recovered source. Compiler owners and
per-source flags are described in [matching](MATCHING.md); the
[source map](../src/README.md) identifies the code behind each subsystem.
