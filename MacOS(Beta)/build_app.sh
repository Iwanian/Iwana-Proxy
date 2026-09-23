#!/bin/bash
# build_app.sh — Builds Iwana Proxy as a Universal (Apple Silicon + Intel)
# .app bundle, so a single download works on every Mac model.
#
# Usage:
#   ./build_app.sh                # ad-hoc signed, runs on your own Mac
#   ./build_app.sh "Developer ID Application: Your Name (TEAMID)"
#                                  # signs with a real Developer ID for
#                                  # distributing to other people's Macs
#
# Requires: Xcode (or Xcode Command Line Tools) with Swift 5.9+, run on macOS.
set -euo pipefail
cd "$(dirname "$0")"

APP_NAME="IwanaProxy"
DISPLAY_NAME="Iwana Proxy"
BUILD_DIR=".build"
OUT_DIR="dist"
APP_BUNDLE="$OUT_DIR/$DISPLAY_NAME.app"
SIGN_IDENTITY="${1:--}"   # "-" = ad-hoc signing (default, works locally)

echo "==> Cleaning previous build"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

echo "==> Building universal binary (arm64 + x86_64), release configuration"
swift build -c release --arch arm64 --arch x86_64

BIN_PATH="$BUILD_DIR/apple/Products/Release/$APP_NAME"
if [ ! -f "$BIN_PATH" ]; then
    # Fallback path used by some toolchain versions.
    BIN_PATH=$(find "$BUILD_DIR" -type f -perm -u+x -name "$APP_NAME" | grep -i release | head -n1)
fi

echo "==> Verifying universal binary architectures"
lipo -info "$BIN_PATH"

echo "==> Assembling .app bundle at $APP_BUNDLE"
mkdir -p "$APP_BUNDLE/Contents/MacOS"
mkdir -p "$APP_BUNDLE/Contents/Resources"

cp "$BIN_PATH" "$APP_BUNDLE/Contents/MacOS/$APP_NAME"
cp Resources/Info.plist "$APP_BUNDLE/Contents/Info.plist"

if [ -f "Resources/AppIcon.icns" ]; then
    cp Resources/AppIcon.icns "$APP_BUNDLE/Contents/Resources/AppIcon.icns"
else
    echo "!! Resources/AppIcon.icns not found — run ./make_icon.sh first (app will use a generic icon)."
fi

echo "==> Code-signing ($SIGN_IDENTITY)"
codesign --force --deep --options runtime \
    --entitlements Resources/IwanaProxy.entitlements \
    --sign "$SIGN_IDENTITY" \
    "$APP_BUNDLE"

echo "==> Verifying signature"
codesign --verify --deep --strict --verbose=2 "$APP_BUNDLE" || true
spctl --assess --type execute "$APP_BUNDLE" || echo "(spctl assessment only passes for notarized, Developer-ID-signed builds — ad-hoc builds are expected to fail this check but still run fine locally)"

echo
echo "Done: $APP_BUNDLE"
echo "Run it with:  open \"$APP_BUNDLE\""
