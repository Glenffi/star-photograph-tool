#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAMPLE_ROOT="${SCRIPT_DIR}/../star-photograph-tool-samples"

if [ $# -gt 0 ] && [[ "$1" != --* ]]; then
    SAMPLE_ROOT="$1"
    shift
fi

cmake -S "${SCRIPT_DIR}" -B "${SCRIPT_DIR}/build" \
    -DBUILD_SAMPLE_TOOLS=ON -DBUILD_TESTING=ON
cmake --build "${SCRIPT_DIR}/build" --target StarProcessorSampleRegression --parallel

"${SCRIPT_DIR}/build/StarProcessorSampleRegression" \
    --samples "${SAMPLE_ROOT}" \
    --output "${SCRIPT_DIR}/build/sample-regression-output" \
    "$@"
