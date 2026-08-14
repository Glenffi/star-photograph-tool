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
