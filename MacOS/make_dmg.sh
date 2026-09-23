#!/bin/bash
# make_dmg.sh — Packages dist/Iwana Proxy.app into a distributable .dmg
# with an Applications-folder shortcut, like most macOS downloads.
# Run after build_app.sh.
set -euo pipefail
cd "$(dirname "$0")"

APP="dist/Iwana Proxy.app"
DMG_NAME="IwanaProxy-macOS-universal.dmg"
STAGING="dist/dmg-staging"

if [ ! -d "$APP" ]; then
    echo "Build the app first: ./build_app.sh" >&2
    exit 1
fi

rm -rf "$STAGING" "dist/$DMG_NAME"
mkdir -p "$STAGING"
cp -R "$APP" "$STAGING/"
ln -s /Applications "$STAGING/Applications"

hdiutil create -volname "Iwana Proxy" \
    -srcfolder "$STAGING" \
    -ov -format UDZO \
    "dist/$DMG_NAME"

rm -rf "$STAGING"
echo "Done: dist/$DMG_NAME"
