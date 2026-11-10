"""Headless Linux game session: splash, menu, play, pause, resume, back to menu.

Usage: smoke_game.py GAME ASSETS. Exits 77 (CTest skip) when ASSETS is absent.
Input goes through the normal SDL event path (DXBALL_TEST_SCRIPT); checks use
the 8-bit frames the game presented.
"""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ('60:key=Escape;200:shot=menu.bmp;220:click;400:shot=game.bmp;420:click;'
          '480:move=-100;540:move=200;900:key=P;960:shot=paused.bmp;1020:key=P;'
          '1200:shot=resumed.bmp;1220:key=Escape;1400:shot=back.bmp;1450:quit')


def frame(path):
    """Return (pixel digest, share of visibly lit pixels) of an 8-bit BMP."""
    data = path.read_bytes()
    offset, = struct.unpack_from('<I', data, 10)
    width, height = struct.unpack_from('<ii', data, 18)
    bits, = struct.unpack_from('<H', data, 28)
    if (width, abs(height), bits) != (640, 480, 8):
        raise AssertionError('%s: unexpected format %dx%d/%d' % (path.name, width, height, bits))
    palette = data[54:54 + 1024]
    pixels = data[offset:offset + 640 * 480]
    bright = [max(palette[4 * i:4 * i + 3]) > 24 for i in range(256)]
    lit = sum(bright[value] for value in pixels) / len(pixels)
    return hashlib.sha256(pixels).hexdigest(), lit


def main():
    game, assets = Path(sys.argv[1]), Path(sys.argv[2])
    if not assets.is_dir():
        print('skipped: no game assets at', assets)
        return 77
    with tempfile.TemporaryDirectory(prefix='dxball-smoke-') as temporary:
        runtime = Path(temporary) / 'game'
        subprocess.run([sys.executable, '-I', str(ROOT / 'scripts/run-linux.py'), '--assets', str(assets),
                        '--allow-unverified', '--runtime', str(runtime), '--prepare-only'],
                       check=True, stderr=subprocess.DEVNULL)
        env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
                   XDG_RUNTIME_DIR=temporary, DXBALL_TEST_SCRIPT=SCRIPT)
        result = subprocess.run([str(game)], cwd=runtime, env=env, timeout=120,
                                stdout=subprocess.PIPE, text=True)
        failures = []
        if result.returncode != 0:
            failures.append('game exited with %d' % result.returncode)
        states = {}
        for line in result.stdout.splitlines():
            if line.startswith('dxball-state '):
                fields = dict(item.split('=', 1) for item in line.split()[1:])
                states[fields['shot'][:-4]] = fields
                print(line)
        # (display mode, paused) the game itself reports at each shot
        for name, mode, paused in (('menu', 0, 0), ('game', 1, 0), ('paused', 1, 1),
                                   ('resumed', 1, 0), ('back', 0, 0)):
            state = states.get(name)
            if state is None:
                failures.append('no state at %s' % name)
            elif (int(state['mode']), int(state['paused'])) != (mode, paused):
                failures.append('%s: mode %s paused %s, expected %d/%d'
                                % (name, state['mode'], state['paused'], mode, paused))
        frames = {}
        for name in ('menu', 'game', 'paused', 'resumed', 'back'):
            path = runtime / (name + '.bmp')
            if not path.exists():
                failures.append('no %s frame' % name)
                continue
            frames[name] = frame(path)
            print('%-8s lit %.3f  %s' % (name, frames[name][1], frames[name][0][:16]))
            if frames[name][1] < 0.05:
                failures.append('%s frame is nearly black' % name)
        if len(frames) == 5 and frames['paused'][0] == frames['resumed'][0]:
            failures.append('pause/resume did not change the presented frame')
        if (runtime / 'score.dat').stat().st_size != 660:
            failures.append('score.dat is not 15 x 44-byte records')
    for failure in failures:
        print('FAIL:', failure)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
