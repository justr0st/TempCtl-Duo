# Troubleshooting

Start with the logs: the web *Log* page (system log, colour-coded, searchable), the touch *Event log*, or the
serial console (`log`, `info`, `status`). The S3's UART port prints the technical log.

## Wi-Fi

| Problem | What to do |
|---|---|
| The device does not join the network | it opens its access point `TempCtl-Duo-XXXX` (password `tempctl123` unless changed) after 20 s; join it and check *Settings › Wi-Fi*. Over serial: `wifi status`, `wifi scan`, `wifi connect` reports *wrong password / network not found / timeout* |
| `tempctl-duo.local` does not open | some networks or phones don't resolve `.local`: use the IP address (touch *Menu › System* info, serial `info`), or the desktop app's device search |

## Readings

| Problem | What to do |
|---|---|
| *no reading* / sensor fault | open thermocouple, loose wire, converter not powered or wrong CS pin; MAX6675 selected for a MAX31855 or the other way round |
| The reading falls while heating | T+ and T− swapped |
| Noisy reading, single spikes | screened thermocouple wire with the screen to GND at one end; route it away from mains wires; switch on the median filter (5 or 7) |
| The reading is a few degrees off | two-point calibration on the web *Channels* page |
| *Converter too hot* | the MAX31855 sits too close to the heater: move it away (it measures its own temperature for the cold junction) |

## Heating

| Problem | What to do |
|---|---|
| *Heating failed* (runaway) | the thermocouple is not on the heater it belongs to, the SSR or heater is broken, the SSR supply relay is off (S3: setting *SSR supply relay* on with no relay fitted?) — or the heater is simply slow: raise the time (*Protection › Heating failed … for*) |
| Overshoot or slow settling | run the PID autotune at the working temperature ([Channels](Channels.md)) |
| A heater does not switch on | E-stop open (S3, the sidebar and the touch screen say so), a fault waiting for *Acknowledge*, the converter too hot, or the program does not use that channel |
| *Heating while off* | a stuck SSR (shorted output) or a controller that ignores "outputs off": switch the heater supply off and check the SSR |

## Display and touch

The **automatic screen check** (*Display › Screen check*, on by default) reads the display's state back every
second and restarts the display or touch controller when it hangs, without restarting the device; each
restart is an event ("Screen restarted automatically: …"). `screen check` over serial shows what it sees,
`screen restart` restarts both by hand (also *Settings › Device › Restart screen* on the web page).

| Problem | What to do |
|---|---|
| White or garbled screen after interference | the screen check restarts it within seconds; otherwise `screen restart` |
| Wrong pixels at 80 MHz | back to 40 MHz: web *Settings › Display*, or serial `set display.spiMhz=40` |
| Touches land in the wrong place | S3: `TOUCH_SWAP_XY` / `TOUCH_INVERT_X / Y` in `src/board_s3.h`; CYD: `touch cal` |
| A held button "lets go" (S3) | short touch gaps of the FT6336 are bridged (up to ~60 ms); `screen check` prints the statistics |
| Colours inverted | `LCD_INVERT 1` in the board header |

### Display speed

*Display SPI clock* (20 / 40 / 80 MHz, default 40) sets how fast the picture goes to the display. 80 MHz
needs short, clean wires (S3: GPIO 10–13; CYD: 12–15); the setting takes effect at once.

The **frame rate meter** (*Menu › Display › Frame rate meter*, serial `perf on`, not saved) shows a box at the
bottom right and prints a line every second:

```
[perf] 24 FPS  LVGL CPU 61 %, frame 31 ms (draw 19, wait 12), SPI 40 MHz busy 52 %, 410 kB/s
```

Much "wait": the SPI is the limit (a higher clock helps). Much "draw": the CPU is. On the S3 the dashboard's
chart is drawn once per change into a picture in PSRAM (about 10–15 ms, once a second) and only copied to the
screen; busy screens reach 13–15 FPS.

## Firmware update

| Problem | What to do |
|---|---|
| *Not while a program runs or an output is on* | stop the run, switch manual mode off |
| *The file is for another board / env* | download the image of your env ([Updates](Updates-and-backups.md)) |
| S3 online update: *name not found* / *connection* errors | the Wi-Fi has no internet access, or DNS fails; use the web page's *Check for updates* from a computer instead |
| The new firmware did not start | it rolls back by itself to the previous one; the event log says "Firmware update to … failed" |

## Modbus (beta)

No answer from a controller: check address, baud rate and parity against the controller (`ConF/othr` on a PMA),
swap A and B (harmless), remove 120 Ω terminators on a short line, check the module is a 3.3 V one. See
[Modbus controllers](Modbus.md).

## Restarts

After a restart the event log names the reason (power on, software restart, watchdog, crash, brown-out). A
crash prints a backtrace on the UART log. A run that was interrupted can be resumed by hand
([Programs](Programs.md#run-interrupted-by-a-reset)). A **brown-out** usually means a weak USB supply or the
display's backlight on the same thin cable: use a good 5 V supply.
