# Web API

The device's HTTP API — what the web page, the desktop app and the browser demo use. Everything the page can do
goes through it, so your own scripts can do the same. JSON in and out.

The flat fields of `/api/status` (kept from TempCtl 1.2) show the **bottom channel**; everything else
covers both (`ch` = 0 bottom, 1 top). The serial console's API mode ([below](#over-usb-the-serial-api-mode)) reaches the same paths.

## Request checks

There is no password, but other web pages must not reach the device: every **POST needs the
header `X-TempCtl: 1`**, otherwise 403. A browser only sends such a header cross-site after a CORS check,
which the device never answers, so a script on some other website open on a PC in the workshop can't
switch anything. Own scripts add it, e.g.
`curl -H "X-TempCtl: 1" -H "Content-Type: application/json" -d '{"sp":150}' http://tempctl-duo.local/api/manual`
(PowerShell: `-Headers @{"X-TempCtl"="1"}`). The **`Host` header** must be the device: an IP address, its
host name, or the host name with a local suffix (`.local`, `.lan`, `.home`, `.fritz.box`, … see
`src/host_check.h`); other names get 403 (DNS rebinding), each refused name is logged once. The serial
console's API mode has no such checks.

> The web page has no login. Keep the device on a trusted network.

## Endpoints

| Method | Path | Body / query |
|---|---|---|
| GET | `/api/live` | web page, polled every second: both channels (pv, sp, out, outOn (outputs on), state, target, kept, canExtend, manual, fault, autotune, …), run (state, el, rem, phase / steps, progPause, actionRequired, pauseText, edited, msg; `intr` while a run interrupted by a reset can be resumed: reason, exact, el, i / st / left per channel), `sel`, `accent`, `progGen`, `cfgGen` (changes with every settings save), `logSeq`, `sysSeq` (newest system log line), `boot` (start number), `time` (device clock, UTC s; 0 = unknown), `tz` (time zone, "" = not set), `sd` (S3: card, logging, file, err); `mon` while the monitor channel is on: name, pv, raw, cold, err; `?a0=&a1=[&a2=]` (device millis, as `/api/raw`): also `raw` [[ms, pv in 0.1 °C or null, …], …] = the PV readings after them, up to 32 per channel (a2: a third array, the monitor's; more: `rawMore`, the page then uses `/api/raw`) |
| GET | `/api/chart?v=run` | current / last run: 300 bins of `pv`, `sp` and state lanes `st` per channel, past events `ev` [t, type, ch, idx], forecast `fc` (`seg` [t0, t1, v0, v1, src, flags] per channel, `ev`; times from now) |
| GET | `/api/chart?v=live` | the live period (`display.liveMin`) in 300 points of `pv` / `sp` / `dlo` / `dhi` per channel; `bin` s per point, `span`, `sh` (the points before the newest end `sh` s later than whole bins before now), `have` / `cap` (s recorded / s the history holds), `ms` (device millis), `raw` (readings kept per channel; run view: also `ra` / `rn`, run time against millis). Both views: while the monitor channel is on, `pv` / `dlo` / `dhi` have a third row (its PV) and `mon` its name |
| GET | `/api/raw?a0=&a1=[&a2=]&win=&max=` | every PV reading of both channels after `a0` / `a1` (device millis), at most `win` s old, `max` (8192) per channel. Binary, little-endian: uint32 now, n0, n1, 1; then 8-byte records (with `a2` on a board with the monitor channel: uint32 now, n0, n1, 2, n2, then n0 + n1 + n2 records) uint32 millis, int16 PV in 0.1 °C (−32768 = no reading). Wi-Fi only. The web chart draws these; S3 keeps 131 072 per channel (2 MB PSRAM: 36 h at 1 s polling, 9 h at 250 ms), CYD 600 (10 min at 1 s) |
| GET | `/api/log?since=n` | event log entries after n: [seq, uptime s, run time s or null, text, kind, epoch] (kind 1 event, 2 run, 3 warning, 4 error; epoch UTC s, 0 = clock unknown) |
| GET | `/api/syslog?since=n` | system log lines after n (about 12 kB per answer, `more`: ask again): [seq, uptime s, epoch, kind (0 technical … 4 error), text]; also `first`, `boot` |
| GET | `/api/sd` | SD run logs (S3): `slot`, `card`, `logging`, `file`, `err`, `totalKb`, `freeKb`, `files` [[name, bytes, mtime], …] newest first |
| GET | `/api/sd/file?name=` | one run log (CSV; up to 4 MB through the web) |
| POST | `/api/sd/delete` | `{"name":…}` deletes a run log (not the one being written) |
| GET | `/api/backup` | SD backups (S3): `slot`, `card`, `err`, `totalKb`, `freeKb`, `files` [[name, bytes, mtime], …] newest first |
| POST | `/api/backup` | `{}` writes a new backup → `{"ok":true,"name":…}` |
| GET | `/api/backup/info?name=` | what a backup holds: `time`, `fw`, `settings`, `programs` (count), `slots` ("1, 2, 5"), `mask` (bit k = slot k) |
| POST | `/api/backup/restore` | `{"name":…,"settings":true,"programs":true}` → `{"ok":true,"msg":…}` (or `err`; not during a run) |
| POST | `/api/backup/delete` | `{"name":…}` deletes a backup |
| GET | `/api/backup/file?name=` | the backup file (JSON) |
| GET | `/api/eventlog.txt` | the events saved in flash, all starts: lines `boot \t epoch \t uptime \t kind \t text` |
| POST | `/api/time` | `{"t":<UTC s>}`: sets the device clock if it has no NTP time (the page does it; for the log); `{"zone":…, "fixed":"UTC+02:00"}`: the time zone, only while none is set |
| GET | `/api/timezones` | `{"zones":[…]}`: the names `time.zone` takes (besides `UTC+hh:mm`) |
| GET | `/api/status` | live values (bottom), manual setpoint, selected program; `channels`: both channels (name, type, ok, err, pv, sp, out, mode, manual); `run`: state, `type` (`seq`/`tl`), `seq` (phase `idx`, `stage`, `el`, `dur`), `waiting`, `progPause`, `actionRequired`, `pauseText`, `elapsed`, `remaining`, `edited`, `channels[]` (inProgram, idle, sp, target, moving, Timeline `lane`) |
| GET/POST | `/api/config` | the whole settings document (= `/duo/settings.json` without passwords) + `limits` (`rs485`, `localMax`, `monitor` of this board variant). POST changes only the keys given, e.g. `{"channels":[null,{"type":"modbus","modbus":{"addr":2}}]}`; `"modbus":{"loadDefaults":true}` loads the register map of the channel's controller type; errors come back as `{"ok":false,"err":"channels[1].type: one of …"}` |
| GET | `/api/log.csv` | last program run (or, without one, the live history) as CSV, both channels (+ `monitor_pv` while the monitor channel is on) |
| GET | `/api/history.csv` | the whole live history, streamed: time_s (before now), uptime_s, then per channel pv, min, max, sp (the monitor channel: pv, min, max); `?pv=3&sp=3` = channels (bit 0 bottom, bit 1 top, pv bit 2 the monitor) whose PV / SP columns are included (default all; rows without any value are left out) |
| GET | `/api/programs` | all 20 slots: `used`, `name`, `type`, `steps` (phases / steps), `total` (s), `uses` [bottom, top], `waits`; `sel` |
| GET | `/api/program?i=0` | `used`, `program`: the program JSON (spec 9.3.5) |
| POST | `/api/program` | `{"i":0,"program":{…}}` save (validated, setpoint limits checked) or `{"i":0,"delete":true}` |
| GET | `/api/run` | `run` (as in `/api/status`) + `program`: the running copy with its run edits |
| POST | `/api/run` | `{"cmd":"start","i":0,"program":{…}}` (start slot 0 with this unsaved content), `{"cmd":"extend","ch":0,"sec":30}`, `{"cmd":"hold","ch":1,"idx":4,"sec":45}`, `{"cmd":"ramp","ch":1,"idx":5,"sec":40}`, `{"cmd":"target","ch":0,"idx":-1,"t":170}` (`idx` = phase / step, -1 = current / kept), `{"cmd":"save"}`, `"next"`, `"continue"`, `"recover"` (resume the run interrupted by a reset), `"dismiss"` (dismiss it); the result shows in `run.msg` |
| POST | `/api/select` | `{"i":index}` select the program for the next run (Dashboard, touch) |
| POST | `/api/manual` | `{"sp":150}` and/or `{"on":true}`, optional `"ch":1` (top; default 0 = bottom) |
| POST | `/api/autotune` | local channels: `{"ch":1,"cmd":"start","sp":200}`, `"stop"`, `"accept"` (proposed kp/ki/kd → settings), `"discard"`; progress and result in `/api/status` → `channels[].autotune` |
| POST | `/api/ack` | `{"ch":0}` acknowledge a channel fault (runaway, sensor, over temperature, heating while off, frozen reading, converter too hot); refused while the cause is still there. The outputs stay off until switched on again |
| GET | `/api/ota` | firmware update: `board`, `version`, `slot`, `slotKb` (0 = no second slot, `why`), `canStart` / `why`, `probation`, `sd` (SD slot), `status` {`phase` idle / running / done / failed, `source`, `done`, `total`, `version`, `msg`}; `/api/live` → `ota` while one runs or the new firmware is on probation |
| POST | `/api/ota?size=<bytes>[&md5=<hex>]` | multipart upload of a `firmware.bin` (field `fw`, Wi-Fi only): written while it arrives (with `md5`: checked at the end), `{"ok":true,"version"}` or `{"err"}`; the device restarts 2 s later |
| GET / POST | `/api/ota/sd` | S3: `?start=1` starts the check of the `firmware.bin` in the SD card's root; then `{"busy":true}` until its result (`found`, `sizeKb`, `board`, `version`, `problem`, or `err`) / POST: install it (progress in `/api/live` → `ota`) |
| POST | `/api/beep` | `{}` one short test beep (Settings › Safety). `/api/live` → `safety`: `estop`, `contact` (open / closed, S3), `chipC`, `chipHot`, `muted`; `/api/config` → `limits.estop / sound / chipTemp` |
| POST | `/api/cmd` | `{"cmd":"start"|"pause"|"resume"|"stop"|"next"|"continue", "prog":index}` (`skip` = `next`) |
| POST | `/api/wifi` | `{"ssid","pass","apPass"}`, reconnects without restart |
| GET | `/api/wifiscan` | `?start=1` starts a network scan; then `{"busy":true}` until `{"networks":[…]}` (strongest first) |
| POST | `/api/brightness` | `{"v":128}` touch screen brightness while the web slider moves (not saved) |
| POST | `/api/restart` | restarts the device (refused while a program runs) |
| POST | `/api/screen/restart` | sets the display and the touch controller up again (reset + init), redraws; the device keeps running (result in the serial log) |
| GET | `/api/mb/read?addr=&n=&fn=&ch=` | raw registers (`ch`: channel whose controller address is used, default 0; also in the POST tools below) |
| POST | `/api/mb/write` | `{"addr","value"}` raw |
| POST | `/api/mb/findsp` | `{"from","to"}`: starts the scan in the background (at most 20 s) → `{"busy":true}` |
| GET | `/api/mb/findsp` | `{"busy":true}` while it runs, then `ok`, `err`, `reference`, `candidates`, `stoppedAt` (the time limit stopped it there: scan on from the next address); `{"none":true}`: none started |
| GET/POST | `/api/params` | PMA parameters (`?ch=` / `"ch"`). GET reads them in the background: `{"busy":true,"done","total"}` until all are read (ask again), then `supported`, `params`, `actions` |
| POST | `/api/action` | `{"i":index}` PMA actions (self-tuning, outputs off, …) |


## Over USB: the serial API mode

`api on` on the serial console switches it to one JSON object per line, for the page over USB (Web Serial). Requests
`{"id":7,"m":"GET","p":"/api/live"}` or `{"id":8,"m":"POST","p":"/api/manual","b":{...}}` (`m`: GET,
POST, PING) go through the same handlers as the HTTP API (above), with the same paths, query
parameters and JSON. Answers: `{"id":7,"code":200,"body":{...}}`; text (CSV) in pieces
`{"id":9,"code":200,"type":"text/csv","chunk":"..."}` … `{"id":9,"end":true}`. Also `{"t":"hello",
"api":1,"fw":…,"board":…,"variant":…,"host":…}` after `api on`, `{"t":"bye","why":…}` at the end, and
on the CYD (shared port) the log as `{"t":"log","m":"..."}`. `api off`, or no request for 15 s, returns
to the console. Output is queued and written as the port takes it (the loop never waits for the
port); a request line may be up to 16 kB (a whole program). Try it in a terminal: `api on`, then
`{"id":1,"p":"/api/live"}`. `help api` shows a short description on the device.

The [desktop app](Desktop-app.md) and the hosted page's USB link use this mode; the [serial console](Serial-console.md)
page lists the human commands.
