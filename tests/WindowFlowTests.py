"""Generate deterministic DNG fixtures and run NebulensWindowTests offscreen."""

import argparse
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from TimelapseCliTests import HEIGHT, WIDTH, write_dng


def write_ground_detail_fixture(path, capture_second, shift):
    write_dng(path, capture_second, shift, orientation=1)
    # The writer ends with a single uncompressed Bayer strip. Add fixed,
    # neutral texture well below the horizon while leaving the stars intact.
    row = struct.pack(f"<{WIDTH}H", *(560 if x // 12 % 2 else 320 for x in range(WIDTH)))
    texture = row * (HEIGHT // 5)
    with path.open("r+b") as fixture:
        fixture.seek(-len(texture), os.SEEK_END)
        fixture.write(texture)


def write_fixtures(root):
    source, replacement = (root / "source.dng", root / "replacement.dng")
    write_dng(source, capture_second=10, shift=0, orientation=1)
    write_dng(replacement, capture_second=10, shift=5, orientation=1)
    for path in (source, replacement):
        # A clear must invalidate the result even if the next import has the
        # same path, size, and filesystem timestamp as the original RAW.
        os.utime(path, (1_700_000_000, 1_700_000_000))
    for name, second, shift in (("late", 35, 2), ("frame10", 15, 1), ("frame2", 15, 0)):
        write_dng(root / f"{name}.dng", second, shift, orientation=1)
    for index in range(2):
        write_ground_detail_fixture(root / f"ridge{index}.dng", 20 + index, index)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("window_tests", type=Path, help="path to the NebulensWindowTests binary")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--reopen", action="store_true")
    mode.add_argument("--example", nargs=2, type=Path, metavar=("ORDER", "BASELINE_TIFF"),
                      help="local opt-in: compare GUI frame use and RGB16 delivery with the example and baseline TIFF")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="nebulens-window-flow-") as temp:
        root = Path(temp).resolve()
        home = root / "home"
        home.mkdir()
        write_fixtures(root)
        # An alternate home must be in place before macOS loads CFPreferences.
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen", HOME=str(home),
                   CFFIXED_USER_HOME=str(home), USERPROFILE=str(home),
                   XDG_CONFIG_HOME=str(home / "config"), XDG_CACHE_HOME=str(home / "cache"),
                   XDG_DATA_HOME=str(home / "data"))
        try:
            phases = [["--order-write"], ["--order-read"]] if args.reopen else [[]]
            if args.example:
                phases = [["--replay-example", *(str(p.resolve()) for p in args.example)]]
            for extra in phases:
                # Allow both source and receipt previews to finish around the 600-second replay limit.
                subprocess.run([str(args.window_tests.resolve()), str(root), *extra],
                               env=env, check=True, timeout=720 if args.example else 120)
        except subprocess.TimeoutExpired:
            parser.exit(1, "NebulensWindowTests timed out\n")
        except subprocess.CalledProcessError as error:
            parser.exit(1, f"NebulensWindowTests exited with status {error.returncode}\n")
        except OSError as error:
            parser.exit(1, f"Could not start NebulensWindowTests: {error}\n")


if __name__ == "__main__":
    main()
