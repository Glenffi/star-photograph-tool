# Offline sky/ground comparison

Requirements: Python 3.10+, NumPy, Pillow 10.1+ with FreeType, and (for `run` only) an
already-built `StarProcessorPipelineRunner`. On the local Mac use `/opt/miniconda3/bin/python3`.
The tool never downloads anything. Paths below are examples; run from the repository root.

## Render existing results

```sh
/opt/miniconda3/bin/python3 -B tools/lake_example/generate.py render-only \
  --single-dir /path/to/run/single --stack-dir /path/to/run/stack \
  --separated-dir /path/to/run/guided --user-mask /path/to/original-binary-mask.png \
  --start-index 0 --limit 9 --reference-index 4 --reference-file reference.ARW \
  --roi 3184,2664,640,320 --output /path/to/new-bundle
```

The Runner directories can have any names. Each must contain its schema-20 `pipeline-report.json`
and reported formal TIFF; the third also needs the reported diagnostic mask PNG. The files can
move together; artifact basenames in the reports are resolved within the supplied directories.
`render-only` never launches the Runner, reads RAWs, or changes the three input directories.
The selection arguments in this mode are optional assertions against the reports.

For an automatic separation run, leave out `--user-mask`. For a user-mask run, supply the same
original mask path reported by `--sky-ground-mask` (prefer an absolute path when invoking the
Runner). It must be opaque single-channel black and white (`0` ground, `255` sky). Only its
binary pixels are repackaged as `ridge-mask.png`, never the Runner's feathered diagnostic.

`--roi X,Y,W,H` uses zero-based coordinates in the **oriented, decoded reference RAW** before
alignment/CA trimming, not the sensor mosaic or preview. The ROI must fit every TIFF after
subtracting that TIFF's own offsets, including any offsets on the single. With no explicit ROI,
the tool searches the actual diagnostic mask for a sustained sky-to-ground edge inside the shared
extent, and fails if there is no credible window. Verify the automatic edge by eye when terrain
could have been misclassified; use a true guided mask for cases that need it.

## Run the production pipeline

```sh
/opt/miniconda3/bin/python3 -B tools/lake_example/generate.py run \
  --runner /path/to/StarProcessorPipelineRunner --input /path/to/raw-sequence \
  --start-index 0 --limit 9 --reference-index 4 --output /path/to/new-bundle \
  --roi 3184,2664,640,320
```

`--reference-index` is relative to the case-insensitively sorted Runner slice. The single processes
that same RAW. Both stacks use Average (`kappa=2.5`), no quality rejection or photometric
normalization, and no finishing, sharpness or star reduction. Default automatic lens CA correction
stays enabled on all three. Separation uses Average ground, feather 20 and ground detail 0;
add `--user-mask` for a guided run. The two stacks must have the same selected and actually used
paths, even if some frames are skipped for failed alignment. With schema 20 a skipped basename
must identify one selected path unambiguously. Missing or inconsistent evidence is an error.

Output must be a new directory with an existing parent. No RAWs are copied. TIFFs, Runner reports
and scratch data are temporary unless `run --keep-intermediates` is set, which keeps the three
Runner output directories under `intermediates/`. Those reports contain unsanitized local paths.

## Bundle contract

- `comparison.json` is UTF-8 JSON with `schema: "nebulens.ridge-comparison"`, `version: 1`,
  and `layout: "three-column"`. `images.single`, `.stack`, `.separated` and `.comparison`
  name `ridge-single.png`, `ridge-stack.png`, `ridge-separated.png` and `ridge-comparison.png`.
  The standalone crops are equal-sized 1:1 RGB PNGs; the montage adds labels outside the pixels,
  in single / stack / separated order. `panels` records the pixel rectangles in the montage.
- `roi`, `roiSource`, `runner.crops`, `runner.maskSource` and `runner.lensCorrection` record
  geometry and provenance. `selectedLights` lists all attempted RAWs, `lights` lists just those
  actually used, and `reference` identifies the shared reference; these paths are relative to the
  Runner input root. The manifest contains no absolute source paths or copied EXIF.
- `display` records a single fit from the reference TIFF's ROI. For each pixel, form linear sRGB
  luminance `Y` in 16-bit code values with the recorded weights. `blackY` equals half the reference
  luminance P0.5. With `x = max(Y-blackY, 0) * exposurePerCodeValue`, map to `Yout = x/(1+x)`;
  exposure targets sRGB 0.36 at the reference median. If that median is zero, `anchorY` records the
  nominal 18%-of-full-scale fallback. Rebuild linear RGB with one shared `Yout/Y` gain (zero for
  zero luminance); cap the common gain at `1/max(R,G,B)` for saturated colors. Then encode sRGB and
  round to 8 bits. There is no per-channel black subtraction, per-image stretch or sharpening.
- `comparison.nebprint` is a `NebulensPrintOrder` v1, with the selected **full** Runner slice,
  `excluded: []`, the same `reference` and `selected`, stable App parameters, `outputDirectory:
  "results"`, and `comparisonManifest: "comparison.json"`. The optional `userMask` field names
  `ridge-mask.png`; the parameter is `skyGroundMode: "user-mask"` (otherwise `"auto"`).
  The order's RAW paths are relative to its own directory. By default they point to the reported
  input root; use `--order-raw-root` to link to a differently located or later remounted RAW tree.
- Use `--title` for a sequence-specific manifest title and `--order-name` for a different `.nebprint`
  basename. Neither changes the four fixed PNG filenames.

## Local verification

```sh
/opt/miniconda3/bin/python3 -B tests/LakeExampleTests.py -v \
  --runner /path/to/StarProcessorPipelineRunner
```

Tests use synthetic RGB16 TIFFs and the repository's existing synthetic Bayer DNG fixture writer.
Without `--runner` only the native fixture test is skipped. Fixture outputs are not sample photos;
all temporary test inputs and results are removed at the end of the run.
