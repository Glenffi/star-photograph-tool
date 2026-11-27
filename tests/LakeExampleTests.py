"""Offline checks using synthetic RGB16 TIFFs and the existing synthetic Bayer DNG fixture."""

import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

import numpy as np
from PIL import Image, PngImagePlugin

from TimelapseCliTests import HEIGHT, WIDTH, pack_ifd, text, u16, u32, write_dng


TOOL = Path(__file__).resolve().parents[1] / "tools" / "lake_example"
sys.path.insert(0, str(TOOL))
from raster import fit_display, map_display


ROLES = ("single", "stack", "separated")
NAMES = ("frame01.dng", "frame02.dng", "frame03.dng", "frame04.dng", "nested/frame05.dng")
REFERENCE = NAMES[2]
USED = (NAMES[1], NAMES[2], NAMES[4])
BOUNDS = {"single": (4, 6, 172, 132), "stack": (8, 9, 170, 125),
          "separated": (11, 7, 164, 128)}
ROI = (37, 53, 64, 47)
FILES = {role: f"ridge-{role}.png" for role in (*ROLES, "comparison")}
LUMA = np.array([0.2126, 0.7152, 0.0722])
RUNNER = None


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_tiff(path, rgb):
    """Synthetic uncompressed RGB16 TIFF; 11-row strips exercise offset arithmetic."""
    height, width, _ = rgb.shape
    rows = 11
    strips = [rgb[y:y + rows].astype("<u2").tobytes() for y in range(0, height, rows)]

    def longs(values):
        return 4, len(values), struct.pack(f"<{len(values)}I", *values)

    tags = {256: u32(width), 257: u32(height), 258: u16(16, 16, 16), 259: u16(1),
            262: u16(2), 270: text("synthetic TIFF only"), 273: longs([0] * len(strips)),
            274: u16(1), 277: u16(3), 278: u32(rows), 279: longs([len(s) for s in strips]),
            284: u16(1)}
    offset = 8 + len(pack_ifd(tags, 8))
    offsets = []
    for strip in strips:
        offsets.append(offset)
        offset += len(strip)
    tags[273] = longs(offsets)
    path.write_bytes(b"II" + struct.pack("<HI", 42, 8) + pack_ifd(tags, 8) + b"".join(strips))


def split_mask(width, height, center):
    y, x = np.indices((height, width))
    height_from_edge = center + (7 * np.cos(x / 18)).round() - y
    binary = np.where(height_from_edge >= 0, 255, 0).astype(np.uint8)
    feathered = np.clip(128 + 7 * height_from_edge, 0, 255).astype(np.uint8)
    return binary, feathered


def decode_srgb(rgb):
    v = np.asarray(rgb) / 255
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def encode_srgb(v):
    return np.where(v <= 0.0031308, 12.92 * v, 1.055 * v ** (1 / 2.4) - 0.055)


def expected_display(rgb, black, exposure):
    y = rgb @ LUMA
    q = np.maximum(0, y - black) * exposure
    gain = q / (1 + q) / y
    rgb = rgb * np.minimum(gain, 1.0 / rgb.max(axis=-1))[..., None]
    return (encode_srgb(rgb) * 255 + 0.5).astype(np.uint8)


class CliTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="nebulens-ridge-fixture-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name).resolve()

    def cli(self, *args, error=None):
        result = subprocess.run([sys.executable, "-B", str(TOOL / "generate.py"), *map(str, args)],
                                capture_output=True, encoding="utf-8", timeout=90)
        if error is None:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn(error, result.stderr)
        return result

    def render_only(self, source_dirs, output, *options, error=None):
        dirs = [arg for name in ROLES for arg in (f"--{name}-dir", source_dirs / name)]
        return self.cli("render-only", *dirs, "--output", output, *options, error=error)


class SyntheticReportTests(CliTests):
    def setUp(self):
        super().setUp()
        self.runs, self.output = self.root / "synthetic-runs", self.root / "proof"
        self.source = self.root / "not-mounted-private-location"
        self.mask = self.root / "synthetic-original-mask.png"
        self.binary, self.diagnostic = split_mask(184, 144, 80)
        image_text = PngImagePlugin.PngInfo()
        image_text.add_text("Private comment", "synthetic mask metadata should not survive")
        Image.fromarray(self.binary).save(self.mask, pnginfo=image_text)
        self.rgb = {}
        y, x = np.indices(self.binary.shape)
        coords = np.stack((880 + 11 * x + 6 * y, 1500 + 7 * x + 15 * y,
                           450 + 9 * x + 3 * y), axis=-1).astype(np.uint16)
        for role in ROLES:
            folder = self.runs / role
            folder.mkdir(parents=True)
            raw = coords.copy()
            if role == "stack":
                raw[:, 64:] += 900
            elif role == "separated":
                raw[:, 64:] *= 2
            self.rgb[role] = raw
            x0, y0, w, h = BOUNDS[role]
            write_tiff(folder / "formal.tiff", raw[y0:y0 + h, x0:x0 + w])
            Image.new("RGB", (4, 4), "red").save(folder / "result-preview.png")
            if role == "separated":
                Image.fromarray(self.diagnostic).save(folder / "sky-ground-mask.png")
            self.write_report(role, self.report(role))

    def report(self, role):
        single, separated = role == "single", role == "separated"
        names = (REFERENCE,) if single else NAMES
        skips = () if single else (NAMES[0], NAMES[3])
        x, y, w, h = BOUNDS[role]
        processed = len(names) - len(skips)
        return {
            "schemaVersion": 20, "toolVersion": "fixture-synthetic", "input": str(self.source),
            "success": True, "error": "", "singleFrameMode": single, "timelapseMode": False,
            "starTrailMode": False, "deepSkyCalibrationEnabled": False, "referenceFrames": 1,
            "autoQualityRejectionEnabled": False, "qualityRejectedFrames": 0,
            "photometricNormalizationEnabled": False, "automaticLensChromaticAberrationEnabled": True,
            "automaticLensChromaticAberrationApplied": True,
            "chromaticAberrationRed": {"reliable": True, "sourceScale": 1.002},
            "chromaticAberrationBlue": {"reliable": False, "sourceScale": 1.0},
            "method": "kappa-sigma" if single else "average", "kappa": 2.5,
            "skyGroundSeparationEnabled": separated, "skyGroundMode": "automatic" if separated else "disabled",
            "skyGroundMaskSource": "linear-fallback" if separated else "", "skyGroundMask": "",
            "skyGroundMaskOutput": "/old-runner-location/sky-ground-mask.png" if separated else "",
            "skyGroundSkyFraction": 0.52 if separated else 0, "skyGroundFeatherRadius": 20,
            "groundStackMethod": "average", "groundDetailStrength": 0,
            "denoiseStrength": 0, "dehazeStrength": 0, "modifiedCameraColorEnabled": False,
            "modifiedCameraColorApplied": False, "modifiedCameraColorMode": "automatic",
            "modifiedCameraColorStrength": 100, "stretchEnabled": False,
            "temperature": 0, "tint": 0, "exposureEv": 0, "contrast": 0, "highlights": 0,
            "shadows": 0, "whites": 0, "blacks": 0, "vibrance": 0, "saturation": 0,
            "sharpening": 0, "starDefringeStrength": 0, "starReduceStrength": 0,
            "startIndex": 9 if single else 7, "selectedFrames": len(names), "processedFrames": processed,
            "transformedSourceFrames": processed - 1, "affineAlignedFrames": processed - 1,
            "homographyAlignedFrames": 0, "skippedFrameCount": len(skips),
            "skippedFrames": [{"file": name, "stage": "alignment"} for name in skips],
            "requestedReferenceIndex": -1 if single else 2, "referenceIndex": 0 if single else 2,
            "referencePath": str(self.source / REFERENCE), "referenceFile": REFERENCE,
            "frameQuality": [{"index": i, "file": Path(name).name, "rejected": False,
                              "sourcePath": str(self.source / name)} for i, name in enumerate(names)],
            "cropOffsetX": x, "cropOffsetY": y, "width": w, "height": h,
            "outputFile": "/old-runner-location/formal.tiff", "previewFile": "result-preview.png",
            "previewBlackPoint": 1777, "previewWhitePoint": 3800,
        }

    def write_report(self, role, value):
        (self.runs / role / "pipeline-report.json").write_text(json.dumps(value), encoding="utf-8")

    def test_coordinate_mapping_shared_fit_and_fixed_filenames(self):
        root = self.root / "shareable-media"
        self.render_only(self.runs, self.output, "--roi", ",".join(map(str, ROI)),
                         "--order-raw-root", root, "--title", "Synthetic TIFF fixture",
                         "--order-name", "fixture.nebprint", "--start-index", 7, "--limit", 5,
                         "--reference-index", 2, "--reference-file", REFERENCE)
        manifest_path = self.output / "comparison.json"
        manifest = read_json(manifest_path)
        self.assertEqual((manifest["schema"], manifest["version"], manifest["layout"]),
                         ("nebulens.ridge-comparison", 1, "three-column"))
        self.assertEqual(manifest["images"], FILES)
        self.assertEqual(sorted(p.name for p in self.output.iterdir()),
                         sorted((*FILES.values(), "comparison.json", "fixture.nebprint")))
        self.assertNotIn(str(self.source), manifest_path.read_text())
        self.assertEqual(manifest["selectedLights"], list(NAMES))
        self.assertEqual(manifest["lights"], list(USED))
        self.assertEqual(manifest["runner"]["processedFrames"], 3)
        self.assertEqual(manifest["runner"]["lensCorrection"], {"enabled": True, "applied": True})
        self.assertEqual(manifest["roiSource"], "explicit")

        x, y, w, h = ROI
        self.assertEqual(manifest["roi"], {"x": x, "y": y, "width": w, "height": h})
        single_y = self.rgb["single"][y:y + h, x:x + w] @ LUMA
        display = manifest["display"]
        self.assertAlmostEqual(display["blackY"], 0.5 * np.percentile(single_y, 0.5))
        self.assertAlmostEqual(display["referenceMedianY"], np.median(single_y))
        self.assertEqual(display["targetMedianSrgb"], 0.36)
        with Image.open(self.output / FILES["comparison"]) as image:
            combined = np.asarray(image)
        crops = []
        for role in ROLES:
            with Image.open(self.output / FILES[role]) as encoded:
                self.assertEqual((encoded.size, encoded.mode, encoded.info), ((w, h), "RGB", {"srgb": 0}))
                crop = np.asarray(encoded)
            raw = self.rgb[role][y:y + h, x:x + w]
            np.testing.assert_array_equal(crop, expected_display(
                raw, display["blackY"], display["exposurePerCodeValue"]))
            panel = manifest["panels"][role]
            mx, my = panel["x"], panel["y"]
            np.testing.assert_array_equal(crop, combined[my:my + h, mx:mx + w])
            crops.append(crop)
        self.assertLess(manifest["panels"]["single"]["x"], manifest["panels"]["stack"]["x"])
        self.assertLess(manifest["panels"]["stack"]["x"], manifest["panels"]["separated"]["x"])
        np.testing.assert_array_equal(crops[0][:, :20], crops[1][:, :20])
        np.testing.assert_array_equal(crops[0][:, :20], crops[2][:, :20])
        self.assertAlmostEqual(np.median(encode_srgb(decode_srgb(crops[0]) @ LUMA)), 0.36, places=2)

        order = read_json(self.output / "fixture.nebprint")
        self.assertEqual(order["format"], "NebulensPrintOrder")
        self.assertEqual((order["version"], order["scene"], order["automaticStill"]), (1, "sky-ground", False))
        self.assertEqual(order["lights"], [f"../shareable-media/{name}" for name in NAMES])
        self.assertEqual(order["excluded"], [])
        self.assertEqual(order["reference"], order["lights"][2])
        self.assertEqual(order["reference"], order["selected"])
        self.assertEqual((order["outputDirectory"], order["comparisonManifest"]), ("results", "comparison.json"))
        self.assertNotIn("userMask", order)
        expected_params = {"stackMethod": "average", "kappa": 2.5, "autoReject": False,
                           "photometric": False, "skyGround": True, "skyGroundMode": "auto",
                           "feather": 20, "groundMethod": "average", "groundDetail": 0,
                           "outputFormat": "tiff16", "stretchEnabled": False,
                           "noiseReductionEnabled": False, "noiseReductionStrength": 0,
                           "dewarpEnabled": False, "modifiedCameraColorEnabled": False,
                           "starDefringeEnabled": False, "starReduceEnabled": False, "sharpening": 0}
        self.assertEqual({key: order["params"][key] for key in expected_params}, expected_params)
        self.assertFalse(self.source.exists())

    def test_guided_bundle_uses_binary_original_not_the_diagnostic(self):
        separated = self.report("separated")
        separated.update(skyGroundMaskSource="user-mask", skyGroundMode="user-mask",
                         skyGroundMask=str(self.mask))
        self.write_report("separated", separated)
        self.render_only(self.runs, self.output, error="pass the original binary image")
        diagnostic = self.runs / "separated" / "sky-ground-mask.png"
        self.render_only(self.runs, self.output, "--user-mask", diagnostic,
                         error="must not be the Runner's feathered diagnostic")
        Image.fromarray(self.diagnostic).save(self.mask)
        self.render_only(self.runs, self.output, "--user-mask", self.mask, error="must contain only 0")
        self.assertFalse(self.output.exists())
        info = PngImagePlugin.PngInfo()
        info.add_text("Private comment", "mask metadata should not survive")
        Image.fromarray(self.binary).save(self.mask, pnginfo=info)
        self.render_only(self.runs, self.output, "--user-mask", self.mask)
        order = read_json(self.output / "comparison.nebprint")
        manifest = read_json(self.output / "comparison.json")
        self.assertEqual((order["userMask"], order["params"]["skyGroundMode"]), ("ridge-mask.png", "user-mask"))
        self.assertEqual((manifest["userMask"], manifest["runner"]["maskSource"], manifest["roiSource"]),
                         ("ridge-mask.png", "user-mask", "user-mask-horizon"))
        with Image.open(self.output / "ridge-mask.png") as image:
            self.assertEqual((image.mode, image.info), ("L", {}))
            np.testing.assert_array_equal(image, self.binary)

    def test_automatic_roi_requires_a_reliable_horizon(self):
        self.render_only(self.runs, self.output, "--user-mask", self.mask, error="used an automatic mask")
        self.render_only(self.runs, self.output)
        manifest = read_json(self.output / "comparison.json")
        roi = manifest["roi"]
        self.assertEqual(manifest["roiSource"], "automatic-mask-horizon")
        center_x, center_y = roi["x"] + roi["width"] // 2, roi["y"] + roi["height"] // 2
        self.assertLess(abs(center_y - (80 + round(7 * np.cos(center_x / 18)))), 10)
        no_horizon = self.root / "no-horizon"
        Image.new("L", (184, 144), 220).save(self.runs / "separated" / "sky-ground-mask.png")
        self.render_only(self.runs, no_horizon, error="no stable sky-to-ground horizon")
        self.assertFalse(no_horizon.exists())

    def test_rejects_different_successful_identities_at_the_same_count(self):
        report = self.report("separated")
        report["skippedFrames"][0]["file"] = NAMES[1]
        self.write_report("separated", report)
        self.render_only(self.runs, self.output, error="did not process the same successful RAW identities")
        self.assertFalse(self.output.exists())

    def test_rejects_ambiguous_skip_basename(self):
        report = self.report("stack")
        report["frameQuality"][-1].update(sourcePath=str(self.source / "nested" / NAMES[0]), file=NAMES[0])
        self.write_report("stack", report)
        self.render_only(self.runs, self.output, error="cannot identify skipped RAW")

    def test_rejects_bad_status_reference_count_setting_and_extent(self):
        for role, field, value, diagnostic in (
                ("single", "success", False, "success must be True"),
                ("single", "referencePath", str(self.source / NAMES[1]), "referencePath does not match"),
                ("stack", "processedFrames", 4, "processedFrames must be 3"),
                ("separated", "photometricNormalizationEnabled", True, "photometricNormalizationEnabled"),
                ("separated", "cropOffsetX", -1, "invalid or missing cropOffsetX"),
                ("stack", "cropOffsetY", 100, "exceed the reference mask extent"),
                ("stack", "width", 169, "TIFF dimensions do not match")):
            with self.subTest(role=role, field=field):
                report = self.report(role)
                self.write_report(role, {**report, field: value})
                self.render_only(self.runs, self.output, "--roi", "37,53,64,47", error=diagnostic)
                self.assertFalse(self.output.exists())
                self.write_report(role, report)

    def test_rejects_non_rgb16_and_will_not_overwrite(self):
        self.output.mkdir()
        sentinel = self.output / "sentinel"
        sentinel.write_bytes(b"keep me")
        self.render_only(self.runs, self.output, error="output already exists")
        self.assertEqual(sentinel.read_bytes(), b"keep me")
        self.render_only(self.runs, self.root / "out-of-bounds", "--roi", "3,7,64,47",
                         error="fully inside the shared reference extent")
        Image.new("RGB", (170, 125)).save(self.runs / "stack" / "formal.tiff")
        self.render_only(self.runs, self.root / "not16", "--roi", "37,53,64,47", error="RGB16 Runner TIFF")


class ToneTests(unittest.TestCase):
    def test_neutral_ramp_has_reference_middle_and_monotone_shoulder(self):
        steps = np.array([400, 800, 950, 1150, 2300, 12000, 65535], dtype=np.uint16)
        grayscale = np.repeat(steps[None, :, None], 3, axis=-1)
        display = fit_display(grayscale)
        rendered = map_display(grayscale, display)[0]
        np.testing.assert_array_equal(rendered[:, 0], rendered[:, 2])
        self.assertTrue(np.all(np.diff(rendered[:, 0].astype(int)) > 0))
        self.assertAlmostEqual(rendered[3, 0] / 255, 0.36, places=2)
        self.assertLess(rendered[-1, 0], 255)

    def test_below_black_channels_survive_and_gamut_cap_preserves_color(self):
        reference = np.array([[[1000, 1000, 1000], [1100, 1100, 1100], [1300, 1300, 1300]]])
        display = fit_display(reference)
        color_ramp = np.array([[[350, 3000, 450], [700, 6000, 900], [65000, 1000, 1800]]])
        self.assertLess(color_ramp[0, 0, 0], display["blackY"])
        self.assertLess(color_ramp[0, 0, 2], display["blackY"])
        result = map_display(color_ramp, display)
        self.assertTrue(np.all(result[0, 0] > 0))
        self.assertEqual(result[0, 2, 0], 255)
        decoded = decode_srgb(result)
        np.testing.assert_allclose(decoded / decoded.sum(axis=-1, keepdims=True),
                                   color_ramp / color_ramp.sum(axis=-1, keepdims=True), atol=0.003)


class NativeRunnerFixtureTests(CliTests):
    def test_real_runner_with_existing_synthetic_dng_fixture(self):
        if RUNNER is None:
            self.skipTest("pass --runner to exercise the native pipeline on synthetic DNGs")
        raw = self.root / "synthetic-dngs"
        for i in range(6):
            write_dng(raw / f"fixture{i:02}.dng", i * 5, i * 2, 1)
        mask = self.root / "synthetic-binary-mask.png"
        binary, _ = split_mask(WIDTH, HEIGHT, 280)
        Image.fromarray(binary).save(mask)
        bundle = self.root / "synthetic-guided"
        common = ("run", "--runner", RUNNER, "--input", raw, "--start-index", 1, "--limit", 4,
                  "--reference-index", 2, "--reference-file", "fixture03.dng", "--roi", "100,240,160,80")
        self.cli(*common, "--output", bundle, "--user-mask", mask, "--keep-intermediates")
        manifest = read_json(bundle / "comparison.json")
        self.assertEqual(manifest["reference"], "fixture03.dng")
        self.assertEqual(len(manifest["lights"]), 4)
        self.assertGreater(manifest["runner"]["crops"]["stack"]["x"], 0)
        self.assertEqual(manifest["runner"]["maskSource"], "user-mask")
        rerender = self.root / "synthetic-replay"
        self.render_only(bundle / "intermediates", rerender, "--user-mask", mask, "--roi", "100,240,160,80")
        self.assertEqual(read_json(rerender / "comparison.json"), manifest)
        for name in FILES.values():
            self.assertEqual((rerender / name).read_bytes(), (bundle / name).read_bytes())

        automatic = self.root / "synthetic-auto"
        self.cli(*common, "--output", automatic)
        auto_order = read_json(automatic / "comparison.nebprint")
        self.assertEqual(auto_order["params"]["skyGroundMode"], "auto")
        self.assertFalse((automatic / "intermediates").exists())
        self.assertEqual(sorted(p.suffix for p in automatic.iterdir()),
                         sorted([".png"] * 4 + [".json", ".nebprint"]))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--runner", type=Path)
    args, unittest_args = parser.parse_known_args()
    RUNNER = args.runner.resolve(strict=True) if args.runner else None
    unittest.main(argv=[sys.argv[0], *unittest_args])
