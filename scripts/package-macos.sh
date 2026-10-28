#!/usr/bin/env bash
# Package the macOS simulator build as dist/Kickray-macOS.zip (Kickray.app).
#
#   bash scripts/package-macos.sh [build dir]     (default: Kick/build/sim)
#
# Puts assets/, demos/ and templates/ into Kickray.app/Contents/Resources (see kick_paths.cpp),
# names the app "Kickray" in Finder and signs it ad hoc: Apple Silicon refuses to start an app
# whose contents changed after linking ("damaged"). The app is not notarized – see README.
set -euo pipefail
REPO_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
BUILD_DIR="${1:-$REPO_DIR/Kick/build/sim}"
DIST="$REPO_DIR/dist"

APP_SRC="$(find "$BUILD_DIR" -maxdepth 6 -type d -name "KT_KickEM.app" | head -n 1)"
[ -n "$APP_SRC" ] || { echo "KT_KickEM.app not found below $BUILD_DIR" >&2; exit 1; }

rm -rf "$DIST/Kickray.app" "$DIST/Kickray-macOS.zip"
mkdir -p "$DIST"
cp -R "$APP_SRC" "$DIST/Kickray.app"
RES="$DIST/Kickray.app/Contents/Resources"
mkdir -p "$RES"
for d in assets demos templates; do
  rm -rf "$RES/$d"
  cp -R "$REPO_DIR/Kick/$d" "$RES/$d"
done

PLIST="$DIST/Kickray.app/Contents/Info.plist"
plutil -replace CFBundleName -string "Kickray" "$PLIST"
plutil -replace CFBundleDisplayName -string "Kickray" "$PLIST"

codesign --force --deep --sign - "$DIST/Kickray.app"
codesign --verify --deep --strict "$DIST/Kickray.app"
lipo -archs "$DIST/Kickray.app/Contents/MacOS/KT_KickEM"

( cd "$DIST" && ditto -c -k --keepParent Kickray.app Kickray-macOS.zip )
echo "Packaged: $DIST/Kickray-macOS.zip"
