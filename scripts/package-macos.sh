#!/usr/bin/env bash
# Package the macOS simulator build as dist/AwesomeKick-macOS.zip (AwesomeKick.app).
#
#   bash scripts/package-macos.sh [build dir]     (default: Kick/build/sim)
#
# Puts assets/, demos/ and templates/ into AwesomeKick.app/Contents/Resources (see kick_paths.cpp),
# names the app "AwesomeKick" in Finder and signs it ad hoc: Apple Silicon refuses to start an app
# whose contents changed after linking ("damaged"). The app is not notarized – see README.
set -euo pipefail
REPO_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
BUILD_DIR="${1:-$REPO_DIR/Kick/build/sim}"
DIST="$REPO_DIR/dist"

APP_SRC="$(find "$BUILD_DIR" -maxdepth 6 -type d -name "KT_KickEM.app" | head -n 1)"
[ -n "$APP_SRC" ] || { echo "KT_KickEM.app not found below $BUILD_DIR" >&2; exit 1; }

rm -rf "$DIST/AwesomeKick.app" "$DIST/AwesomeKick-macOS.zip"
mkdir -p "$DIST"
cp -R "$APP_SRC" "$DIST/AwesomeKick.app"
RES="$DIST/AwesomeKick.app/Contents/Resources"
mkdir -p "$RES"
for d in assets demos templates; do
  rm -rf "$RES/$d"
  cp -R "$REPO_DIR/Kick/$d" "$RES/$d"
done

PLIST="$DIST/AwesomeKick.app/Contents/Info.plist"
plutil -replace CFBundleName -string "AwesomeKick" "$PLIST"
plutil -replace CFBundleDisplayName -string "AwesomeKick" "$PLIST"

codesign --force --deep --sign - "$DIST/AwesomeKick.app"
codesign --verify --deep --strict "$DIST/AwesomeKick.app"
lipo -archs "$DIST/AwesomeKick.app/Contents/MacOS/KT_KickEM"

( cd "$DIST" && ditto -c -k --keepParent AwesomeKick.app AwesomeKick-macOS.zip )
echo "Packaged: $DIST/AwesomeKick-macOS.zip"
