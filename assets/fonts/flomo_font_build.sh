#!/usr/bin/env bash
# Regenerate the flomo voice-memo app LVGL subset fonts from NotoSansSC-Regular.otf.
# Pinned converter: lv_font_conv@1.5.3 (npx --yes lv_font_conv@1.5.3).
# Character inventory comes from flomo_chars.txt (see assets/README.md, fonts section).
set -euo pipefail
cd "$(dirname "$0")"
RANGES="$(sed 's/^U+/0x/' flomo_chars.txt | paste -sd, -)"

for SIZE in 16 24; do
  npx --yes lv_font_conv@1.5.3 \
    --font NotoSansSC-Regular.otf \
    --size "$SIZE" \
    --bpp 4 \
    --format lvgl \
    --no-compress \
    --lv-include lvgl.h \
    --range "$RANGES" \
    --lv-font-name "flomo_font_${SIZE}" \
    -o "flomo_font_${SIZE}.c"
done
