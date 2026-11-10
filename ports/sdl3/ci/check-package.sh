#!/bin/bash
# Start a packaged game without game data, from an empty folder: it must load
# every library it ships with and stop with the missing-data guidance.
# Usage: check-package.sh [LAUNCHER] GAME
set -uo pipefail
empty=$(mktemp -d)
output=$(cd "$empty" && SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy "$@" 2>&1)
status=$?
if [ "$status" -ne 1 ] || ! grep -q 'game data .* not found' <<<"$output"; then
    echo "package check failed (exit $status):"
    echo "$output"
    exit 1
fi
echo "package check OK: $*"
