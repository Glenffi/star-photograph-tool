#!/usr/bin/env bash

# Build a self-contained local app bundle, install it in /Applications, and
# refresh Launch Services. The bundle keeps its technical executable identity
# for settings/update compatibility while Finder presents the 星炼 name.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-package-macos}"
DIST_DIR="${DIST_DIR:-${ROOT_DIR}/dist}"
INSTALL_ROOT="${INSTALL_ROOT:-/Applications}"
INSTALLED_APP="${INSTALL_ROOT}/星炼.app"
LEGACY_APP="${INSTALL_ROOT}/StarProcessor.app"
LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "Local app installation is available only on macOS" >&2
    exit 2
fi

# Homebrew libraries may target the currently installed macOS. That is valid
# for this machine-only bundle; public packages keep the stricter CI gate.
BUILD_DIR="${BUILD_DIR}" DIST_DIR="${DIST_DIR}" SKIP_DMG_CREATE=1 \
    ALLOW_HIGHER_DEPLOYMENT_TARGET="${ALLOW_HIGHER_DEPLOYMENT_TARGET:-1}" \
    "${ROOT_DIR}/scripts/package-macos.sh"

BUILT_APP="${BUILD_DIR}/StarProcessor.app"
if [[ ! -d "${BUILT_APP}" ]]; then
    echo "Built app bundle was not found: ${BUILT_APP}" >&2
    exit 3
fi

VERSION="$(/usr/libexec/PlistBuddy -c \
    'Print :CFBundleShortVersionString' "${BUILT_APP}/Contents/Info.plist")"
ARCH="$(uname -m)"
SOURCE_APP="${DIST_DIR}/.StarProcessor-v${VERSION}-macOS-${ARCH}-staging/dmg/StarProcessor.app"
if [[ ! -d "${SOURCE_APP}" ]]; then
    echo "Packaged app bundle was not found: ${SOURCE_APP}" >&2
    exit 3
fi

pkill -x StarProcessor 2>/dev/null || true
for app in "${INSTALLED_APP}" "${LEGACY_APP}"; do
    if [[ -d "${app}" ]]; then
        "${LSREGISTER}" -u "${app}" 2>/dev/null || true
        rm -rf "${app}"
    fi
done

ditto "${SOURCE_APP}" "${INSTALLED_APP}"
touch "${INSTALLED_APP}"
codesign --verify --deep --strict --verbose=2 "${INSTALLED_APP}"

DISPLAY_NAME="$(/usr/libexec/PlistBuddy -c \
    'Print :CFBundleDisplayName' "${INSTALLED_APP}/Contents/Info.plist")"
"${LSREGISTER}" -u "${SOURCE_APP}" 2>/dev/null || true
"${LSREGISTER}" -f "${INSTALLED_APP}"
open "${INSTALLED_APP}"

echo "Installed: ${INSTALLED_APP}"
echo "Application: ${DISPLAY_NAME} ${VERSION} (${ARCH})"
