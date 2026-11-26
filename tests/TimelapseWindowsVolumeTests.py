"""Smoke-test the Windows timelapse CLI with an optional writable UNC fixture."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from TimelapseCliTests import write_dng


def run_timelapse(runner, source, scratch, output):
    result = subprocess.run(
        [str(runner), "-i", str(source), "--output", str(output),
         "--timelapse", "--timelapse-window", "3", "--timelapse-no-ground"],
        env=dict(os.environ, TMP=str(scratch), TEMP=str(scratch)),
        capture_output=True, encoding="utf-8", errors="replace", timeout=90,
    )
    assert result.returncode == 0, (scratch, output, result.stdout, result.stderr)
    report = json.loads((output / "pipeline-report.json").read_text(encoding="utf-8"))
    assert report["success"] and report["processedFrames"] == 3, report["error"]
    assert len(list(Path(report["outputFile"]).glob("*.tiff"))) == 3, report["outputFile"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runner", type=Path, help="StarProcessorPipelineRunner.exe")
    parser.add_argument("--unc-dir", type=Path, help="Existing writable UNC directory")
    parser.add_argument("--mapped-dir", type=Path, help="Drive-letter alias for --unc-dir")
    args = parser.parse_args()
    if os.name != "nt":
        print("SKIP: requires Windows")
        return
    if args.unc_dir is not None and not (
            args.unc_dir.is_absolute() and args.unc_dir.drive.startswith("\\\\")):
        parser.error("--unc-dir must be an absolute UNC path")
    if args.mapped_dir is not None and not (
            args.unc_dir is not None and args.mapped_dir.is_absolute()
            and len(args.mapped_dir.drive) == 2):
        parser.error("--mapped-dir requires --unc-dir and an absolute drive-letter path")

    runner = args.runner.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="nebulens-win-") as temp:
        local = Path(temp)
        source = local / "raw"
        for index in range(3):
            write_dng(source / f"frame{index}.dng", 10 + index, index, 1)
        run_timelapse(runner, source, local, local / "out")
        if args.unc_dir is None:
            print("PASS: local timelapse; SKIP: UNC cases require --unc-dir")
            return

        with tempfile.TemporaryDirectory(prefix="nebulens-win-", dir=args.unc_dir) as shared:
            unc = Path(shared)
            run_timelapse(runner, source, local, unc / "unc-out")
            run_timelapse(runner, source, unc, unc / "same-share-out")
            if args.mapped_dir is not None:
                mapped = args.mapped_dir / unc.name
                assert mapped.is_dir(), "--mapped-dir must point to the same directory as --unc-dir"
                run_timelapse(runner, source, mapped, unc / "mapped-out")
            else:
                print("SKIP: mapped-drive case requires --mapped-dir")
    print("PASS: Windows timelapse volume smoke tests")


if __name__ == "__main__":
    main()
