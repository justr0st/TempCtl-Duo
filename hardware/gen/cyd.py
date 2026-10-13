"""CYD schematics (ESP32-2432S028R), one sheet per firmware env: placement in grid units, y down."""
from sheet import Sheet, report

VARIANTS = ("a", "b", "c")
ENV = {"a": "cyd-1xlocal-1x485", "b": "cyd-2xlocal", "c": "cyd-2xlocal-2x485"}
WHAT = {"a": "RS485 + one local channel", "b": "two local channels", "c": "RS485 + two local channels"}
REV = "1.0.39"


def module(S, v):
    """column 1: the CYD with its connectors; returns the bottom of the frame"""
    led = v == "c"
    S.frame(11, 10, 112, 92 if led else 84, "CYD ESP32-2432S028R" + (" (RGB LED removed)" if led else ""))
    U1 = S.add("CYD_LED" if led else "CYD", "U1", 24, 26)
    U1.ref_pos, U1.val_pos = (13, 19, "left"), (13, 17, "left")
    nets = {"P3-1": "NC", "CN1-3": "NC", "P1-2": "NC", "P1-3": "NC", "P4-1": "SPK_P", "P4-2": "SPK_N"}
    if v in ("a", "c"):
        nets.update({"P3-2": "RS485_TX", "P3-3": "RS485_RX", "CN1-2": "RS485_DE"})
    if v == "a":
        nets.update({"SD-2": "TC_CS", "SD-3": "TC_SCK", "SD-5": "SSR", "SD-7": "TC_SO"})
    if v == "b":
        nets.update({"P3-2": "SSR_T", "P3-3": "NC", "CN1-2": "TC_CS_T",
                     "SD-2": "TC_CS_B", "SD-3": "TC_SCK", "SD-5": "SSR_B", "SD-7": "TC_SO"})
    if v == "c":
        nets.update({"SD-2": "TC_CS_B", "SD-3": "TC_SCK", "SD-5": "SSR_B", "SD-7": "TC_SO",
                     "LED-R": "TC_CS_T", "LED-G": "SSR_T", "LED-B": "LED_DIN"})
    for pin, net in nets.items():
        S.stub(U1, pin, net, 10)
    for pin in ("CN1-1", "SD-4"):
        S.stub(U1, pin, "+3V3", 0)
    S.stub(U1, "P1-1", "+5V", 0)
    for pin in ("P1-4", "P3-4", "CN1-4", "SD-6"):
        S.stub(U1, pin, "GND", 0)
    # connector names next to the label column
    y = lambda pin: U1.pin_xy(pin)[1]
    groups = [("P3-1", "P3: JST 1.25 mm, 4 pin"), ("CN1-2", "CN1: JST 1.25 mm, 4 pin"),
              ("SD-2", "micro-SD slot: via a micro-SD"), ("SD-3", "breakout adapter, no card"),
              ("P4-1", "P4: speaker (IO26 drives the"), ("P4-2", "on-board SC8002B amplifier)"),
              ("P1-2", "P1: USB serial, not used")]
    if led:
        groups += [("LED-R", "RGB LED footprint: the pads"), ("LED-G", "to the ESP32 pins (LED")]
        groups += [("LED-B", "removed)")]
    for pin, s in groups:
        S.text(80, y(pin) + 0.5, s)
    return 92 if led else 84


def notes(S, v, y0):
    lines = [f"TempCtl Duo, env {ENV[v]}; pins as in src/board_cyd.h",
             f"(firmware {REV}). Modules connected with cables: a connection",
             "schematic of the whole device, not a PCB layout.",
             "Net labels with the same name are connected.",
             "",
             "Power: USB on the CYD. 3V3 for the modules from CN1-1 or the",
             "SD VDD pin (the board's regulator), 5 V for the SSR inputs from",
             "P1-1 (VIN). One ground for the CYD and every module.",
             "",
             "The SD-slot pins: plug a micro-SD breakout (sniffer) adapter into",
             "the slot; no SD card. IO5 has the board's SD pull-up (converter",
             "deselected at boot); IO18 has none (SSR off with the 10k",
             "pull-down)."]
    if v == "b":
        lines += ["", "CS top on IO27: the board's pull-up R18 keeps it deselected."]
    if v == "c":
        lines += ["", "RGB LED removed: wire to the pads that go to the ESP32 pins,",
                  "not the ones to the 1k resistors to 3V3 (check with an",
                  "ohmmeter, or remove those resistors too). IO4 floats at boot:",
                  "R1 keeps the top converter deselected."]
    lines += ["", "No E-stop input, no SSR supply relay and no chip temperature on",
              "the CYD (no free pins). IO35 is input only.", "",
              "A software controller is never the only protection: fit an",
              "independent over-temperature limiter and a contactor."]
    for i, s in enumerate(lines):
        S.text(13, y0 + 12 + i * 2.5, s)
    yy = y0 + 12 + len(lines) * 2.5 + 3
    S.frame(11, y0 + 4, 112, round(yy) + 8, "Notes")
    S.power("GND", 16, round(yy))
    S.power("PWR_FLAG", 16, round(yy))
    S.text(19, round(yy), "one ground for the CYD and every module")


def rs485(S):
    S.frame(116, 10, 218, 52, "RS485 (Modbus RTU, beta)")
    U2 = S.add("RS485", "U2", 136, 20)
    S.stub(U2, "1", "RS485_TX", 8)
    S.stub(U2, "2", "RS485_RX", 8)
    dx, dy = U2.pin_xy("3")
    rx, ry = U2.pin_xy("4")
    S.wire((dx, dy), (dx - 4, dy))
    S.wire((dx - 4, dy), (dx - 8, dy))
    S.wire((rx, ry), (rx - 4, ry), (dx - 4, dy))
    S.junction(dx - 4, dy)
    S.label("RS485_DE", dx - 8, dy, "L")
    S.stub(U2, "5", "+3V3", 0)
    S.stub(U2, "8", "GND", 0)
    U2.ref_pos, U2.val_pos = (134, 15, "right"), (134, 17, "right")
    J2 = S.add("PMA", "J2", 176, 22, value="PMA KS 4x-1")
    S.wire(U2.pin_xy("6"), J2.pin_xy("17"))
    S.wire(U2.pin_xy("7"), J2.pin_xy("15"))
    S.nc(J2, "13")
    J2.ref_pos, J2.val_pos = (176, 18, "left"), (176, 20, "left")
    S.text(119, 44, "One or two controllers on the same A / B pair (own addresses).")
    S.text(119, 46, "No answer: swap A / B. No 120 ohm terminators below 8 devices.")
    S.text(119, 48, "RGND not connected. Auto-direction module: DE / RE open.")


def thermocouples(S, top, chans, u0):
    """chans: [(cs net, pull-up resistor ref or None, name)]"""
    n = len(chans)
    y_last = top + 14 + 26 * (n - 1)
    S.frame(116, top, 218, y_last + 28, "Thermocouples (local channel" + ("s)" if n > 1 else ")"))
    for i, (cs, rref, name) in enumerate(chans):
        y0 = top + 14 + 26 * i
        U = S.add("MAX31855", f"U{u0 + i}", 140, y0)
        S.stub(U, "4", "TC_SCK", 14)
        S.stub(U, "5", "TC_SO", 14)
        cx, cy = U.pin_xy("3")
        if rref:
            R = S.add("R_V", rref, cx - 6, cy - 6, value="10k")
            S.wire((cx, cy), (cx - 6, cy))
            S.wire((cx - 6, cy), (cx - 14, cy))
            S.wire(R.pin_xy("2"), (cx - 6, cy))
            S.junction(cx - 6, cy)
            S.power("+3V3", *R.pin_xy("1"))
            S.label(cs, cx - 14, cy, "L")
            R.ref_pos, R.val_pos = (cx - 8, cy - 7, "right"), (cx - 8, cy - 5, "right")
        else:
            S.stub(U, "3", cs, 14)
        S.stub(U, "1", "+3V3", 0)
        S.stub(U, "2", "GND", 0)
        U.ref_pos, U.val_pos = (153, y0 - 3.5, "left"), (153, y0 - 1.5, "left")
        T = S.add("TC", f"TC{1 + i}", 172, y0 + 2, value="type K, " + name)
        S.wire(U.pin_xy("6"), T.pin_xy("1"))
        S.wire(U.pin_xy("7"), T.pin_xy("2"))
        T.ref_pos, T.val_pos = (172, y0 + 0.5, "left"), (172, y0 + 10.5, "left")
    yt = y_last + 20
    if n > 1:
        S.text(119, yt, "Shared SCK / SO, one chip select per converter.")
    else:
        S.text(119, yt, "The local channel: bottom or top (setting), the other one Modbus.")
    S.text(119, yt + 2, "MAX6675 modules fit the same way.")


def ssr_drivers(S, chans, r0):
    """chans: [(input net, name)]; returns the bottom of the frame"""
    bottom = 10 + 40 * len(chans) + 14
    S.frame(222, 10, 319, bottom, "SSR driver" + ("s" if len(chans) > 1 else "") + " and SSR"
            + ("s" if len(chans) > 1 else "") + " (local channel" + ("s)" if len(chans) > 1 else ")"))
    for i, (net, name) in enumerate(chans):
        yq = 36 + 40 * i
        Q = S.add("Q_NPN", f"Q{1 + i}", 262, yq, value="BC337")
        Rb = S.add("R_H", f"R{r0 + i}", 251, yq, value="1k")
        bx, by = Rb.pin_xy("2")
        S.wire((bx, by), Q.pin_xy("2"))
        Rp = S.add("R_V", f"R{r0 + len(chans) + i}", bx, yq + 6, value="10k")
        S.wire(Rp.pin_xy("1"), (bx, by))
        S.junction(bx, by)
        S.power("GND", *Rp.pin_xy("2"))
        S.power("GND", *Q.pin_xy("3"))
        x1, y1 = Rb.pin_xy("1")
        S.wire((x1, y1), (x1 - 6, y1))
        S.label(net, x1 - 6, y1, "L")
        K = S.add("SSR", f"K{1 + i}", 274, yq - 16, value="SSR DC-AC zero-cross")
        cx, cy = Q.pin_xy("1")
        mx, my = K.pin_xy("4")
        S.wire((cx, cy), (cx, my), (mx, my))
        px, py = K.pin_xy("3")
        S.wire((px, py), (px - 4, py))
        S.power("+5V", px - 4, py)
        S.nc(K, "1")
        S.nc(K, "2")
        K.ref_pos, K.val_pos = (274, yq - 20.5, "left"), (274, yq - 18.5, "left")
        Rb.ref_pos, Rb.val_pos = (251, yq - 1.5, "center"), (251, yq + 2.5, "center")
        Rp.ref_pos, Rp.val_pos = (bx - 2, yq + 5, "right"), (bx - 2, yq + 7, "right")
        Q.ref_pos, Q.val_pos = (267, yq - 1, "left"), (267, yq + 1, "left")
        S.text(274, yq - 4, f"load side: L - fuse - SSR 1/2 - {name} heater - N")
        S.text(274, yq - 2, "(mains, not drawn)")
    S.text(225, bottom - 8, "Q: BC337 or 2N2222 (TO-92 C-B-E). The 10k base pull-down")
    S.text(225, bottom - 6, "keeps the SSR off during reset and flashing.")
    S.text(225, bottom - 4, "SSR + input: 5 V from P1-1 (VIN).")
    return bottom


def speaker(S, top):
    S.frame(222, top, 319, top + 42, "Alarm sound (speaker on P4)")
    LS = S.add("Speaker", "LS1", 252, top + 14, value="8 ohm, 0.5-2 W")
    for pin, net in (("1", "SPK_P"), ("2", "SPK_N")):
        x, y = LS.pin_xy(pin)
        S.wire((x, y), (x - 8, y))
        S.label(net, x - 8, y, "L")
    LS.ref_pos, LS.val_pos = (258, top + 13, "left"), (258, top + 15, "left")
    for i, s in enumerate(["IO26 drives the CYD's SC8002B amplifier, its bridged",
                           "output is P4: SPK- is not ground, connect the",
                           "speaker only between SPK+ and SPK-. A small 8 ohm",
                           "speaker or a passive piezo. 2.7 kHz beeps (setting",
                           "safety.sound; touch: Settings > System > Alarm sound)."]):
        S.text(225, top + 26 + i * 2.5, s)
    return top + 42


def status_led(S, top):
    S.frame(222, top, 319, 196, "Status LED (external WS2812 on IO17)")
    D1 = S.add("WS2812", "D1", 250, top + 10)
    x, y = D1.pin_xy("4")
    S.wire((x, y), (x - 8, y))
    S.label("LED_DIN", x - 8, y, "L")
    S.nc(D1, "2")
    S.stub(D1, "1", "+3V3", 0)
    S.stub(D1, "3", "GND", 0)
    D1.ref_pos, D1.val_pos = (268, top + 10, "left"), (268, top + 12, "left")
    for i, s in enumerate(["Powered from 3V3: the 3.3 V data level is then in spec.",
                           "Colours as on the S3 (green idle, blue running, ...)."]):
        S.text(225, top + 26 + i * 2.5, s)


def build(v):
    S = Sheet(f"TempCtl-Duo-CYD-{v.upper()}", f"TempCtl Duo - CYD {v.upper()}: {WHAT[v]}", ENV[v], REV,
              "Modules connected with cables; pins: src/board_cyd.h",
              f"TempCtl Duo - CYD (env {ENV[v]}) wiring", "Modules connected with cables; pins as in src/board_cyd.h")
    y = module(S, v)
    notes(S, v, y)
    if v == "a":
        rs485(S)
        thermocouples(S, 56, [("TC_CS", None, "local")], 3)
        b = ssr_drivers(S, [("SSR", "local")], 1)
    elif v == "b":
        thermocouples(S, 10, [("TC_CS_B", None, "bottom"), ("TC_CS_T", None, "top")], 2)
        b = ssr_drivers(S, [("SSR_B", "bottom"), ("SSR_T", "top")], 1)
    else:
        rs485(S)
        thermocouples(S, 56, [("TC_CS_B", None, "bottom"), ("TC_CS_T", "R1", "top")], 3)
        b = ssr_drivers(S, [("SSR_B", "bottom"), ("SSR_T", "top")], 2)
    b = speaker(S, b + 4)
    if v == "c":
        status_led(S, b + 4)
    return S


if __name__ == "__main__":
    import sys
    for v in sys.argv[1:] or VARIANTS:
        print(f"== {v}")
        report(build(v))
