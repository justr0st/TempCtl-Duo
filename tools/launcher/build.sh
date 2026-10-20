#!/bin/bash
# Builds the TempCtl Duo launcher (spec 7b): the page from web/ (tools/build_site.py) embedded in one
# executable per platform, named with the project version (src/version.h). Needs Go 1.21+.
#   tools/launcher/build.sh            -> tools/launcher/dist/tempctl-duo-<version>.exe (Windows, 64 bit)
#   tools/launcher/build.sh all        -> + macOS (arm64 / amd64) and Linux (amd64)
set -e
cd "$(dirname "$0")"
V=$(sed -n 's/^#define FW_VERSION "\(.*\)"/\1/p' ../../src/version.h)
"${PY:-python3}" ../build_site.py site   # (PY: tools/release_local.sh hands over PlatformIO's python)
mkdir -p dist
LD="-s -w -X main.version=$V"
GOOS=windows GOARCH=amd64 go build -trimpath -ldflags "$LD -H windowsgui" -o "dist/tempctl-duo-$V.exe" .
echo "dist/tempctl-duo-$V.exe"
if [ "$1" = all ]; then
  GOOS=darwin GOARCH=arm64 go build -trimpath -ldflags "$LD" -o "dist/tempctl-duo-$V-macos-arm64" .
  GOOS=darwin GOARCH=amd64 go build -trimpath -ldflags "$LD" -o "dist/tempctl-duo-$V-macos-x64" .
  GOOS=linux GOARCH=amd64 go build -trimpath -ldflags "$LD" -o "dist/tempctl-duo-$V-linux-x64" .
  ls dist
fi
