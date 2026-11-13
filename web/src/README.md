# Web page sources

The web page is one file when it is served (`web/index.html`: HTML, CSS and one script). It is written here in
parts and joined by `tools/web_join.py`; `web/index.html` is generated and not in git.

- `index.html`: the page's HTML with include lines. A line `<!-- include: <file> -->` (the whole line, a path
  relative to `web/src/`) is replaced by that file's text. The include lines give the order.
- `css/`: the `<style>` content. `10-base.css` the page's own rules, `20-glass.css` the glass look on top of them.
- `js/`: the `<script>` content, cut along its section comments. **One global scope**: the parts are joined in the
  order of the include lines (the same as the numbers in the file names) and share their `const` / `let` /
  `function` names, so a part is not a module and cannot be checked or run on its own. Code that runs at load time
  (event handlers set on elements, `const` used by later parts) depends on that order; the start is the last line
  of `90-poll.js`.

| File | |
|---|---|
| `00-core.js` | `'use strict'`, `$`, `esc`, number / time formats, shared state, `api()` |
| `05-usb-connect.js` | USB transport (Web Serial), the connect screen off the device, downloads, `ask()` / `note()` |
| `10-theme.js` | theme, accent and channel colours, menu |
| `15-programs.js` | programs: stored, drafts, undo / redo |
| `20-chart-model.js` | the chart's model (live, run, preview) |
| `21-chart-raw.js` | every reading (`/api/raw`) |
| `22-chart-draw.js` | chart drawing, zoom, handles |
| `23-chart-hover.js` | hover line and value box (shared with the CSV replay) |
| `30-cards.js`, `31-autotune.js`, `32-sidebar.js` | channel cards, autotune, the sidebar |
| `40-editor.js`, `41-reorder.js` | program editor, reorder by drag |
| `45-runbar.js` | start with unsaved changes, run bar, overlays |
| `50-eventlog.js`, `55-replay.js` | event log, CSV replay |
| `60-pages.js`, `61-forms.js` | page routes, settings forms |
| `62-programs-page.js` … `67-logs.js` | Programs, Channels, System, firmware update, Tools (+ SD backups), system log and SD run logs |
| `90-poll.js` | polling, start-up |

New part: a file in `css/` or `js/` and its include line in `index.html` at its place.

## Building

```
python tools/web_join.py            web/src/ -> web/index.html (written only when it changes)
python tools/web_join.py --check    exit 1 if web/index.html is missing or not the join result
```

Every tool that uses the page joins it first: the firmware build (`tools/web_embed.py`, PlatformIO pre-script),
`tools/build_site.py` (launcher, hosted page, demo, web page tests), the wiki's demo device
(`tools/wiki/mock/server.py`: again when a part changed). After editing a part, reload the mock device's page or
run one of them; `node --check` works on the joined script only.
