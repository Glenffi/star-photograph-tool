#!/usr/bin/env python3
"""Generate StarProcessor's signed-transport update manifest.

The manifest intentionally contains only public metadata. Server credentials
belong in SSH configuration or the caller's interactive session, never here.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import pathlib
import re
import sys
import urllib.parse


PLATFORMS = {"windows-x64", "macos-arm64", "macos-x64"}
SEMVER = re.compile(r"^(?:v)?[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$")


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as package:
        for block in iter(lambda: package.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_package(specification: str) -> tuple[str, pathlib.Path]:
    try:
        platform, path_text = specification.split("=", 1)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "package must use PLATFORM=/path/to/package"
        ) from error
    if platform not in PLATFORMS:
        raise argparse.ArgumentTypeError(f"unsupported platform: {platform}")
    path = pathlib.Path(path_text).expanduser().resolve()
    if not path.is_file():
        raise argparse.ArgumentTypeError(f"package does not exist: {path}")
    return platform, path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--release-notes", default="功能改进和问题修复。")
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument(
        "package", nargs="+", type=parse_package,
        help="PLATFORM=/path/to/package",
    )
    arguments = parser.parse_args()

    version = arguments.version.removeprefix("v")
    if not SEMVER.fullmatch(version):
        parser.error("version must be a semantic version such as 0.9.0")

    base_url = arguments.base_url.rstrip("/") + "/"
    parsed_base = urllib.parse.urlparse(base_url)
    if parsed_base.scheme != "https" or not parsed_base.netloc:
        parser.error("base URL must be HTTPS")

    platforms: dict[str, dict[str, object]] = {}
    for platform, path in arguments.package:
        if platform in platforms:
            parser.error(f"duplicate package platform: {platform}")
        expected_name = {
            "windows-x64": re.compile(
                rf"^StarProcessor-Windows-x64-v{re.escape(version)}\.zip$"
            ),
            "macos-arm64": re.compile(
                rf"^StarProcessor-v{re.escape(version)}-macOS-arm64\.dmg$"
            ),
            "macos-x64": re.compile(
                rf"^StarProcessor-v{re.escape(version)}-macOS-x64\.dmg$"
            ),
        }[platform]
        if not expected_name.fullmatch(path.name):
            parser.error(
                f"package name does not match {platform} v{version}: {path.name}"
            )
        package_url = urllib.parse.urljoin(
            base_url, urllib.parse.quote(path.name)
        )
        platforms[platform] = {
            "fileName": path.name,
            "url": package_url,
            "sha256": sha256(path),
            "size": path.stat().st_size,
        }

    manifest = {
        "schemaVersion": 1,
        "version": version,
        "publishedAt": dt.datetime.now(dt.timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z"),
        "releaseNotes": arguments.release_notes,
        "platforms": platforms,
    }
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(arguments.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
