#!/bin/bash
# Add the player instructions and licenses to a release folder.
# Usage, from the repository root: add-notes.sh FOLDER
set -euo pipefail
cp ports/sdl3/dist/README-SDL3.txt ports/sdl3/dist/THIRD-PARTY.txt "$1/"
cp LICENSE "$1/LICENSE.txt"
