"""Run the production RAW CLI, or generate small Bayer DNGs for manual QA."""

import argparse
from array import array
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile


WIDTH, HEIGHT = 512, 384
TIMED_FRAMES = (
    ("A_skip.dng", 25), ("B_late.dng", 40), ("frame10.dng", 20),
    ("frame2.dng", 20), ("Y_early.dng", 10), ("Z_skip.dng", 0),
)
MIXED_FRAMES = (
    ("frame10.dng", 10), ("frame2.dng", 30), ("frame3.dng", None),
    ("nested/frame2.dng", 40),
)


def u32(value):
    return 4, 1, struct.pack("<I", value)


def u16(*values):
    return 3, len(values), struct.pack(f"<{len(values)}H", *values)


def u8(*values):
    return 1, len(values), bytes(values)


def text(value):
    data = value.encode("ascii") + b"\0"
    return 2, len(data), data


def rationals(*numerators, denominator=1, signed=False):
    fmt = "<ii" if signed else "<II"
    return (10 if signed else 5), len(numerators), b"".join(
        struct.pack(fmt, n, denominator) for n in numerators
    )


def pack_ifd(tags, offset):
    entries = bytearray(struct.pack("<H", len(tags)))
    payload_offset = offset + 2 + len(tags) * 12 + 4
    payload = bytearray()
    for tag, (kind, count, data) in sorted(tags.items()):
        entries.extend(struct.pack("<HHI", tag, kind, count))
        if len(data) <= 4:
            entries.extend(data.ljust(4, b"\0"))
        else:
            entries.extend(struct.pack("<I", payload_offset + len(payload)))
            payload.extend(data)
            if len(payload) % 2:
                payload.append(0)
    return entries + bytes(4) + payload


def star_field(shift):
    sky = array("H", (1100 + x // 5 for x in range(WIDTH)))
    ground = array("H", (350 + int(85 * math.cos(x / 13)) for x in range(WIDTH)))
    pixels = sky * 280 + ground * (HEIGHT - 280)
    rng = random.Random(781)
    for y in range(19, 265, 29):
        for x in range(18, 494, 37):
            sx = x + rng.randint(-7, 7) + shift
            sy = y + rng.randint(-7, 7) + shift // 2
            peak = rng.randint(3500, 10000)
            sigma = rng.uniform(1.2, 2.0)
            for dy in range(-6, 7):
                for dx in range(-6, 7):
                    pos = (sy + dy) * WIDTH + sx + dx
                    pixels[pos] += int(peak * math.exp(-(dx * dx + dy * dy) / (2 * sigma**2)))
    if sys.byteorder == "big":
        pixels.byteswap()
    return pixels.tobytes()


def write_dng(path, capture_second, shift, orientation):
    # Single uncompressed little-endian TIFF strip with an RGGB mosaic.
    tags = {
        254: u32(0), 256: u32(WIDTH), 257: u32(HEIGHT), 258: u16(16),
        259: u16(1), 262: u16(32803), 271: text("Nebulens"),
        272: text("Synthetic Bayer"), 273: u32(0), 274: u16(orientation),
        277: u16(1), 278: u32(HEIGHT), 279: u32(WIDTH * HEIGHT * 2),
        284: u16(1), 33421: u16(2, 2), 33422: u8(0, 1, 1, 2), 34665: u32(0),
        50706: u8(1, 4, 0, 0), 50707: u8(1, 1, 0, 0),
        50708: text("Nebulens Synthetic Bayer"), 50710: u8(0, 1, 2),
        50711: u16(1), 50713: u16(1, 1), 50714: u16(64), 50717: u32(16383),
        50721: rationals(1, 0, 0, 0, 1, 0, 0, 0, 1, signed=True),
        50728: rationals(1, 1, 1), 50778: u16(21),
    }
    exif = {33434: rationals(10), 33437: rationals(28, denominator=10), 34855: u16(800)}
    if capture_second is not None:
        timestamp = text(f"2026:08:31 21:00:{capture_second:02d}")
        tags[306] = exif[36867] = exif[36868] = timestamp

    exif_offset = 8 + len(pack_ifd(tags, 8))
    exif_ifd = pack_ifd(exif, exif_offset)
    tags[34665] = u32(exif_offset)
    tags[273] = u32(exif_offset + len(exif_ifd))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"II" + struct.pack("<HI", 42, 8) + pack_ifd(tags, 8) +
                     exif_ifd + star_field(shift))


def write_fixtures(root, orientation=1):
    for group, frames in (("timed", TIMED_FRAMES), ("mixed", MIXED_FRAMES)):
        for shift, (name, second) in enumerate(frames):
            write_dng(root / group / name, second, shift, orientation)


def run_cli(runner, source, output, *args):
    return subprocess.run(
        [str(runner), "-i", str(source), "--output", str(output), *args],
        capture_output=True, encoding="utf-8", errors="replace", timeout=90,
    )


def load_report(runner, source, output, *args, expect_success=True):
    result = run_cli(runner, source, output, *args)
    assert result.returncode == (0 if expect_success else 5), (args, result.stdout, result.stderr)
    report = json.loads((output / "pipeline-report.json").read_text(encoding="utf-8"))
    assert report["success"] == expect_success, report["error"]
    return report


def check_order(report, source, expected_names, reference_index, timelapse=False):
    frames = report["frameQuality"]
    assert report["selectedFrames"] == len(frames) == len(expected_names), frames
    assert report["referenceIndex"] == reference_index, report["referenceIndex"]
    assert report["referencePath"] == frames[reference_index]["sourcePath"], frames
    assert report["referenceFile"] == frames[reference_index]["file"], frames
    output_names = []
    for index, (frame, name) in enumerate(zip(frames, expected_names)):
        path = source / name
        assert frame["index"] == index, frame
        assert frame["file"] == path.name, frame
        assert Path(frame["sourcePath"]) == path, frames
        if timelapse:
            assert frame["outputNumber"] == index + 1, frame
            output_names.append(f"{index + 1:04d}_{path.stem}_denoised.tiff")
        else:
            assert "outputNumber" not in frame, frame
    if timelapse:
        actual_names = sorted(p.name for p in Path(report["outputFile"]).glob("*.tiff"))
        assert actual_names == output_names, (actual_names, output_names)
        assert report["processedFrames"] == len(frames), report["processedFrames"]
        assert report["previewMode"] == "automatic-range", report["previewMode"]
        assert report["previewSaved"], report["previewFile"]
        assert not report["stretchEnabled"], report["stretchEnabled"]
        assert not report["automaticLensChromaticAberrationEnabled"], report


def run_tests(runner, root):
    write_fixtures(root)
    timed, mixed = root / "timed", root / "mixed"
    rejected = root / "rejected"
    for options in (
        ("--stretch", "--star-reduce-strength", "80"),
        ("--star-defringe-strength", "60"), ("--denoise-strength", "30"),
        ("--dehaze-strength", "40"), ("--restore-modified-camera-color",),
        ("--modified-camera-gray-point", "0.5,0.5"),
        ("--modified-camera-color-strength", "100"), ("--temperature", "0"),
        ("--exposure", "0.5"), ("--sharpening", "20"),
        ("--method", "average"), ("--no-auto-lens-ca",), ("--sky-ground",),
        ("--reference-index", "0"), ("--reference-index", "-2"),
        ("--reference-index", "invalid"),
    ):
        result = run_cli(runner, timed, rejected, "--timelapse", *options)
        assert result.returncode == 2, (options, result.stdout, result.stderr)
        for option in options:
            if option.startswith("--"):
                assert option in result.stderr, (option, result.stderr)
        assert not rejected.exists(), result.stdout

    slice_args = ("--start-index", "1", "--limit", "4")
    ordered_slice = ("Y_early.dng", "frame2.dng", "frame10.dng", "B_late.dng")
    report = load_report(runner, timed, root / "budget-out", "--timelapse",
                         "--memory-budget-mib", "1", *slice_args, expect_success=False)
    assert tuple(frame["file"] for frame in report["frameQuality"]) == ordered_slice
    assert all("outputNumber" not in frame for frame in report["frameQuality"])
    assert report["processedFrames"] == 0 and not report["outputFile"]

    report = load_report(runner, timed, root / "timed-out", "--timelapse",
                         "--timelapse-window", "3", "--timelapse-no-ground",
                         "--reference-index", "-1", "--no-photometric-normalization",
                         *slice_args)
    check_order(report, timed, ordered_slice, 2, timelapse=True)
    assert report["requestedReferenceIndex"] == -1
    assert not report["photometricNormalizationEnabled"]

    report = load_report(runner, mixed, root / "mixed-out", "--timelapse",
                         "--sky-ground-feather", "4")
    check_order(report, mixed, ("frame2.dng", "nested/frame2.dng", "frame3.dng",
                               "frame10.dng"), 2, timelapse=True)
    assert report["skyGroundFeatherRadius"] == 4

    report = load_report(runner, timed, root / "trail-out", "--star-trail",
                         "--star-trail-no-ground-protection", *slice_args)
    check_order(report, timed, ordered_slice, 0)

    report = load_report(runner, timed, root / "stack-out", "--reference-index", "1",
                         "--method", "average", "--no-quality-rejection", "--stretch",
                         *slice_args)
    check_order(report, timed, ("B_late.dng", "frame10.dng", "frame2.dng", "Y_early.dng"), 1)
    assert report["requestedReferenceIndex"] == 1
    assert report["previewMode"] == "fixed-full-range", report["previewMode"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runner", nargs="?", type=Path, help="StarProcessorPipelineRunner binary")
    parser.add_argument("--write-fixtures", type=Path, metavar="DIRECTORY")
    parser.add_argument("--orientation", type=int, choices=(1, 6, 8), default=1)
    args = parser.parse_args()
    if (args.runner is None) == (args.write_fixtures is None):
        parser.error("specify the runner or --write-fixtures")
    if args.write_fixtures is not None:
        write_fixtures(args.write_fixtures, args.orientation)
        return
    with tempfile.TemporaryDirectory(prefix="nebulens-timelapse-") as temp:
        run_tests(args.runner.resolve(), Path(temp).resolve())
    print("Timelapse CLI tests passed")


if __name__ == "__main__":
    main()
