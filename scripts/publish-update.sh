#!/usr/bin/env bash

# Publish exactly one StarProcessor release to the small update server.
# Authentication is intentionally delegated to ssh-agent or the interactive
# SSH prompt. Do not put a password in this file or in shell history.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPDATE_SERVER="${UPDATE_SERVER:-root@118.196.0.184}"
UPDATE_ROOT="${UPDATE_ROOT:-/www/wwwroot/starprocessor}"
UPDATE_BASE_URL="${UPDATE_BASE_URL:-https://di.nexusgen.net/starprocessor/downloads}"

if [[ $# -lt 3 ]]; then
    echo "Usage: $0 VERSION PLATFORM=PACKAGE PLATFORM=PACKAGE [-- notes]" >&2
    echo "Example: $0 0.9.0 windows-x64=dist/app.zip macos-arm64=dist/app.dmg" >&2
    exit 2
fi

version="$1"
shift
declare -a packages=()
release_notes="功能改进和问题修复。"
while [[ $# -gt 0 ]]; do
    if [[ "$1" == "--" ]]; then
        shift
        release_notes="${*:-${release_notes}}"
        break
    fi
    packages+=("$1")
    shift
done

if [[ ${#packages[@]} -eq 0 ]]; then
    echo "At least one platform package is required" >&2
    exit 2
fi

temporary_dir="$(mktemp -d)"
trap 'rm -rf "${temporary_dir}"' EXIT
manifest="${temporary_dir}/update.json"

python3 "${ROOT_DIR}/scripts/generate-update-manifest.py" \
    --version "${version}" \
    --base-url "${UPDATE_BASE_URL}" \
    --release-notes "${release_notes}" \
    --output "${manifest}" \
    "${packages[@]}"

ssh "${UPDATE_SERVER}" \
    "install -d -m 0755 '${UPDATE_ROOT}/downloads'"

declare -a keep_files=()
for specification in "${packages[@]}"; do
    package_path="${specification#*=}"
    package_name="$(basename "${package_path}")"
    if [[ ! "${package_name}" =~ ^StarProcessor[-A-Za-z0-9._]+\.(zip|dmg)$ ]]; then
        echo "Unexpected package name: ${package_name}" >&2
        exit 2
    fi
    keep_files+=("${package_name}")
    scp "${package_path}" \
        "${UPDATE_SERVER}:${UPDATE_ROOT}/downloads/${package_name}.uploading"
done
scp "${manifest}" "${UPDATE_SERVER}:${UPDATE_ROOT}/update.json.uploading"

# Activate complete packages first, then replace the manifest. A client can
# therefore never discover a package that is only partially uploaded.
for package_name in "${keep_files[@]}"; do
    ssh "${UPDATE_SERVER}" \
        "mv -f '${UPDATE_ROOT}/downloads/${package_name}.uploading' '${UPDATE_ROOT}/downloads/${package_name}'"
done
ssh "${UPDATE_SERVER}" \
    "chmod 0644 '${UPDATE_ROOT}/update.json.uploading' && mv -f '${UPDATE_ROOT}/update.json.uploading' '${UPDATE_ROOT}/update.json'"

# The host has deliberately limited storage. Remove prior StarProcessor
# packages only after the new manifest and packages are active.
keep_expression=""
for package_name in "${keep_files[@]}"; do
    keep_expression+=" ! -name '${package_name}'"
done
ssh "${UPDATE_SERVER}" \
    "find '${UPDATE_ROOT}/downloads' -maxdepth 1 -type f \\( -name 'StarProcessor*.zip' -o -name 'StarProcessor*.dmg' \\)${keep_expression} -delete"

echo "Published StarProcessor ${version} to ${UPDATE_BASE_URL}"
