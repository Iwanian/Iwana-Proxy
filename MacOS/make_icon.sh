#!/bin/bash
# make_icon.sh — Builds Resources/AppIcon.icns from Resources/AppIcon-1024.png
# (which was upscaled from the Windows build's icon.ico — swap in a real
# 1024x1024 source PNG here for a crisper result if you have one).
# Run this once on macOS before build_app.sh; requires `sips` and `iconutil`
# (both ship with macOS, no install needed).
set -euo pipefail
cd "$(dirname "$0")"

SRC="Resources/AppIcon-1024.png"
ICONSET="Resources/AppIcon.iconset"

if [ ! -f "$SRC" ]; then
    echo "Missing $SRC" >&2
    exit 1
fi

rm -rf "$ICONSET"
mkdir -p "$ICONSET"

sizes=(16 32 128 256 512)
for s in "${sizes[@]}"; do
    sips -z "$s" "$s" "$SRC" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    s2=$((s * 2))
    sips -z "$s2" "$s2" "$SRC" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done

iconutil -c icns "$ICONSET" -o "Resources/AppIcon.icns"
rm -rf "$ICONSET"

echo "Wrote Resources/AppIcon.icns"
