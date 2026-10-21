# Serial console

Commands over the USB serial port at **115200 baud**: S3 the port marked **"USB"** (the log stays on "UART"),
CYD its one USB port (shared with the log). Any terminal works (PlatformIO's monitor, PuTTY, screen …); opening
the port never resets the board. The help is printed at the start and with `help`.

- One command per line; commands and keys are not case-sensitive. Parameters are `key=value`, a value with
  spaces in quotes: `s="My Net"`.
- Typed characters are echoed, Backspace works, Up / Down recall the last 20 commands.
- Errors start with `error:`. Passwords are never shown.
- `help <section>` prints one section (`help wifi`); `help settings` lists every setting with its range.

## Commands

| Command | |
|---|---|
| `status` · `watch` · `info` · `log [n]` | both channels and the run · the same as one line per second (any key stops) · firmware, uptime, memory, network · the last n events |
| `manual b on sp=150` · `manual t sp=180` · `manual b off` | manual setpoint (b = bottom, t = top); not for a channel of the running program |
| `ack b` | acknowledge a channel fault |
| `programs` · `program n=2` · `show` | list, select for the next run, print the program |
| `start` `pause` `resume` `stop` `next` `continue` | run control |
| `extend b +30` · `target t 230` | extend the running hold (s), change the current / kept target |
| `resume run` · `dismiss run` | after "Run interrupted by a reset" |
| `save` · `discard` | the changes made during the last run |
| `tune b start temp=200` · `tune b stop` · `tune b accept` · `tune b discard` · `tune b` | PID autotune of a local channel |
| `wifi scan` · `wifi connect n=2` · `wifi connect s="My Net"` | join a network; without `pw=` the password is asked for (hidden) |
| `wifi ap [pw=…]` · `wifi status` · `wifi forget` | access point, state, forget the network |
| `get [b\|t\|bus\|run\|wifi\|display\|<prefix>]` | settings as `key=value` lines |
| `set <key>=<value> …` | up to 8 settings at once (the web API's keys; `b.` / `t.` = the channels); a value out of range is an error and nothing is changed |
| `fw` · `fw sd` · `fw sd install yes` · `fw net` · `fw net install yes` | firmware: version and slot · update from the SD card (S3) · from the release page (S3) |
| `backup sd` · `backup sd list` · `backup sd restore n=1 [settings\|programs] yes` · `backup sd delete n=1 yes` | backups on the SD card (S3) |
| `screen restart` · `screen check` | set the display and touch up again · what the automatic screen check sees |
| `perf on` · `perf off` · `perf simd on\|off` | frame rate meter ([Troubleshooting](Troubleshooting.md#display-speed)) · S3: the SIMD drawing routines |
| `touch cal` | CYD: calibrate the touch screen (tap 4 crosses) |
| `factory reset yes` | all settings to their defaults (programs kept), then a restart |
| `reboot` | restart (refused while a program runs) |
| `api on` · `api off` | machine mode for the desktop app (JSON lines) |

## Examples

```
set display.spiMhz=40
set b.local.median=on b.local.medianN=5
set time.zone=Europe/Berlin
get b.local
```

## API mode

`api on` switches the console to JSON lines — what the desktop app uses. Each request
`{"id":7,"m":"GET","p":"/api/live"}` goes through the same handlers as the HTTP API, with the same paths and
JSON; answers come back as `{"id":7,"code":200,"body":{…}}`. `api off`, or 15 s without a request, returns to
the console. `help api` shows a short description on the device.
