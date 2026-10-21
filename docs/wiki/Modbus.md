# Modbus controllers (beta)

> **Note:** Modbus channels are built in but have **not been tested with a real controller yet**. Local
> thermocouple + SSR channels are the tested path. Feedback from a real setup is welcome.

A Modbus channel leaves the PID control to an RS485 temperature controller; the ESP32 writes its setpoints,
reads PV, setpoint and output, and can switch its outputs off. Wiring: [ESP32-S3](Hardware-S3.md#rs485-modbus-channels-beta),
[CYD](Hardware-CYD.md) (wirings A and C). One RS485 bus serves both channels (one or two controllers with their
own addresses).

<picture><source media="(prefers-color-scheme: dark)" srcset="img/web/channels-modbus-dark.webp"><img src="img/web/channels-modbus-light.webp" alt="Channels page: a Modbus channel" width="900"></picture>

## Set up a channel

1. *Channels* › the channel's tab › *Type* = *Modbus controller (beta)*.
2. *Controller type*: **PMA KS 40-1 / 41-1 / 42-1** or **Generic Modbus RTU** (a new type loads its register
   defaults).
3. *Slave address*; on the RS485 bus: baud rate, parity, stop bits, response timeout — as set on the
   controller.
4. Check that the channel shows its process value (the reading at the top of the channel's settings).
5. *Setpoint ramps* (below) and the setpoint limits.

## PMA KS 4x-1

| Item | Value |
|---|---|
| Interface | RS485 2-wire (option on the controller, galvanically isolated); terminals 17 = DATA A, 15 = DATA B, 13 = RGND |
| Protocol | Modbus RTU, address 1…247; factory 9600 baud, even parity, address 1 (`ConF/othr`) |
| Registers used (1 decimal) | PV `In.1` 13294, effective setpoint `SP.EF` 13293, output `Ypid` 13295, status 5100, alarms 250, outputs off `C.Off` 1086, front lock `L-R` 1080, setpoint `SP` 11372, limits `SP.LO` / `SP.Hi` 11292 / 11293, gradient `r.SP` 11295 |

The controller must be the version with the Modbus option (terminals 12–17 fitted, `othr/bAud` and `Addr`
visible). Its built-in programmer has only 4 segments, which is why the ESP32 runs the program and writes `SP`.
Writing `SP` often is fine on the KS 4x-1: it stores to EEPROM only about every half hour.

Before a run the firmware reads `SP.LO` / `SP.Hi` and refuses to start if a target lies outside them.
The web page reads and writes the controller's PID and alarm parameters (*Controller parameters*) and starts
its actions (self-tuning, outputs off / on).

## Setpoint ramps

| Mode | |
|---|---|
| **Controller ramps (r.SP)** (default) | per segment the ESP32 writes the start value, the gradient and the target; the controller ramps smoothly. About 4 writes per phase. Ramps slower than 0.1 °/min or faster than the register holds fall back to stepping |
| **ESP32 writes setpoint steps** | a new setpoint when it has moved by *min. step* (0.5 °C) and *min. time* (5 s) has passed; works with any controller |

## Other controllers (generic)

The same transceiver works with any Modbus RTU controller, but its register addresses differ:

1. Get its communication manual (PV and SV addresses, scaling, serial format).
2. *Controller type* = *Generic Modbus RTU*; baud rate, parity and address as set on the controller.
3. Enter the PV and SV addresses and the divisor (1 or 10). *Tools › Find the setpoint address* helps once PV
   reads work; *Tools › Read registers* over 0…60 shows where the front panel values are (SV 148 appears as
   148 or 1480).
4. Use *ESP32 writes setpoint steps* with *min. step* ≥ 1 and *min. time* ≥ 30 s to spare its EEPROM.

## Bus tips

- Only a 3.3 V transceiver module (MAX3485 / SP3485).
- PMA's "DATA A / B" naming is reversed against EIA-485 in theory; in practice DATA A goes to the module's
  A(+). No answer: swap A and B (harmless).
- No 120 Ω terminators for fewer than 8 devices at ≤ 9600 baud; the module needs fail-safe bias resistors.
- Twisted pair, screened (e.g. LiYCY 2×2×0.25), screen grounded at one end.
