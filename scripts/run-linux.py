#!/usr/bin/env python3
"""Run the Linux SDL3 game from a working folder of lowercase asset copies.

The game opens lowercase names (mainmenu.pcx, score.dat, ...). Assets are
checked against config/assets.csv (English v1.07) before they are copied.
"""
import argparse
import csv
import hashlib
import os
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]


def manifest():
    with (ROOT / 'config/assets.csv').open() as stream:
        return list(csv.DictReader(stream))


def find_assets(directory, allow_unverified):
    """Map each manifest name to its file in directory, matched without case."""
    present = {path.name.casefold(): path for path in Path(directory).iterdir() if path.is_file()}
    found, problems = {}, []
    for entry in manifest():
        name = entry['filename']
        path = present.get(name.casefold())
        if path is None:
            problems.append('missing ' + name)
            continue
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry['sha256']:
            problems.append('differs from v1.07: ' + name)
        found[name] = path
    if problems and not allow_unverified:
        raise SystemExit('Assets in %s do not match English v1.07 (%d issue(s), first: %s).\n'
                         'Use --allow-unverified to run with them anyway.'
                         % (directory, len(problems), problems[0]))
    for problem in problems:
        print('warning:', problem, file=sys.stderr)
    return found


def prepare(assets, runtime, fresh):
    runtime.mkdir(parents=True, exist_ok=True)
    for name, source in assets.items():
        if Path(name).suffix.lower() == '.exe':
            continue
        destination = runtime / name.lower()
        if destination.is_symlink():
            raise SystemExit('Refusing a runtime symlink: %s' % destination)
        # Keep the player's rankings and edited boards unless asked to reset.
        if destination.exists() and not fresh and destination.name in ('score.dat', 'default.bds'):
            continue
        shutil.copyfile(source, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--assets', default=str(ROOT / 'original'),
                        help='folder with the original game files (default: original/)')
    parser.add_argument('--allow-unverified', action='store_true',
                        help='accept assets that differ from the v1.07 manifest')
    parser.add_argument('--runtime', default=str(ROOT / 'build/runtime/linux-sdl3'),
                        help='working folder for lowercase copies and saves')
    parser.add_argument('--game', default=str(ROOT / 'build/sdl3/dxball'),
                        help='Linux game executable')
    parser.add_argument('--fresh', action='store_true',
                        help='replace saved score.dat and default.bds with the originals')
    parser.add_argument('--prepare-only', action='store_true',
                        help='prepare the working folder without launching')
    args = parser.parse_args()
    runtime = Path(args.runtime).resolve()
    prepare(find_assets(args.assets, args.allow_unverified), runtime, args.fresh)
    print('Linux game directory:', runtime, flush=True)
    if args.prepare_only:
        return
    game = str(Path(args.game).resolve())
    os.chdir(runtime)
    os.execv(game, [game])


if __name__ == '__main__':
    main()
