"""Run StarProcessorPipelineRunner and verify the production report contract."""

from dataclasses import dataclass
import json
import math
import os
from pathlib import Path
import subprocess

from raster import ComparisonError, Rect


ROLES = ("single", "stack", "separated")
REPORT_SCHEMA = 20
BASELINE = {
    "success": True, "error": "", "timelapseMode": False, "starTrailMode": False,
    "deepSkyCalibrationEnabled": False, "autoQualityRejectionEnabled": False,
    "qualityRejectedFrames": 0, "photometricNormalizationEnabled": False,
    "automaticLensChromaticAberrationEnabled": True,
    "denoiseStrength": 0, "dehazeStrength": 0, "modifiedCameraColorEnabled": False,
    "modifiedCameraColorApplied": False, "modifiedCameraColorMode": "automatic",
    "modifiedCameraColorStrength": 100, "stretchEnabled": False,
    "temperature": 0, "tint": 0, "exposureEv": 0, "contrast": 0,
    "highlights": 0, "shadows": 0, "whites": 0, "blacks": 0,
    "vibrance": 0, "saturation": 0, "sharpening": 0,
    "starDefringeStrength": 0, "starReduceStrength": 0, "groundDetailStrength": 0,
    "referenceFrames": 1,
}
STACK_SETTINGS = {"method": "average", "kappa": 2.5, "groundStackMethod": "average",
                  "skyGroundFeatherRadius": 20}


def require(condition, message):
    if not condition:
        raise ComparisonError(message)


def expect(record, field, expected, label):
    value = record.get(field)
    if type(expected) is bool:
        matches = value is expected
    elif type(expected) in (int, float):
        matches = type(value) in (int, float) and value == expected
    else:
        matches = value == expected
    require(matches, f"{label}: {field} must be {expected!r}")


def integer(record, field, label, minimum=0):
    value = record.get(field)
    require(type(value) is int and value >= minimum,
            f"{label}: invalid or missing {field}")
    return value


def number(record, field, label):
    value = record.get(field)
    require(type(value) in (int, float) and math.isfinite(value),
            f"{label}: invalid or missing {field}")
    return value


def source_path(record, field, label):
    value = record.get(field)
    require(isinstance(value, str) and "\0" not in value and Path(value).is_absolute(),
            f"{label}: invalid or missing absolute {field}")
    # Do not stat or resolve the report's RAW paths: render-only can run unmounted.
    return Path(os.path.normpath(value))


def artifact(record, field, directory, label, suffixes):
    value = record.get(field)
    require(isinstance(value, str) and "\0" not in value and Path(value).name,
            f"{label}: invalid or missing {field}")
    # An already-run directory can be moved together with its report.
    path = directory / Path(value).name
    require(path.suffix.lower() in suffixes and path.is_file()
            and path.resolve().parent == directory,
            f"{label}: {field} must name a local {'/'.join(suffixes)} file in the Runner directory")
    return path


@dataclass(frozen=True)
class Run:
    role: str
    report: dict
    source: Path
    start: int
    reference_index: int
    selected: tuple
    used: tuple
    bounds: Rect
    tiff: Path
    mask: Path | None

    @property
    def reference(self):
        return self.selected[self.reference_index]


def read_run(role, directory):
    directory = directory.resolve(strict=True)
    try:
        report = json.loads((directory / "pipeline-report.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ComparisonError(f"{role}: cannot read pipeline-report.json: {error}") from error
    require(isinstance(report, dict), f"{role}: expected a JSON Runner report object")
    expect(report, "schemaVersion", REPORT_SCHEMA, role)
    require(isinstance(report.get("toolVersion"), str) and report["toolVersion"].strip(),
            f"{role}: missing Runner toolVersion")
    for field, value in BASELINE.items():
        expect(report, field, value, role)

    single, separated = role == "single", role == "separated"
    expect(report, "singleFrameMode", single, role)
    expect(report, "skyGroundSeparationEnabled", separated, role)
    if not single:
        for field, value in STACK_SETTINGS.items():
            expect(report, field, value, role)
    require(type(report.get("automaticLensChromaticAberrationApplied")) is bool,
            f"{role}: missing lens correction outcome")

    mask = None
    if separated:
        mask_source = report.get("skyGroundMaskSource")
        if mask_source == "user-mask":
            expect(report, "skyGroundMode", "user-mask", role)
            mask_input = report.get("skyGroundMask")
            require(isinstance(mask_input, str) and mask_input and "\0" not in mask_input,
                    f"{role}: missing original user mask path")
        else:
            require(mask_source in ("embedded-preview", "linear-fallback"),
                    f"{role}: no supported sky/ground mask was used")
            expect(report, "skyGroundMode", "automatic", role)
            expect(report, "skyGroundMask", "", role)
        require(0 < number(report, "skyGroundSkyFraction", role) < 1,
                f"{role}: the mask must contain both sky and ground")
        mask = artifact(report, "skyGroundMaskOutput", directory, role, (".png",))
    else:
        expect(report, "skyGroundMode", "disabled", role)
        expect(report, "skyGroundMaskSource", "", role)
        expect(report, "skyGroundMask", "", role)

    source = source_path(report, "input", role)
    start = integer(report, "startIndex", role)
    count = integer(report, "selectedFrames", role, 1 if single else 2)
    reference = integer(report, "referenceIndex", role)
    requested = integer(report, "requestedReferenceIndex", role, -1 if single else 0)
    require(reference < count and (requested == reference or (single and requested == -1)),
            f"{role}: Runner must use the explicitly requested reference")
    require(not single or count == 1, f"{role}: expected one selected RAW")

    quality = report.get("frameQuality")
    require(isinstance(quality, list) and len(quality) == count,
            f"{role}: frameQuality does not enumerate the selected frames")
    selected = []
    by_name = {}
    for index, frame in enumerate(quality):
        require(isinstance(frame, dict), f"{role}: invalid frameQuality row {index}")
        expect(frame, "index", index, role)
        expect(frame, "rejected", False, role)
        path = source_path(frame, "sourcePath", role)
        require(source in path.parents, f"{role}: selected RAW is outside the input root")
        expect(frame, "file", path.name, role)
        selected.append(path.relative_to(source))
        by_name.setdefault(path.name, []).append(selected[-1])
    require(len(set(selected)) == count, f"{role}: duplicate selected RAW path")
    require(source_path(report, "referencePath", role) == source / selected[reference],
            f"{role}: referencePath does not match the selected reference frame")
    expect(report, "referenceFile", selected[reference].name, role)

    # Schema 20 provides full paths for selection, but only basenames for skips.
    # Never guess when a skipped name could refer to more than one selected RAW.
    skipped = report.get("skippedFrames")
    require(isinstance(skipped, list), f"{role}: missing skippedFrames")
    expect(report, "skippedFrameCount", len(skipped), role)
    skipped_paths = set()
    for entry in skipped:
        name = entry.get("file") if isinstance(entry, dict) else None
        matches = by_name.get(name, ()) if isinstance(name, str) else ()
        require(len(matches) == 1, f"{role}: cannot identify skipped RAW {name!r} unambiguously")
        path = matches[0]
        require(path != selected[reference] and path not in skipped_paths,
                f"{role}: invalid or duplicate skipped RAW {name!r}")
        skipped_paths.add(path)
    used = tuple(path for path in selected if path not in skipped_paths)
    require(len(used) >= (1 if single else 2), f"{role}: too few successful frames")
    expect(report, "processedFrames", len(used), role)
    expect(report, "transformedSourceFrames", len(used) - 1, role)
    aligned = sum(integer(report, field, role) for field in (
        "affineAlignedFrames", "homographyAlignedFrames"))
    require(aligned == len(used) - 1, f"{role}: alignment counts do not match the successful frames")

    bounds = Rect(integer(report, "cropOffsetX", role), integer(report, "cropOffsetY", role),
                  integer(report, "width", role, 1), integer(report, "height", role, 1))
    tiff = artifact(report, "outputFile", directory, role, (".tiff", ".tif"))
    return Run(role, report, source, start, reference, tuple(selected), used, bounds, tiff, mask)


def verify_runs(single, stack, separated, *, start=None, count=None, reference=None,
                reference_file=None, source=None):
    runs = (single, stack, separated)
    require(all(run.source == stack.source for run in runs)
            and (source is None or stack.source == source),
            "Runner reports must share the same input root")
    require(all(run.report["toolVersion"] == stack.report["toolVersion"] for run in runs),
            "Runner reports must use the same toolVersion")
    require((stack.start, stack.selected) == (separated.start, separated.selected),
            "stack and separated used different selected frame segments")
    require(stack.used == separated.used,
            "stack and separated did not process the same successful RAW identities")
    require(all(run.reference == stack.reference for run in runs)
            and stack.reference_index == separated.reference_index
            and single.start == stack.start + stack.reference_index,
            "single, stack and separated must use the same reference RAW")
    for expected, actual, label in (
            (start, stack.start, "--start-index"), (count, len(stack.selected), "--limit"),
            (reference, stack.reference_index, "--reference-index"),
            (reference_file, stack.reference.as_posix(), "--reference-file")):
        require(expected is None or expected == actual, f"reports do not match {label}")

    applied = stack.report["automaticLensChromaticAberrationApplied"]
    for run in runs:
        expect(run.report, "automaticLensChromaticAberrationApplied", applied, run.role)
    if applied:
        for field in ("chromaticAberrationRed", "chromaticAberrationBlue"):
            models = [run.report.get(field) for run in runs]
            require(all(isinstance(model, dict) and type(model.get("reliable")) is bool
                        for model in models), f"missing lens model: {field}")
            scales = [number(model, "sourceScale", field) for model in models]
            require(all(model["reliable"] == models[0]["reliable"] for model in models)
                    and all(math.isclose(scale, scales[0], rel_tol=1e-8) for scale in scales),
                    f"reference lens models differ: {field}")


def run_pipeline(runner, source, output, scratch, start, count, reference, memory_budget,
                 user_mask):
    common = [str(runner), "--input", str(source), "--method", STACK_SETTINGS["method"],
              "--kappa", str(STACK_SETTINGS["kappa"]),
              "--no-quality-rejection", "--no-photometric-normalization",
              "--ground-method", STACK_SETTINGS["groundStackMethod"],
              "--sky-ground-feather", str(STACK_SETTINGS["skyGroundFeatherRadius"]),
              "--ground-detail-strength", "0", "--denoise-strength", "0",
              "--dehaze-strength", "0", "--sharpening", "0", "--star-reduce-strength", "0",
              "--star-defringe-strength", "0"]
    if memory_budget is not None:
        common += ["--memory-budget-mib", str(memory_budget)]
    for role in ROLES:
        directory = output / role
        if role == "single":
            offset, limit, index = start + reference, 1, 0
            mode = ["--single"]
        else:
            offset, limit, index = start, count, reference
            mode = ["--sky-ground"] if role == "separated" else []
            if role == "separated" and user_mask is not None:
                mode += ["--sky-ground-mask", str(user_mask)]
        argv = common + ["--output", str(directory), "--start-index", str(offset),
                         "--limit", str(limit), "--reference-index", str(index)] + mode
        print(f"Runner: {role}", flush=True)
        result = subprocess.run(argv, env={**os.environ, "TMPDIR": str(scratch)})
        require(result.returncode == 0, f"{role}: Runner exited with status {result.returncode}")
        yield read_run(role, directory)
