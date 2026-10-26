#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cmake --build build -j 4
APP="$PWD/build/UVCExample.app"
mkdir -p "$APP/Contents/MacOS"
cp build/example "$APP/Contents/MacOS/example"
cp tools/Example-Info.plist "$APP/Contents/Info.plist"
codesign --force --sign - "$APP"
open "$APP" --args --system-av "$@"
