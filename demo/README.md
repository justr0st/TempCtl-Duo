# Browser demo

The web page with a simulated device, on the release page under `/demo/` (user side: wiki page
[Browser demo](../docs/wiki/Demo.md)). The device is the firmware itself: its control code is compiled to
WebAssembly together with a simulated board, and the normal web page talks to it as it does over USB.

```
web/index.html (data-demo)  --request lines-->  demo.js  --demo_request()-->  webApiRequest()  (src/web.cpp)
       serLine()  <--------answer lines-------  demo.js  <---apiOut()-------  the same handlers as HTTP / USB
                                                demo.js  --demo_run(ms)--->   sim.cpp: clock, tasks, pins, heat model
```

## Files

| File | |
|---|---|
| `sim.cpp` | the board: simulated clock; the tasks stepped in turn every 10 ms (esp_timers, `localCtrlTick()` every 50 ms, `controlTaskStep()`, the loop's `syslogLoop()` / `webLoop()`); pins (SSRs, SSR supply relay, MAX31855 / MAX6675 frames bit by bit, E-stop, buzzer); the flash file system in memory; the heat model; the exports the page calls |
| `stubs.cpp` | what the demo does without: Wi-Fi, display, SD card, firmware update, status LED, serial console |
| `shim/` | stand-ins for the Arduino core, FreeRTOS, ESP-IDF, LittleFS, WebServer (one thread: locks always succeed, a wait for the control task runs it) |
| `board_demo.h` | the board (`-D BOARD_DEMO`, `src/config.h`): two local channels, relay, E-stop, buzzer; no RS485, display, SD |
| `demo.js` | in the page: loads the module (a small WASI layer: log output, clocks), steps it (speed 1–10×), passes the page's request lines, keeps the file system and the heat state in local storage, the demo panel |
| `build.sh` | builds `out/tempctl-demo.wasm` |

Compiled firmware sources: `app_state web local_ctrl settings settings_store run_engine duo_program
program_store channel controller pid autotune runaway safety tc_sensor crumbs syslog modbus_rtu`. The firmware
needs three hooks for this, with no effect on the boards: `controlTaskBegin()` / `controlTaskStep()` (the control
task's loop body), `localCtrlTick()` (the local task's), and `BOARD_DEMO` in `src/config.h` and for
`web_assets.h` in `src/web.cpp`.

## Heat model (`sim.cpp`)

Four bodies: bottom IR emitter (260 J/K, 1100 W), board (320 J/K), top IR emitter (60 J/K, 450 W), GPU zone
(60 J/K: the chip and the board around it). Radiation between emitter and target (emissivity 0.9, effective
areas), convection, conduction from the GPU zone into the board, ambient 23 °C. The heaters get power only
while the SSR supply relay pin is on. Thermocouples: bottom on the board, top on the GPU, 1.5 s lag, a little
noise; the converters' cold junction warms slowly with the heaters. Full power from cold: board 150 °C after
about 3.3 min (up to ~0.85 °C/s); top alone with the board at 150 °C: GPU up to ~270 °C.

On the first start the PID values of an autotune of this model are set (bottom Kp 8.7 Ki 0.040 Kd 134, top
Kp 5.7 Ki 0.053 Kd 44); the firmware's own defaults are placeholders until an autotune.

## Build

```
WASI_SDK=<wasi-sdk 24 folder> demo/build.sh                     (the release workflow does this)
WASI_SYSROOT=<wasi-sysroot-24.0> WASI_RT=<libclang_rt.builtins-wasm32.a> demo/build.sh   (system clang 18 + lld)
AJ=<ArduinoJson>/src ...                                         (default: .pio/libdeps/s3/ArduinoJson/src)
```

wasi-sdk: <https://github.com/WebAssembly/wasi-sdk/releases> (`wasi-sdk-24.0-x86_64-linux.tar.gz`, or the sysroot
and the builtins archive alone for a system clang). `python3 tools/build_site.py <dir> --web` then puts the
demo under `<dir>/demo/` (the page with `data-demo`, `demo.js`, the module); serve the folder
(`python3 -m http.server --directory <dir>`) and open `/demo/`.

Exports of the module: `demo_boot(wallMs, restarted)`, `demo_run(ms)` (1 = the firmware restarts: the page saves
and starts the module again), `demo_request(line)` / `demo_request_len()`, `demo_state()` (JSON for the panel),
`demo_set(what, ch, v)` (1 E-stop pressed, 2 thermocouple open, 3 speed), `demo_fs_load` / `demo_fs_dump` /
`demo_fs_size` / `demo_fs_gen`, `demo_heat()` (7 floats), `demo_coast(sec)`, `demo_alloc` / `demo_free`,
`demo_wall_ms()`.
