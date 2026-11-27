"""Read production TIFF crops and render a shared, reference-fitted proof."""

from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont, PngImagePlugin


class ComparisonError(ValueError):
    pass


@dataclass(frozen=True)
class Rect:
    x: int
    y: int
    width: int
    height: int

    @property
    def right(self):
        return self.x + self.width

    @property
    def bottom(self):
        return self.y + self.height

    def contains(self, other):
        return (self.x <= other.x < other.right <= self.right
                and self.y <= other.y < other.bottom <= self.bottom)


def intersect(bounds):
    x = max(rect.x for rect in bounds)
    y = max(rect.y for rect in bounds)
    return Rect(x, y, min(rect.right for rect in bounds) - x,
                min(rect.bottom for rect in bounds) - y)


def read_tiff_crop(path: Path, bounds: Rect, roi: Rect):
    if not bounds.contains(roi):
        raise ComparisonError(f"ROI extends outside {path.name}")

    with path.open("rb") as stream:
        order = {b"II": "<u2", b"MM": ">u2"}.get(stream.read(2))
        stream.seek(0)
        with Image.open(stream, formats=("TIFF",)) as image:
            tags = image.tag_v2
            if (order is None or image.n_frames != 1 or tags.get(262) != 2
                    or tags.get(277) != 3 or tags.get(258) != (16, 16, 16)
                    or tags.get(339, (1,)) not in ((1,), (1, 1, 1))
                    or tags.get(274, 1) != 1 or tags.get(284, 1) != 1
                    or tags.get(259, 1) != 1 or 324 in tags):
                raise ComparisonError(
                    f"{path.name}: expected a top-left, uncompressed RGB16 Runner TIFF")
            if image.size != (bounds.width, bounds.height):
                raise ComparisonError(f"{path.name}: TIFF dimensions do not match the report")

            rows = tags.get(278, bounds.height)
            offsets, counts = tags.get(273, ()), tags.get(279, ())
            if not isinstance(rows, int) or rows <= 0:
                raise ComparisonError(f"{path.name}: invalid TIFF rows per strip")
            expected_strips = (bounds.height + rows - 1) // rows
            if len(offsets) != expected_strips or len(counts) != expected_strips:
                raise ComparisonError(f"{path.name}: invalid TIFF strip table")
            stride, file_size = bounds.width * 6, path.stat().st_size
            for index, (offset, count) in enumerate(zip(offsets, counts)):
                required = min(rows, bounds.height - index * rows) * stride
                if offset < 8 or count < required or offset + count > file_size:
                    raise ComparisonError(f"{path.name}: invalid or truncated TIFF strip")

            # Pillow exposes the TIFF structure but decodes RGB16 to RGB8.
            # Read the production exporter's uncompressed scanlines directly.
            result = np.empty((roi.height, roi.width, 3), dtype=np.uint16)
            x_bytes = (roi.x - bounds.x) * 6
            for i in range(roi.height):
                strip, row = divmod(roi.y - bounds.y + i, rows)
                stream.seek(offsets[strip] + row * stride + x_bytes)
                data = stream.read(roi.width * 6)
                if len(data) != roi.width * 6:
                    raise ComparisonError(f"{path.name}: truncated TIFF scanline")
                result[i] = np.frombuffer(data, dtype=order).reshape(roi.width, 3)
    return result


def horizon_roi(mask, common: Rect, feather: int):
    """Find a window with sustained white sky over dark ground in the Runner mask."""
    width = min(640, common.width // 2)
    height = min(384, common.height // 2)
    if width >= 32 and height >= 32:
        gap = min(height // 3, max(8, 2 * feather))
        last_x = common.right - width
        starts = sorted({last_x, *range(common.x, last_x + 1, width // 4)})
        best = None
        for x in starts:
            crossings = []
            for column in np.linspace(x, x + width - 1, 17, dtype=int):
                line = mask[common.y:common.bottom, column]
                edges = np.flatnonzero((line[:-1] >= 128) & (line[1:] < 128)) + 1
                credible = [int(y) + common.y for y in edges
                            if gap <= y < len(line) - gap
                            and line[y - gap] >= 200 and line[y + gap] <= 55]
                if len(credible) == 1:
                    crossings.append(credible[0])
            if len(crossings) < 14:
                continue
            center_y = int(np.median(crossings))
            roi = Rect(x, center_y - height // 2, width, height)
            if (not common.contains(roi) or min(crossings) < roi.y + gap
                    or max(crossings) >= roi.bottom - gap):
                continue
            relief = np.percentile(crossings, 90) - np.percentile(crossings, 10)
            center_distance = abs(2 * x + width - common.x - common.right)
            score = (len(crossings), relief, -center_distance)
            if best is None or score > best[0]:
                best = (score, roi)
        if best is not None:
            return best[1]
    raise ComparisonError("no stable sky-to-ground horizon in the shared crop; supply --roi")


LUMA = (0.2126, 0.7152, 0.0722)
BLACK_PERCENTILE = 0.5
BLACK_FRACTION = 0.5
MID_GRAY_SRGB = 0.36


def fit_display(single):
    luminance = np.einsum("...c,c->...", single, LUMA)
    low, median = map(float, np.percentile(luminance, (BLACK_PERCENTILE, 50)))
    black = BLACK_FRACTION * low
    target = ((MID_GRAY_SRGB + 0.055) / 1.055) ** 2.4
    anchor = median if median > black else 0.18 * 65535
    exposure = (target / (1.0 - target)) / (anchor - black)
    return {
        "input": "linear-sRGB16", "output": "sRGB8", "fit": "single-roi",
        "luminanceWeights": LUMA, "referenceBlackPercentile": BLACK_PERCENTILE,
        "referenceLowY": low, "referenceMedianY": median, "blackFraction": BLACK_FRACTION,
        "blackY": black, "anchorY": anchor, "exposurePerCodeValue": exposure,
        "targetMedianSrgb": MID_GRAY_SRGB, "targetMedianLinear": target,
        "curve": "reinhard-luminance", "shoulder": "x/(1+x)",
        "gamutPolicy": "uniform-rgb-gain-cap",
    }


def map_display(rgb16, display):
    rgb = rgb16.astype(np.float64)
    y = np.einsum("...c,c->...", rgb, LUMA)
    exposed = np.maximum(0, y - display["blackY"]) * display["exposurePerCodeValue"]
    mapped_y = exposed / (1.0 + exposed)
    gain = np.divide(mapped_y, y, out=np.zeros_like(y), where=y > 0)
    # Preserve linear RGB ratios even for bright, saturated highlights.
    peak = np.max(rgb, axis=-1)
    np.divide(1.0, peak, out=peak, where=peak > 0)
    np.minimum(gain, peak, out=gain)
    rgb *= gain[..., None]
    srgb = np.where(rgb <= 0.0031308, 12.92 * rgb, 1.055 * rgb ** (1 / 2.4) - 0.055)
    return np.floor(255 * srgb + 0.5).astype(np.uint8)


def save_png(image, path):
    metadata = PngImagePlugin.PngInfo()
    metadata.add(b"sRGB", b"\x00")
    image.save(path, format="PNG", pnginfo=metadata)


def read_binary_mask(path):
    with Image.open(path) as image:
        if (image.mode not in ("1", "L") or "transparency" in image.info
                or getattr(image, "n_frames", 1) != 1):
            raise ComparisonError("--user-mask must be a single-channel binary image without transparency")
        binary = image.convert("L")
        colors = binary.getcolors(maxcolors=2)
        if colors is None or {value for _, value in colors} != {0, 255}:
            raise ComparisonError("--user-mask must contain only 0 ground and 255 sky (both required)")
        binary.info.clear()
        return binary


def montage(images, guided):
    mask_label = "Guided Mask" if guided else "Auto Mask"
    labels = ("01  Single RAW", "02  Average Stack", f"03  Sky + Ground ({mask_label})")
    font = ImageFont.load_default(size=20)
    width, height = images[0].size
    pad, header = 12, 44
    column_width = max(width, *(int(font.getlength(label)) + 2 * pad + 1
                                for label in labels))
    combined = Image.new("RGB", (3 * column_width + 4 * pad, header + height + pad),
                         "#171b1e")
    draw = ImageDraw.Draw(combined)
    panels = []
    for i, (label, image) in enumerate(zip(labels, images)):
        center = pad + i * (column_width + pad) + column_width // 2
        draw.text((center, header // 2), label, font=font, fill="#f4f6f7", anchor="mm")
        x = pad + i * (column_width + pad) + (column_width - width) // 2
        combined.paste(image, (x, header))
        panels.append(Rect(x, header, width, height))
    return combined, panels
