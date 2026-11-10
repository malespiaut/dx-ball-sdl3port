DX-Ball (SDL3 port)
===================

A native build of DX-Ball v1.07 for Linux, macOS and Windows, compiled from
the reconstructed source code: https://github.com/malespiaut/dx-ball-sdl3port

This package contains the game program only. The game data is not included.


1. Add the game data
--------------------

Copy every file of the English DX-Ball v1.07 game (INTRO.PCX, MAINMENU.PCX,
the .WAV, .MDS and .BDS files, SCORE.DAT, ...) into this folder, next to the
dxball program. Do not copy DXBALL.EXE; it is not needed.

Linux opens the files by lowercase name. If they are uppercase, rename them
from this folder with:

    for f in *.[A-Z][A-Z][A-Z]; do mv -n "$f" "$(printf %s "$f" | tr A-Z a-z)"; done


2. Music (optional)
-------------------

The music is MIDI, played through FluidSynth, which needs a General MIDI
SoundFont. Put one in this folder named soundfont.sf2, or set the
DXBALL_SOUNDFONT environment variable to its path. Without a SoundFont the
game plays sound effects but no music.

Free SoundFonts include GeneralUser GS and FluidR3_GM. On Linux, music also
needs FluidSynth installed (libfluidsynth3 or the fluidsynth package); the game
runs without it. It also finds FluidR3_GM from the fluid-soundfont-gm package.


3. Start the game
-----------------

Windows: double-click dxball.exe.

macOS (Apple Silicon, macOS 15 or newer): in Terminal, run

    xattr -dr com.apple.quarantine /path/to/this/folder
    /path/to/this/folder/dxball

The first command lets macOS start a program that was downloaded and is not
signed by an Apple developer account.

Linux: run ./dxball from this folder. It needs SDL3's usual X11 or Wayland
and audio libraries, which desktop systems already have.

Alt+Enter switches between a window and full screen. Saves (SCORE.DAT,
DEFAULT.BDS) are written in this folder.


DX-Ball was created by Michael P. Welch, with 3D graphics by Seumas McNally.
See LICENSE.txt for the reconstruction's license and THIRD-PARTY.txt for the
included libraries.
