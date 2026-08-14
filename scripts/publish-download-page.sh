#!/usr/bin/env bash

# Publish the static StarProcessor download page without touching packages or
# update.json. Authentication is delegated to ssh-agent or the SSH prompt.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPDATE_SERVER="${UPDATE_SERVER:-root@118.196.0.184}"
UPDATE_ROOT="${UPDATE_ROOT:-/www/wwwroot/starprocessor}"
PAGE_ROOT="${ROOT_DIR}/deploy/web/starprocessor"
REMOTE_STAGE="${UPDATE_ROOT}/.site-uploading"

required_files=(
    "index.html"
    "assets/download.css"
    "assets/download.js"
    "assets/StarProcessor.png"
    "assets/app-workspace.png"
)

for relative_path in "${required_files[@]}"; do
    if [[ ! -f "${PAGE_ROOT}/${relative_path}" ]]; then
        echo "Missing download-page asset: ${relative_path}" >&2
        exit 2
    fi
done

ssh "${UPDATE_SERVER}" \
    "install -d -m 0755 '${REMOTE_STAGE}/assets' '${UPDATE_ROOT}/assets'"

scp "${PAGE_ROOT}/index.html" \
    "${UPDATE_SERVER}:${REMOTE_STAGE}/index.html"
scp "${PAGE_ROOT}/assets/download.css" \
    "${PAGE_ROOT}/assets/download.js" \
    "${PAGE_ROOT}/assets/StarProcessor.png" \
    "${PAGE_ROOT}/assets/app-workspace.png" \
    "${UPDATE_SERVER}:${REMOTE_STAGE}/assets/"

# Assets become visible first; index.html is switched last so visitors never
# receive a page that references files still in transit.
ssh "${UPDATE_SERVER}" \
    "chmod 0644 '${REMOTE_STAGE}/index.html' '${REMOTE_STAGE}/assets/'* && \
     mv -f '${REMOTE_STAGE}/assets/'* '${UPDATE_ROOT}/assets/' && \
     mv -f '${REMOTE_STAGE}/index.html' '${UPDATE_ROOT}/index.html' && \
     rmdir '${REMOTE_STAGE}/assets' '${REMOTE_STAGE}'"

echo "Published download page: https://di.nexusgen.net/starprocessor/"
