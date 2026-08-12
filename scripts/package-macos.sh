#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-package-macos}"
DIST_DIR="${DIST_DIR:-${ROOT_DIR}/dist}"
DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-14.0}"

version_is_greater() {
    awk -v lhs="$1" -v rhs="$2" 'BEGIN {
        split(lhs, l, "."); split(rhs, r, ".");
        lv = (l[1] + 0) * 1000000 + (l[2] + 0) * 1000 + (l[3] + 0);
        rv = (r[1] + 0) * 1000000 + (r[2] + 0) * 1000 + (r[3] + 0);
        exit !(lv > rv);
    }'
}

mach_o_minimum_version() {
    otool -l "$1" | awk '
        $1 == "cmd" && $2 == "LC_BUILD_VERSION" { build = 1; legacy = 0; next }
        $1 == "cmd" && $2 == "LC_VERSION_MIN_MACOSX" { legacy = 1; build = 0; next }
        build && $1 == "minos" { print $2; exit }
        legacy && $1 == "version" { print $2; exit }
    '
}

for tool in cmake ctest ditto hdiutil codesign otool file; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "Required packaging tool is missing: ${tool}" >&2
        exit 2
    fi
done

if [[ -z "${QT_PREFIX:-}" || -z "${LIBRAW_PREFIX:-}" ||
      -z "${TIFF_PREFIX:-}" ]]; then
    if ! command -v brew >/dev/null 2>&1; then
        echo "Set QT_PREFIX, LIBRAW_PREFIX and TIFF_PREFIX, or install Homebrew" >&2
        exit 2
    fi
    QT_PREFIX="${QT_PREFIX:-$(brew --prefix qt@6)}"
    LIBRAW_PREFIX="${LIBRAW_PREFIX:-$(brew --prefix libraw)}"
    TIFF_PREFIX="${TIFF_PREFIX:-$(brew --prefix libtiff)}"
fi
MACDEPLOYQT="${QT_PREFIX}/bin/macdeployqt"
QT_PLUGIN_DIR="$(${QT_PREFIX}/bin/qtpaths --plugin-dir)"

if [[ ! -x "${MACDEPLOYQT}" ]]; then
    echo "macdeployqt was not found at ${MACDEPLOYQT}" >&2
    exit 2
fi

# Qt 6.9 added -no-codesign, while the macOS 14 CI intentionally uses Qt
# 6.8 LTS. Packaging signs the completed bundle below, so older deploy tools
# can safely use their default ad-hoc behavior during dependency copying.
declare -a macdeployqt_signing_arguments=()
if "${MACDEPLOYQT}" -help 2>&1 | grep -q -- '-no-codesign'; then
    macdeployqt_signing_arguments=(-no-codesign)
fi

cmake_arguments=(
    -S "${ROOT_DIR}"
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}"
    -DBUILD_TESTING=ON
    -DBUILD_SAMPLE_TOOLS=OFF
)
if [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
    cmake_arguments+=("-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
fi
if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    cmake_arguments+=("-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH//:/;}")
fi
if [[ -n "${VCPKG_TARGET_TRIPLET:-}" ]]; then
    cmake_arguments+=("-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}")
fi
if [[ -n "${VCPKG_OVERLAY_TRIPLETS:-}" ]]; then
    cmake_arguments+=("-DVCPKG_OVERLAY_TRIPLETS=${VCPKG_OVERLAY_TRIPLETS}")
fi
if [[ -n "${VCPKG_INSTALLED_DIR:-}" ]]; then
    cmake_arguments+=("-DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}")
fi
cmake "${cmake_arguments[@]}"
cmake --build "${BUILD_DIR}" --parallel
ctest --test-dir "${BUILD_DIR}" --output-on-failure

BUILT_APP="${BUILD_DIR}/StarProcessor.app"
if [[ ! -d "${BUILT_APP}" ]]; then
    echo "Built app bundle was not found: ${BUILT_APP}" >&2
    exit 3
fi

VERSION="$(/usr/libexec/PlistBuddy -c \
    'Print :CFBundleShortVersionString' "${BUILT_APP}/Contents/Info.plist")"
ARCH="$(uname -m)"
PACKAGE_NAME="StarProcessor-v${VERSION}-macOS-${ARCH}"
STAGING_DIR="${DIST_DIR}/.${PACKAGE_NAME}-staging"
DMG_ROOT="${STAGING_DIR}/dmg"
STAGED_APP="${DMG_ROOT}/StarProcessor.app"
DMG_PATH="${DIST_DIR}/${PACKAGE_NAME}.dmg"

cmake -E make_directory "${DIST_DIR}"
cmake -E rm -rf "${STAGING_DIR}"
cmake -E make_directory "${DMG_ROOT}"
ditto "${BUILT_APP}" "${STAGED_APP}"

"${MACDEPLOYQT}" "${STAGED_APP}" \
    -always-overwrite \
    -no-plugins \
    "${macdeployqt_signing_arguments[@]}" \
    -libpath="${LIBRAW_PREFIX}/lib" \
    -libpath="${TIFF_PREFIX}/lib"

# macdeployqt otherwise copies every image/input plugin associated with QtGui,
# including QML and virtual-keyboard stacks this Widgets application never
# loads. Keep only the runtime surface StarProcessor actually uses.
declare -a qt_plugins=(
    "platforms/libqcocoa.dylib"
    "styles/libqmacstyle.dylib"
    "imageformats/libqjpeg.dylib"
)
for plugin in "${qt_plugins[@]}"; do
    source_plugin="${QT_PLUGIN_DIR}/${plugin}"
    staged_plugin="${STAGED_APP}/Contents/PlugIns/${plugin}"
    if [[ ! -f "${source_plugin}" ]]; then
        echo "Required Qt plugin was not found: ${source_plugin}" >&2
        exit 3
    fi
    cmake -E make_directory "$(dirname "${staged_plugin}")"
    ditto "${source_plugin}" "${staged_plugin}"
    "${MACDEPLOYQT}" "${STAGED_APP}" \
        -always-overwrite \
        -no-plugins \
        "${macdeployqt_signing_arguments[@]}" \
        -executable="${staged_plugin}" \
        -libpath="${LIBRAW_PREFIX}/lib" \
        -libpath="${TIFF_PREFIX}/lib"
done

external_dependency_found=0
actual_deployment_target="${DEPLOYMENT_TARGET}"
while IFS= read -r -d '' binary; do
    if ! file "${binary}" | grep -q 'Mach-O'; then
        continue
    fi

    binary_minimum="$(mach_o_minimum_version "${binary}")"
    if [[ -n "${binary_minimum}" ]] && \
       version_is_greater "${binary_minimum}" "${actual_deployment_target}"; then
        actual_deployment_target="${binary_minimum}"
    fi

    install_name="$(otool -D "${binary}" 2>/dev/null | sed -n '2p' || true)"
    while IFS= read -r dependency; do
        [[ -z "${dependency}" ]] && continue
        [[ "${dependency}" == "${install_name}" ]] && continue
        case "${dependency}" in
            @*|/System/*|/usr/lib/*) ;;
            *)
                echo "External dependency remains: ${binary} -> ${dependency}" >&2
                external_dependency_found=1
                ;;
        esac
    done < <(otool -L "${binary}" | tail -n +2 | awk '{print $1}')
done < <(find "${STAGED_APP}" -type f -print0)

if [[ "${external_dependency_found}" -ne 0 ]]; then
    exit 4
fi

if version_is_greater "${actual_deployment_target}" "${DEPLOYMENT_TARGET}"; then
    echo "Warning: bundled dependencies raise the minimum macOS version from" \
         "${DEPLOYMENT_TARGET} to ${actual_deployment_target}." >&2
fi

# The deployment target requested from CMake can be lower than the prebuilt
# Homebrew dependencies. Advertise the highest real requirement so Finder does
# not offer an application that dyld cannot start.
/usr/libexec/PlistBuddy -c \
    "Set :LSMinimumSystemVersion ${actual_deployment_target}" \
    "${STAGED_APP}/Contents/Info.plist"

# macdeployqt may revisit dependencies while deploying plugins. Sign only
# after every path rewrite and Info.plist update is complete.
codesign --force --deep --sign - --timestamp=none "${STAGED_APP}"
codesign --verify --deep --strict --verbose=2 "${STAGED_APP}"

if [[ "${SKIP_LAUNCH_TEST:-0}" != "1" ]]; then
    SCREENSHOT="${STAGING_DIR}/launch-check.png"
    "${STAGED_APP}/Contents/MacOS/StarProcessor" \
        "--screenshot=${SCREENSHOT}"
    if [[ ! -s "${SCREENSHOT}" ]]; then
        echo "Packaged application did not create its launch-check screenshot" >&2
        exit 5
    fi
fi

ln -s /Applications "${DMG_ROOT}/Applications"
ditto "${ROOT_DIR}/LICENSE" "${DMG_ROOT}/LICENSE.txt"
ditto "${ROOT_DIR}/THIRD_PARTY_NOTICES.md" \
    "${DMG_ROOT}/THIRD_PARTY_NOTICES.md"
printf '%s\n' \
    "StarProcessor ${VERSION}" \
    "Platform: macOS ${ARCH}" \
    "Requested deployment target: macOS ${DEPLOYMENT_TARGET}" \
    "Actual bundled minimum: macOS ${actual_deployment_target}" \
    "Signing: ad-hoc (not notarized)" \
    "Commit: $(git -C "${ROOT_DIR}" rev-parse HEAD)" \
    > "${DMG_ROOT}/BUILD-INFO.txt"

cmake -E rm -f "${DMG_PATH}" "${DMG_PATH}.sha256"
if [[ "${SKIP_DMG_CREATE:-0}" == "1" ]]; then
    echo "macOS staging directory: ${DMG_ROOT}"
    exit 0
fi

hdiutil create \
    -volname "StarProcessor ${VERSION}" \
    -srcfolder "${DMG_ROOT}" \
    -format UDZO \
    -ov "${DMG_PATH}"
DMG_FILE="$(basename "${DMG_PATH}")"
(
    cd "${DIST_DIR}"
    shasum -a 256 "${DMG_FILE}" > "${DMG_FILE}.sha256"
)

echo "macOS package: ${DMG_PATH}"
echo "Checksum: ${DMG_PATH}.sha256"
