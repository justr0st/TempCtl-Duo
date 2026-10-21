# For developers

The source layout, the tests and the release pipeline are described in the repository's `README.md`
(sections 11 and 7) and the design notes in `claude/tempctl-duo-spec.md`. This page is the short version.

## Build

PlatformIO, Arduino-ESP32 2.0.17 (`espressif32@6.9.0`), LVGL 9.6. Environments: `s3`, `cyd-1xlocal-1x485`,
`cyd-2xlocal`, `cyd-2xlocal-2x485`. `tools/web_embed.py` gzips the web page into `src/web_assets.h` before each
build (PlatformIO runs it).

## Tests without a board

| What | How |
|---|---|
| Programs, run engine, settings, PID, autotune, runaway, Modbus framing, image check, release manifest | `test/host_test.cpp` (g++, see the command in the README) |
| Touch screens | `test/ui_host/build.sh` builds the screens for the PC and renders screenshots (`./uihost`; `BOARD=cyd` for the 320×240 set; `./uihost monkey` random taps under AddressSanitizer) |
| Serial console | `test/console_host/` types a script of commands |

## Releases

Every push to `main` runs `.github/workflows/release.yml`: builds the four firmware envs and full images, the
desktop app (`tools/launcher/build.sh all`), the hosted page (`tools/build_site.py dist --web`, which also
renders this wiki into `/wiki/`) and the release files (`tools/release.py`), then deploys to Cloudflare
(`wrangler.jsonc`). `tools/release_local.sh` does the same from a PC without a push (`--dry`: build only).

## This wiki

- Pages: `docs/wiki/*.md` (GitHub shows them as they are), the page list: `docs/wiki/_Sidebar.md`.
- On the hosted page: `tools/wiki_site.py` turns them into `/wiki/*.html` (no packages: a small Markdown
  converter; links `Page.md` become `Page.html`).
- Pictures: `docs/wiki/img/` — regenerate them after a UI or wiring change with **`tools/wiki/make_shots.sh`**:

| Part | Made by |
|---|---|
| `img/wiring/*.svg` | `tools/wiki/wiring.py` (pins from `src/board_*.h`; keep them in step) |
| `img/touch/*.png` | the UI host (`test/ui_host`) in dark and light (`UIHOST_THEME=light`), framed by `tools/wiki/touch_png.py` |
| `img/web/*.webp` | `tools/wiki/shots_web.js` (Playwright) against a demo device, `tools/wiki/mock/server.py`, whose runs come from the firmware's own run engine (`tools/wiki/demo_run.cpp`) |

Theme-aware pictures use `<picture>` with a dark source; SVG diagrams follow the viewer's colour scheme by
themselves.
