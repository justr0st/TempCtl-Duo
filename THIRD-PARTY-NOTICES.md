# Third-party notices

TempCtl Duo is licensed under the MIT License (`LICENSE`): the firmware, the web page, the tools, the
documentation and the schematics in this repository, except the parts below, which keep their own licences.
The firmware images, the desktop app and the hosted page contain or are built with them.

| Component | Where | Licence | Copyright |
|---|---|---|---|
| ESP32-S3 SIMD fills and copies, from Espressif's [esp-bsp](https://github.com/espressif/esp-bsp) (`esp_lvgl_port`) | `src/simd/*.S`; `src/simd/lv_blend_esp32s3.c` and `include/lv_blend_esp32s3.h` follow its `esp_lvgl_port_lv_blend.h` | Apache License 2.0 — full text in `src/simd/LICENSE` | © 2024 Espressif Systems (Shanghai) CO LTD |
| Barlow and Barlow Semi Condensed fonts | `web/fonts/`, `tools/fonts/`, converted in `src/fonts/` | SIL Open Font License 1.1 — full text in `web/fonts/OFL.txt` | © 2017 The Barlow Project Authors (https://github.com/jpt/barlow) |
| [LVGL](https://lvgl.io) 9.6 (PlatformIO library, compiled into the firmware), with its Montserrat symbol fonts | — | MIT License (below); Montserrat: SIL Open Font License 1.1 | © 2025 LVGL Kft |
| [ArduinoJson](https://arduinojson.org) 7 (PlatformIO library) | — | MIT License (below) | © 2014-2026 Benoit Blanchon |
| [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen) (PlatformIO library, CYD builds) | — | MIT License (below) | © 2015 Paul Stoffregen |
| [Arduino core for the ESP32](https://github.com/espressif/arduino-esp32) 2.0.17 | the PlatformIO platform `espressif32@6.9.0` | GNU Lesser General Public License 2.1 (core), Apache License 2.0 (most libraries) | © Espressif Systems and the Arduino contributors |
| [ESP-IDF](https://github.com/espressif/esp-idf) (in the Arduino core: FreeRTOS, lwIP, Mbed TLS, the Wi-Fi and Bluetooth stacks, the bootloader) | — | Apache License 2.0, MIT, BSD and others; see ESP-IDF's notices | © Espressif Systems and the respective authors |
| [wasi-libc / LLVM libc++](https://github.com/WebAssembly/wasi-sdk) (the browser demo's WebAssembly) | `demo/` (build) | Apache License 2.0 with LLVM exception, MIT and others | © the respective authors |
| Go standard library (the desktop app) | `tools/launcher/` (build) | BSD 3-Clause | © The Go Authors |

**Arduino core (LGPL-2.1):** the firmware images are statically linked with it. The complete source of TempCtl
Duo is in this repository and the core's source is public, so the firmware can be rebuilt with a changed core
(`platformio.ini` names the platform version). The licence text: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html

## MIT License (LVGL, ArduinoJson, XPT2046_Touchscreen)

Each of these components is distributed under the following terms, with its own copyright line above:

> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
> documentation files (the "Software"), to deal in the Software without restriction, including without
> limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
> Software, and to permit persons to whom the Software is furnished to do so, subject to the following
> conditions:
>
> The above copyright notice and this permission notice shall be included in all copies or substantial portions
> of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
> THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
> CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
> DEALINGS IN THE SOFTWARE.

(XPT2046_Touchscreen's text reads "The above copyright notice, development funding notice, and this permission
notice shall be included …" in the second paragraph; the rest is the same.)
