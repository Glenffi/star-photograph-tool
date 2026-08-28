#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RESOURCE_DIR="${ROOT_DIR}/resources"
SOURCE_PNG="${RESOURCE_DIR}/StarProcessor.png"
command -v magick >/dev/null
python3 -c 'from PIL import Image' >/dev/null

# Public brand: a dark instrument plate, two registration frames and one
# calibrated point. Keep the old filenames for updater/build compatibility.
magick -size 1024x1024 xc:none \
    -fill '#1A1E1F' -stroke '#414849' -strokewidth 22 \
    -draw 'roundrectangle 80,80 944,944 190,190' \
    -fill '#FFFFFF08' -stroke none \
    -draw 'roundrectangle 104,104 920,470 160,160' \
    -fill none -stroke '#59605F' -strokewidth 34 \
    -draw 'roundrectangle 320,288 768,736 96,96' \
    -fill none -stroke '#777A74' -strokewidth 34 \
    -draw 'roundrectangle 256,224 704,672 96,96' \
    -fill none -stroke '#D5AA67' -strokewidth 54 \
    -draw "path 'M 592,176 L 192,176 Q 144,176 144,224 L 144,624 Q 144,672 192,672 L 592,672'" \
    -fill '#78A8A233' -stroke none -draw 'circle 448,432 520,432' \
    -fill '#78A8A2' -draw 'circle 448,432 486,432' \
    -depth 8 -strip "${SOURCE_PNG}"

python3 -c \
    'from PIL import Image; import sys; Image.open(sys.argv[1]).save(sys.argv[2], format="ICNS", sizes=[(16,16),(32,32),(64,64),(128,128),(256,256),(512,512),(1024,1024)])' \
    "${SOURCE_PNG}" "${RESOURCE_DIR}/StarProcessor.icns"

magick "${SOURCE_PNG}" \
    -define icon:auto-resize=256,128,64,48,32,16 \
    "${RESOURCE_DIR}/StarProcessor.ico"

WEBSITE_ICON="${ROOT_DIR}/deploy/web/starprocessor/assets/StarProcessor.png"
PROTOTYPE_ICON="${ROOT_DIR}/../star-photograph-tool-design/web-prototype-v2/assets/logo.png"
if [[ -d "$(dirname "${WEBSITE_ICON}")" ]]; then
    cp "${SOURCE_PNG}" "${WEBSITE_ICON}"
fi
if [[ -d "$(dirname "${PROTOTYPE_ICON}")" ]]; then
    cp "${SOURCE_PNG}" "${PROTOTYPE_ICON}"
fi

echo "Generated Nebulens PNG, ICNS and ICO assets."
