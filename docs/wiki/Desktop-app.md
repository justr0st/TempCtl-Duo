# Desktop app and USB

The web page also works **without Wi-Fi, over the USB cable**: it then talks to the device through the serial
port instead of HTTP — the same screens and functions. This needs **Chrome or Edge** on a computer (the Web
Serial API), and the page must come from the desktop app or the release page (not from the device).

## The desktop app

Download it from the release page <https://justr0st.github.io/TempCtl-Duo/> (*Downloads*): one file, nothing
to install.

| System | File | First start |
|---|---|---|
| Windows (x64) | `tempctl-duo-<version>-windows-x64.exe` | not signed: SmartScreen asks once — *More info › Run anyway* |
| macOS (Apple silicon / Intel) | `…-macos-arm64.zip` / `…-macos-x64.zip` | unzip, then right-click › *Open* (Gatekeeper) |
| Linux (x64) | `…-linux-x64.tar.gz` | unpack, run `./tempctl-duo` |

The app serves the page on `http://localhost:8765` and opens it in Chrome or Edge as an app window. A second
start only opens another window; the app ends by itself 30 s after the last window was closed. It tells you
when a newer version is on the release page.

## Connect

<picture><source media="(prefers-color-scheme: dark)" srcset="img/web/connect-dark.webp"><img src="img/web/connect-light.webp" alt="Connect screen" width="900"></picture>

- **USB cable** › *Connect…* › choose the port: S3 the connector marked **"USB"**, CYD its only port. The page
  switches the device's serial console to its API mode and works as usual; *Connected over USB* shows at the
  bottom left with *Disconnect USB*.
- **Wi-Fi**: the app lists the devices it finds on the network (click one), or enter the address
  (`tempctl-duo.local` or the IP). That opens the device's own page.
- **Try the demo**: the [browser demo](Demo.md) with a simulated device.
- **Manual and wiring**: opens this wiki (Getting started, the wiring of both boards, Troubleshooting).
- **Downloads** (hosted page and app): the newest desktop app and the firmware for every board.

Close a serial monitor (PlatformIO, Arduino) first: only one program can use the port.

## Tabs

Like a terminal: one device per tab — "+" opens a new tab with the connect screen, × (or a middle click) closes
one. The icon shows the connection (USB / Wi-Fi), the dot whether the device answers. The tabs come back at the
next start (USB tabs connect to their port again).

## Device search

The app finds devices by mDNS (`_tempctl._tcp`, and `_http._tcp` for older firmware) on every network of the
computer and checks each answer. Windows asks once whether the app may use the network (*Allow access*, private
networks) — without it the list stays empty; the address field still works.

## When the connection drops

The connection comes back by itself when the page opens again (the last port is remembered until *Disconnect
USB*), when the cable is plugged in again or the device restarted. A lost connection never touches a running
program; 15 s after the page is gone the device's serial port returns to the normal console.

## The hosted page

The release page itself (Chrome / Edge) has the same connect screen, without device search and tabs: USB, or
Wi-Fi = open the device's own `http://` page. Below it: the **Downloads** of the newest release.

> **Note:** over USB the firmware upload is not available (it needs Wi-Fi); CSV downloads work.
