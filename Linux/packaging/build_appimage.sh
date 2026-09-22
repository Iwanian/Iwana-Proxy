#!/usr/bin/env bash
# build_appimage.sh — Packages build/iwana-proxy into an AppImage.
# Requires linuxdeploy + linuxdeploy-plugin-gtk on PATH (see PORT_SPEC.md §0):
#   https://github.com/linuxdeploy/linuxdeploy
#   https://github.com/linuxdeploy/linuxdeploy-plugin-gtk
set -euo pipefail
cd "$(dirname "$0")/.."

APPDIR=build/AppDir
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp build/iwana-proxy "$APPDIR/usr/bin/iwana-proxy"
cp packaging/iwana-proxy.desktop "$APPDIR/usr/share/applications/"
cp assets/iwana_icon_256.png "$APPDIR/usr/share/icons/hicolor/256x256/apps/iwana-proxy.png"

if ! command -v linuxdeploy >/dev/null; then
    echo "linuxdeploy not found on PATH. Download it from:" >&2
    echo "  https://github.com/linuxdeploy/linuxdeploy/releases" >&2
    echo "  https://github.com/linuxdeploy/linuxdeploy-plugin-gtk/releases" >&2
    exit 1
fi

linuxdeploy --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/iwana-proxy" \
    --desktop-file "$APPDIR/usr/share/applications/iwana-proxy.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/iwana-proxy.png" \
    --plugin gtk \
    --output appimage

mv Iwana*.AppImage build/ 2>/dev/null || true
echo "AppImage written to build/"
