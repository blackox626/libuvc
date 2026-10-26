#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
APP="$PWD/build/CameraPreview.app"
mkdir -p "$APP/Contents/MacOS"
cp tools/CameraPreview-Info.plist "$APP/Contents/Info.plist"
swiftc -module-cache-path build/swift-module-cache tools/camera-preview.swift \
  -o "$APP/Contents/MacOS/CameraPreview" -framework Cocoa -framework AVFoundation
codesign --force --sign - "$APP"
open "$APP"
