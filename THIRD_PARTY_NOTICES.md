# Third-Party Notices

StarProcessor is distributed under the MIT License. Its packaged clients also
include dynamically linked open-source libraries. The exact library versions are
selected by Homebrew on macOS and by vcpkg/Qt setup on Windows.

| Component | Purpose | License |
|---|---|---|
| Qt 6 | Desktop UI, image and platform integration | LGPL-3.0-only, with other commercial/GPL options offered upstream |
| LibRaw | Camera RAW decoding | LGPL-2.1-only or CDDL-1.0 |
| Little CMS 2 | ICC color management used by packaged LibRaw | MIT-style license |
| libtiff | TIFF export and related codecs | libtiff BSD-style license |

The corresponding upstream projects, license texts and source distributions are
available from:

- Qt: https://www.qt.io/licensing/open-source-lgpl-obligations and https://code.qt.io/
- LibRaw: https://www.libraw.org/ and https://github.com/LibRaw/LibRaw
- Little CMS 2: https://www.littlecms.com/ and https://github.com/mm2/Little-CMS
- libtiff: https://libtiff.gitlab.io/libtiff/ and https://gitlab.com/libtiff/libtiff

Additional transitive runtime libraries may be listed in the package directory.
Their copyright and license terms remain with their respective authors. Nothing
in the StarProcessor MIT License replaces or limits those terms.

## Bundled Sample Proofs

- **Still:** `lake-complete.jpg` is the unchanged medium JPEG of the Milky Way
  reflected in a Yellowstone lake. Credit: Neal Herbert / NPS. The
  [NPGallery asset record](https://npgallery.nps.gov/AssetDetail/2ef91b9f-ad6d-4c60-9a57-d2386724aeb7)
  identifies it as [public domain](https://www.nps.gov/aboutus/disclaimer.htm).
  No claim to original U.S. Government works.
- **Star trail:** `la-silla-star-trails.jpg` is the unchanged screensize JPEG of
  [Star trails over La Silla](https://www.eso.org/public/images/santerne-northern-sky/),
  assembled from real nighttime exposures by Alexandre Santerne. Credit:
  ESO/A. Santerne. [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
- **Timelapse:** `paranal-timelapse-01.jpg` through `paranal-timelapse-05.jpg`
  are extracted from [Twilight and night sky time-lapse](https://www.eso.org/public/videos/p4paranal/)
  at 8, 13, 18, 23, and 28 seconds of the HD & Apple TV Preview. Credit:
  ESO/S. Guisard (www.eso.org/~sguisard). [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
  All frames share a fixed 1080 x 720 crop, were decoded to RGB, and re-encoded
  as JPEGs. No audio from the source is distributed.

Both ESO work pages link to the [ESO reuse terms](https://www.eso.org/public/outreach/copyright/).
Direct download URLs, frame positions, processing details and checksums are in the
[bundled proof manifest](resources/print-order/product-proof-samples.json).
