#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

APP="build/Waveshare Link.app"
rm -rf build
mkdir -p "$APP/Contents/MacOS"
cp Info.plist "$APP/Contents/Info.plist"
swiftc -module-cache-path /tmp/clang-cache -O -swift-version 5 *.swift -o "$APP/Contents/MacOS/WaveshareLink"
codesign --force --sign - "$APP"

rm -rf "/Applications/Waveshare Link.app"
cp -R "$APP" "/Applications/"
echo "OK: /Applications/Waveshare Link.app"
