#!/usr/bin/env python3
"""Validate Nebulens App Store metadata and image assets."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from urllib.parse import urlparse

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
ACCEPTED_MAC_SIZES = {
    (1280, 800),
    (1440, 900),
    (2560, 1600),
    (2880, 1800),
}


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def check_metadata(path: Path, errors: list[str]) -> None:
    data = load_json(path)
    limits = {
        "name": 30,
        "subtitle": 30,
        "promotionalText": 170,
        "description": 4000,
    }
    for field, limit in limits.items():
        value = data.get(field, "")
        if not isinstance(value, str) or not value.strip():
            errors.append(f"{path}: {field} is missing")
        elif len(value) > limit:
            errors.append(
                f"{path}: {field} has {len(value)} characters, limit {limit}"
            )

    keywords = data.get("keywords", "")
    keyword_bytes = len(keywords.encode("utf-8"))
    if keyword_bytes > 100:
        errors.append(
            f"{path}: keywords use {keyword_bytes} bytes, limit 100"
        )
    for field in ("supportUrl", "marketingUrl", "privacyPolicyUrl"):
        value = data.get(field, "")
        parsed = urlparse(value)
        if parsed.scheme != "https" or not parsed.netloc:
            errors.append(f"{path}: {field} must be a complete HTTPS URL")


def image_has_alpha(image: Image.Image) -> bool:
    return image.mode in {"RGBA", "LA"} or "transparency" in image.info


def check_screenshots(errors: list[str]) -> None:
    for locale in ("zh-Hans", "en-US"):
        directory = ROOT / "screenshots" / locale
        screenshots = sorted(directory.glob("*.png"))
        if not 1 <= len(screenshots) <= 10:
            errors.append(
                f"{directory}: expected 1-10 screenshots, found {len(screenshots)}"
            )
        for path in screenshots:
            with Image.open(path) as image:
                if image.size not in ACCEPTED_MAC_SIZES:
                    errors.append(
                        f"{path}: unsupported Mac screenshot size {image.size}"
                    )
                if image_has_alpha(image):
                    errors.append(f"{path}: screenshots must not contain alpha")


def check_icon(errors: list[str]) -> None:
    path = ROOT / "icon" / "Nebulens-AppIcon-1024.png"
    with Image.open(path) as image:
        if image.size != (1024, 1024):
            errors.append(f"{path}: icon must be 1024x1024")


def placeholder_files() -> list[Path]:
    extensions = {".json", ".md", ".html"}
    return [
        path
        for path in ROOT.rglob("*")
        if path.is_file()
        and path.suffix in extensions
        and "__" in path.read_text(encoding="utf-8")
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--strict",
        action="store_true",
        help="also fail when legal/contact placeholders remain",
    )
    arguments = parser.parse_args()

    errors: list[str] = []
    for locale in ("zh-Hans", "en-US"):
        check_metadata(ROOT / "metadata" / f"{locale}.json", errors)
    check_screenshots(errors)
    check_icon(errors)

    placeholders = placeholder_files()
    if placeholders:
        print("Placeholders remain in:")
        for path in placeholders:
            print(f"  - {path.relative_to(ROOT)}")
        if arguments.strict:
            errors.append("replace all legal/contact placeholders before upload")

    if errors:
        print("App Store material validation failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print("App Store material structure is valid")
    if placeholders:
        print("Draft mode: rerun with --strict before submission")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
