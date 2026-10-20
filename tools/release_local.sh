#!/bin/bash
# A release from this PC (spec 9.13), without commit / push: the same steps as .github/workflows/release.yml,
# deployed with your own Cloudflare login. Windows: Git Bash, started from the PlatformIO terminal of VS Code
# (so `pio` is found). Needs Node.js (npx wrangler; the first time `npx wrangler login` opens the browser) and,
# for the desktop app, Go (without Go: --no-app, then the page lists firmware only).
#   bash tools/release_local.sh [--no-app] [--dry]      --dry: build dist/ only, deploy nothing
set -e
cd "$(dirname "$0")/.."
ENVS="s3 cyd-1xlocal-1x485 cyd-2xlocal cyd-2xlocal-2x485"
NOAPP=""; DRY=""
for a in "$@"; do case "$a" in --no-app) NOAPP=--no-app;; --dry) DRY=1;; *) echo "unknown: $a"; exit 1;; esac; done
# Python and packages: the ones PlatformIO itself uses (pio system info: its python and core folder;
# pio may come from a normal Python install or from PlatformIO's own penv)
[ -n "$NOAPP" ] || command -v go >/dev/null || {
  echo "Go not found (needed for the desktop app): install it (winget install GoLang.Go, then a new terminal) or add --no-app"; exit 1; }
command -v pio >/dev/null || { echo "pio not found: start Git Bash from the PlatformIO terminal of VS Code"; exit 1; }
INFO=$(pio system info --json-output 2>/dev/null || true)
jv() { printf '%s' "$INFO" | grep -o "\"$1\": *{[^}]*}" | sed 's/.*"value": *"\(.*\)".*/\1/' | sed 's#\\\\#/#g'; }
PY=$(jv python_exe); CORE="${PLATFORMIO_CORE_DIR:-$(jv core_dir)}"
if [ -z "$PY" ] || [ ! -f "$PY" ]; then  # (older pio: look next to it)
  D=$(cd "$(dirname "$(command -v pio)")" && pwd); PY=""
  for p in "$D/python.exe" "$D/../python.exe" "$D/python" "$D/python3"; do [ -f "$p" ] && { PY="$p"; break; }; done
fi
[ -n "$CORE" ] || CORE="$HOME/.platformio"
[ -n "$PY" ] && [ -f "$PY" ] || { echo "PlatformIO's python not found (pio system info: $INFO)"; exit 1; }
echo "PlatformIO: $CORE  python: $PY"

args=""; for e in $ENVS; do args="$args -e $e"; done
pio run $args

rm -rf art && mkdir -p art
BA="$CORE/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
ESPTOOL="$CORE/packages/tool-esptoolpy/esptool.py"
for f in "$BA" "$ESPTOOL"; do [ -f "$f" ] || { echo "missing: $f"; exit 1; }; done
for e in $ENVS; do
  B=.pio/build/$e; O=art/fw/$e; mkdir -p $O
  cp $B/firmware.bin $O/firmware.bin
  if [ "$e" = s3 ]; then
    CHIP="--chip esp32s3"; FL="--flash_mode qio --flash_freq 80m --flash_size 16MB"; BOOT=0x0
  else
    CHIP="--chip esp32"; FL="--flash_mode dio --flash_freq 40m --flash_size 4MB"; BOOT=0x1000
  fi
  "$PY" "$ESPTOOL" $CHIP merge_bin -o $O/full.bin $FL \
    $BOOT $B/bootloader.bin 0x8000 $B/partitions.bin 0xe000 "$BA" 0x10000 $B/firmware.bin
done

if [ -z "$NOAPP" ]; then
  PY="$PY" bash tools/launcher/build.sh all
  V=$(sed -n 's/^#define FW_VERSION "\(.*\)".*/\1/p' src/version.h | tr -d '\r')
  mkdir -p art/app && cp tools/launcher/dist/tempctl-duo-$V* art/app/
fi

"$PY" tools/build_site.py dist --web
"$PY" tools/release.py dist art $NOAPP
[ -n "$DRY" ] && { echo "dry run: dist/ is ready, nothing deployed"; exit 0; }
# the deployment list in Cloudflare: the version (tag) and "local build" with the commit it started from
V=$(sed -n 's/^#define FW_VERSION "\(.*\)".*/\1/p' src/version.h | tr -d '\r')
C=$(git rev-parse --short HEAD 2>/dev/null || echo "?")
[ -n "$(git status --porcelain --untracked-files=no 2>/dev/null)" ] && C="$C + uncommitted changes"
npx --yes wrangler@4 deploy --tag "v$V" --message "$V local build from $C"
