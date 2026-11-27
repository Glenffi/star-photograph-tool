#!/usr/bin/env python3
"""Generate an offline, 1:1 sky/ground comparison and Nebulens print order."""

import argparse
from contextlib import contextmanager
from dataclasses import asdict
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

try:
    import numpy as np
    from PIL import Image
except ModuleNotFoundError as error:
    sys.exit(f"Python needs numpy and Pillow installed locally ({error}).")

from pipeline import ROLES, REPORT_SCHEMA, read_run, require, run_pipeline, verify_runs
from raster import (ComparisonError, Rect, fit_display, horizon_roi, intersect, map_display,
                    montage, read_binary_mask, read_tiff_crop, save_png)


IMAGES = {"single": "ridge-single.png", "stack": "ridge-stack.png",
          "separated": "ridge-separated.png", "comparison": "ridge-comparison.png"}
MASK_NAME = "ridge-mask.png"
MANIFEST_NAME = "comparison.json"
MANIFEST_SCHEMA = "nebulens.ridge-comparison"
ADJUSTMENTS = ("temperature", "tint", "contrast", "highlights", "shadows", "whites",
               "blacks", "vibrance", "saturation", "sharpening")


def write_json(path, contents):
    with path.open("x", encoding="utf-8") as stream:
        json.dump(contents, stream, ensure_ascii=False, allow_nan=False, indent=2)
        stream.write("\n")


def order_parameters(report):
    return {
        "stackMethod": report["method"],
        "kappa": report["kappa"],
        "autoReject": report["autoQualityRejectionEnabled"],
        "photometric": report["photometricNormalizationEnabled"],
        "skyGround": report["skyGroundSeparationEnabled"],
        "skyGroundMode": "user-mask" if report["skyGroundMode"] == "user-mask" else "auto",
        "feather": report["skyGroundFeatherRadius"],
        "groundMethod": report["groundStackMethod"],
        "groundDetail": report["groundDetailStrength"],
        "outputFormat": "tiff16",
        "noiseReductionEnabled": report["denoiseStrength"] > 0,
        "noiseReductionStrength": report["denoiseStrength"],
        "dewarpEnabled": report["dehazeStrength"] > 0,
        "dewarpStrength": report["dehazeStrength"],
        "modifiedCameraColorEnabled": report["modifiedCameraColorEnabled"],
        "modifiedCameraColorStrength": report["modifiedCameraColorStrength"],
        "modifiedCameraColorMode": "auto",
        "stretchEnabled": report["stretchEnabled"],
        "starReduceEnabled": report["starReduceStrength"] > 0,
        "starReduceStrength": report["starReduceStrength"],
        "starDefringeEnabled": report["starDefringeStrength"] > 0,
        "starDefringeStrength": report["starDefringeStrength"],
        "exposureTenths": round(report["exposureEv"] * 10),
        **{key: report[key] for key in ADJUSTMENTS},
    }


def make_order(stack, separated, output, raw_root, user_mask):
    root = raw_root if raw_root is not None else stack.source

    def order_relative(path):
        return Path(os.path.relpath(root / path, start=output)).as_posix()

    reference = order_relative(stack.reference)
    order = {
        "format": "NebulensPrintOrder", "version": 1, "scene": "sky-ground",
        "automaticStill": False, "lights": [order_relative(path) for path in stack.selected],
        "excluded": [],
        "reference": reference, "selected": reference,
        "params": order_parameters(separated.report),
        "outputDirectory": "results", "comparisonManifest": MANIFEST_NAME,
    }
    if user_mask:
        order["userMask"] = MASK_NAME
    return order


def original_mask(separated, path):
    guided = separated.report["skyGroundMaskSource"] == "user-mask"
    if not guided:
        require(path is None, "--user-mask was supplied but the separated report used an automatic mask")
        return None
    require(path is not None, "separated used a user mask: pass the original binary image as --user-mask")
    require(not path.samefile(separated.mask), "--user-mask must not be the Runner's feathered diagnostic")
    reported = Path(os.path.abspath(separated.report["skyGroundMask"]))
    require(path == reported or path.resolve(strict=True) == reported,
            "--user-mask must match the original skyGroundMask path in the separated report")
    return read_binary_mask(path)


def render(runs, output, args, source=None):
    single, stack, separated = runs
    verify_runs(*runs, start=args.start_index, count=args.limit, reference=args.reference_index,
                reference_file=args.reference_file, source=source)
    user_mask = original_mask(separated, args.user_mask)
    report = separated.report
    common = intersect([run.bounds for run in runs])
    require(common.width > 0 and common.height > 0, "the Runner TIFFs have no shared reference extent")
    with Image.open(separated.mask, formats=("PNG",)) as diagnostic:
        require(diagnostic.mode == "L", "expected an 8-bit Runner grayscale diagnostic mask")
        frame = Rect(0, 0, *diagnostic.size)
        require(all(frame.contains(run.bounds) for run in runs),
                "Runner crop offsets and dimensions exceed the reference mask extent")
        if not single.report["automaticLensChromaticAberrationApplied"]:
            require(single.bounds == frame, "the uncorrected single TIFF must cover the reference extent")
        if args.roi is None:
            roi = horizon_roi(np.asarray(diagnostic), common, report["skyGroundFeatherRadius"])
            roi_source = "user-mask-horizon" if user_mask is not None else "automatic-mask-horizon"
        else:
            diagnostic.verify()
            roi = args.roi
            roi_source = "explicit"
    require(common.contains(roi), f"--roi must be fully inside the shared reference extent: {common}")
    require(max(roi.width, roi.height) <= 4096 and roi.width * roi.height <= 4 * 1024 * 1024,
            "--roi exceeds the app's 4096px side or 4 Mi-pixel crop limit")

    reference_crop = read_tiff_crop(single.tiff, single.bounds, roi)
    display = fit_display(reference_crop)
    images = [Image.fromarray(map_display(reference_crop, display))]
    del reference_crop
    for run in (stack, separated):
        images.append(Image.fromarray(map_display(read_tiff_crop(run.tiff, run.bounds, roi), display)))
    for role, image in zip(ROLES, images):
        save_png(image, output / IMAGES[role])
    combined, panels = montage(images, guided=user_mask is not None)
    save_png(combined, output / IMAGES["comparison"])

    manifest = {
        "schema": MANIFEST_SCHEMA, "version": 1, "title": args.title,
        "layout": "three-column", "images": IMAGES,
        "panels": {role: asdict(panel) for role, panel in zip(ROLES, panels)},
        "roi": asdict(roi), "roiSource": roi_source, "coordinateSpace": "oriented-reference-raw",
        "reference": stack.reference.as_posix(),
        "lights": [path.as_posix() for path in stack.used],
        "selectedLights": [path.as_posix() for path in stack.selected],
        "display": display,
        "runner": {
            "version": report["toolVersion"], "reportSchemaVersion": REPORT_SCHEMA,
            "startIndex": stack.start, "referenceIndex": stack.reference_index,
            "selectedFrames": len(stack.selected), "processedFrames": len(stack.used),
            "maskSource": report["skyGroundMaskSource"],
            "lensCorrection": {"enabled": report["automaticLensChromaticAberrationEnabled"],
                               "applied": report["automaticLensChromaticAberrationApplied"]},
            "crops": {run.role: asdict(run.bounds) for run in runs},
        },
    }
    if user_mask is not None:
        user_mask.save(output / MASK_NAME, format="PNG")
        manifest["userMask"] = MASK_NAME
    write_json(output / MANIFEST_NAME, manifest)
    write_json(output / args.order_name,
               make_order(stack, separated, output, args.order_raw_root, user_mask is not None))
    print(f"Compared {len(stack.used)}/{len(stack.selected)} frames; ROI {roi.x},{roi.y},{roi.width},{roi.height}")


@contextmanager
def new_output(path):
    path = path.parent.resolve(strict=True) / path.name
    try:
        path.mkdir()
    except FileExistsError as error:
        raise ComparisonError(f"output already exists: {path}") from error
    try:
        yield path
    except BaseException:
        shutil.rmtree(path)
        raise


def parse_roi(text):
    try:
        roi = Rect(*(int(value) for value in text.split(",")))
        if min(roi.x, roi.y) >= 0 and min(roi.width, roi.height) > 0:
            return roi
    except (TypeError, ValueError):
        pass
    raise argparse.ArgumentTypeError("expected X,Y,WIDTH,HEIGHT with nonnegative origin and positive size")


def nonnegative(text):
    value = int(text)
    if value < 0:
        raise argparse.ArgumentTypeError("expected a nonnegative integer")
    return value


def order_name(text):
    if Path(text).name != text or "\\" in text or "\0" in text or Path(text).suffix != ".nebprint":
        raise argparse.ArgumentTypeError("expected a .nebprint basename")
    return text


def absolute(path):
    return Path(os.path.abspath(path))


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("run", help="run the genuine StarProcessorPipelineRunner three times")
    render_only = commands.add_parser("render-only", help="render from three existing Runner directories")
    for command in (run, render_only):
        command.add_argument("--output", type=Path, required=True, help="new bundle directory")
        command.add_argument("--roi", type=parse_roi, metavar="X,Y,W,H",
                             help="zero-based, oriented reference RAW coordinates; default: mask horizon")
        command.add_argument("--title", default="Stack comparison", help="display title in comparison.json")
        command.add_argument("--order-name", type=order_name, default="comparison.nebprint")
        command.add_argument("--order-raw-root", type=absolute, metavar="PATH",
                             help="locate RAW paths under this root when writing the order (no RAWs read)")
        command.add_argument("--user-mask", type=absolute, metavar="FILE",
                             help="original binary mask; required for a user-mask Runner report")
        command.add_argument("--reference-file", help="assert this input-relative RAW path is the reference")
        command.add_argument("--start-index", type=nonnegative, default=0 if command is run else None,
                             help="zero-based slice start (assertion in render-only)")
        command.add_argument("--limit", type=nonnegative, required=command is run,
                             help="selected frame count, before skips (assertion in render-only)")
        command.add_argument("--reference-index", type=nonnegative, required=command is run,
                             help="reference index within the slice (assertion in render-only)")
    run.add_argument("--runner", type=Path, required=True)
    run.add_argument("--input", type=Path, required=True, help="one local fixed-tripod RAW sequence")
    run.add_argument("--memory-budget-mib", type=nonnegative)
    run.add_argument("--keep-intermediates", action="store_true",
                     help="keep TIFFs, masks and unsanitized Runner reports under intermediates/")
    for role in ROLES:
        render_only.add_argument(f"--{role}-dir", type=Path, required=True)

    args = parser.parse_args(argv)
    if args.limit is not None and args.limit < 2:
        parser.error("--limit must be at least 2")
    if args.limit is not None and args.reference_index is not None and args.reference_index >= args.limit:
        parser.error("--reference-index must be within --limit")
    return args


def main(argv=None):
    args = parse_args(argv)
    try:
        with new_output(args.output) as output:
            if args.command == "render-only":
                runs = tuple(read_run(role, getattr(args, f"{role}_dir")) for role in ROLES)
                render(runs, output, args)
            else:
                source = args.input.resolve(strict=True)
                require(source.is_dir(), "--input must be a directory")
                runner = args.runner.resolve(strict=True)
                if args.user_mask is not None:
                    with read_binary_mask(args.user_mask):
                        pass
                with tempfile.TemporaryDirectory(prefix="nebulens-comparison-") as directory:
                    scratch = Path(directory) / "scratch"
                    scratch.mkdir()
                    intermediate = (output if args.keep_intermediates else Path(directory)) / "intermediates"
                    intermediate.mkdir()
                    runs = tuple(run_pipeline(runner, source, intermediate, scratch, args.start_index,
                                              args.limit, args.reference_index, args.memory_budget_mib,
                                              args.user_mask))
                    render(runs, output, args, source=source)
        print(f"Wrote {output / args.order_name} and {output / MANIFEST_NAME}")
        return 0
    except (ComparisonError, OSError, Image.DecompressionBombError) as error:
        print(f"Comparison failed: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
