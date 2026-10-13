"""ESP32-S3 schematic (env s3): placement in grid units, y down."""
from sheet import Sheet, report


def build():
    S = Sheet("TempCtl-Duo-S3", "TempCtl Duo - ESP32-S3 wiring", "s3", "1.0.38",
              "Modules connected with cables; pins: src/board_s3.h",
              "TempCtl Duo - ESP32-S3 (env s3) wiring", "Modules connected with cables; pins as in src/board_s3.h")

    # ======== column 1: ESP32, display, notes
    S.frame(11, 10, 112, 88, "ESP32-S3-DevKitC-1 (v1.0, N16R8)")
    U1 = S.add("DevKitC", "U1", 42, 20)
    J1 = {3: "NC", 4: "RELAY", 5: "ESTOP", 6: "BUZZER", 7: "CTP_INT", 8: "CTP_SCL", 9: "CTP_SDA", 10: "CTP_RST",
          11: "SD_CS", 12: "LCD_LED", 13: "NC", 14: "NC", 15: "LCD_DC", 16: "LCD_CS", 17: "LCD_MOSI", 18: "LCD_SCK",
          19: "LCD_MISO", 20: "LCD_RST"}
    J3 = {2: "NC", 3: "NC", 4: "RS485_TX", 5: "RS485_RX", 6: "RS485_DE", 7: "TC_SCK", 8: "TC_SO", 9: "TC_CS_B",
          10: "TC_CS_T", 11: "NC", 12: "NC", 13: "NC", 14: "NC", 15: "NC", 16: "NC", 17: "SSR_T", 18: "SSR_B", 19: "NC",
          20: "NC"}
    for n, net in J1.items():
        S.stub(U1, f"J1-{n}", net, 8)
    for n, net in J3.items():
        S.stub(U1, f"J3-{n}", net, 8)
    for n in ("J1-1", "J1-2"):
        S.stub(U1, n, "+3V3", 0)
    S.stub(U1, "J1-21", "+5V", 0)
    for n in ("J1-22", "J3-1", "J3-21", "J3-22"):
        S.stub(U1, n, "GND", 0)
    U1.ref_pos, U1.val_pos = (40, 15, "right"), (40, 17, "right")
    S.text(13, 73, "J1 = left header, J3 = right header (top view, USB ports at the bottom).")
    S.text(13, 75, "5 V from USB (J1-21), 3V3 from the DevKit's regulator (J1-1 / J1-2).")
    S.text(13, 77, "Not connected: GPIO35-37 (octal PSRAM), 19/20 and 43/44 (both USB")
    S.text(13, 79, "ports), strapping pins 0, 3, 45, 46; GPIO48 = on-board status LED.")
    S.power("GND", 16, 84)
    S.power("PWR_FLAG", 16, 84)
    S.text(19, 84, "one ground for the DevKit and every module")

    S.frame(11, 92, 112, 150, "Display + touch + micro-SD (14-pin header)")
    DS1 = S.add("Display", "DS1", 26, 102)
    for n, net in {3: "LCD_CS", 4: "LCD_RST", 5: "LCD_DC", 6: "LCD_MOSI", 7: "LCD_SCK", 8: "LCD_LED", 9: "LCD_MISO",
                   10: "CTP_SCL", 11: "CTP_RST", 12: "CTP_SDA", 13: "CTP_INT", 14: "SD_CS"}.items():
        S.stub(DS1, str(n), net, 8)
    S.stub(DS1, "1", "+5V", 0)
    S.stub(DS1, "2", "GND", 0)
    DS1.ref_pos, DS1.val_pos = (38, 97, "left"), (38, 99, "left")
    S.text(13, 142, "VCC 5 V (backlight about 100 mA), logic 3.3 V (level shifters on the module).")
    S.text(13, 144, "Header pin numbers: check them against the print on the module.")
    S.text(13, 146, "The micro-SD slot (run logs) shares SCK / MOSI / MISO; SD_CS selects it.")

    S.frame(11, 154, 112, 222, "Notes")
    for i, line_ in enumerate([
            "TempCtl Duo, env s3; pins as in src/board_s3.h (firmware 1.0.38).",
            "The modules are connected with cables: this is the connection",
            "schematic of the whole device, not a PCB layout.",
            "",
            "Net labels with the same name are connected.",
            "+5V_SW = the relay-switched +5 V of both SSR inputs.",
            "",
            "Display SPI 40 MHz on GPIO 9-14: keep these wires short.",
            "SD card 10 MHz on the same bus, touch I2C 400 kHz,",
            "thermocouples bit-banged on their own bus (GPIO 38-41).",
            "",
            "RS485: only 3.3 V modules (a 5 V MAX485 drives 5 V into RX).",
            "",
            "A software controller is never the only protection: fit an",
            "independent over-temperature limiter and a contactor.",
    ]):
        S.text(13, 162 + i * 2.5, line_)

    # ======== column 2: RS485, thermocouples, relay
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
    U2.ref_pos, U2.val_pos = (136, 15, "left"), (136, 17, "left")
    J2 = S.add("PMA", "J2", 176, 22, value="PMA KS 4x-1")
    S.wire(U2.pin_xy("6"), J2.pin_xy("17"))
    S.wire(U2.pin_xy("7"), J2.pin_xy("15"))
    S.nc(J2, "13")
    J2.ref_pos, J2.val_pos = (176, 18, "left"), (176, 20, "left")
    S.text(119, 44, "One or two controllers on the same A / B pair (own addresses).")
    S.text(119, 46, "No answer: swap A / B. No 120 ohm terminators below 8 devices.")
    S.text(119, 48, "RGND not connected. Auto-direction module: DE / RE open.")

    S.frame(116, 56, 218, 124, "Thermocouples (local channels)")
    for i, (y0, ch) in enumerate(((70, "B"), (96, "T"))):
        U = S.add("MAX31855", f"U{3 + i}", 140, y0)
        S.stub(U, "4", "TC_SCK", 14)
        S.stub(U, "5", "TC_SO", 14)
        cx, cy = U.pin_xy("3")
        R = S.add("R_V", f"R{1 + i}", cx - 6, cy - 6, value="10k")
        S.wire((cx, cy), (cx - 6, cy))
        S.wire((cx - 6, cy), (cx - 14, cy))
        S.wire(R.pin_xy("2"), (cx - 6, cy))
        S.junction(cx - 6, cy)
        S.power("+3V3", *R.pin_xy("1"))
        S.label(f"TC_CS_{ch}", cx - 14, cy, "L")
        R.ref_pos, R.val_pos = (cx - 8, cy - 7, "right"), (cx - 8, cy - 5, "right")
        S.stub(U, "1", "+3V3", 0)
        S.stub(U, "2", "GND", 0)
        U.ref_pos, U.val_pos = (153, y0 - 3.5, "left"), (153, y0 - 1.5, "left")
        T = S.add("TC", f"TC{1 + i}", 172, y0 + 2, value="type K, " + ("bottom" if ch == "B" else "top"))
        S.wire(U.pin_xy("6"), T.pin_xy("1"))
        S.wire(U.pin_xy("7"), T.pin_xy("2"))
        T.ref_pos, T.val_pos = (172, y0 + 0.5, "left"), (172, y0 + 10.5, "left")
    S.text(119, 116, "Shared SCK / SO, one chip select per converter. R1 / R2 keep both")
    S.text(119, 118, "deselected while the ESP32 boots. MAX6675 modules fit the same way.")

    S.frame(116, 128, 218, 186, "SSR supply relay (switched +5 V of both SSR inputs)")
    K1 = S.add("Relay", "K1", 172, 142, value="DC SSR, active high")
    S.stub(K1, "3", "RELAY", 10)
    x, y = K1.pin_xy("4")
    S.wire((x, y), (x - 4, y))
    S.power("GND", x - 4, y)
    x, y = K1.pin_xy("1")
    S.wire((x, y), (x + 4, y))
    S.power("+5V", x + 4, y)
    S.stub(K1, "2", "+5V_SW", 12)
    K1.ref_pos, K1.val_pos = (172, 137.5, "left"), (172, 139.5, "left")
    for i, line_ in enumerate([
            "Active-high DC solid-state relay (input 3-32 V DC), driven",
            "directly by GPIO4: input current at most 20 mA at 3.3 V,",
            "output at least 0.5 A DC. GPIO4 is low (off) from the first",
            "line of setup(). On while a local channel heats, off at once",
            "on a fault or E-stop. It covers a stuck SSR input or driver;",
            "a shorted SSR output needs a contactor in the heater supply.",
    ]):
        S.text(119, 160 + i * 2.5, line_)

    # ======== column 3: SSR drivers, E-stop, buzzer
    S.frame(222, 10, 319, 104, "SSR drivers and SSRs (local channels)")
    for i, (yq, ch, name) in enumerate(((36, "B", "bottom"), (76, "T", "top"))):
        Q = S.add("Q_NPN", f"Q{1 + i}", 262, yq, value="BC337")
        Rb = S.add("R_H", f"R{3 + i}", 251, yq, value="1k")
        bx, by = Rb.pin_xy("2")
        S.wire((bx, by), Q.pin_xy("2"))
        Rp = S.add("R_V", f"R{5 + i}", bx, yq + 6, value="10k")
        S.wire(Rp.pin_xy("1"), (bx, by))
        S.junction(bx, by)
        S.power("GND", *Rp.pin_xy("2"))
        S.power("GND", *Q.pin_xy("3"))
        x1, y1 = Rb.pin_xy("1")
        S.wire((x1, y1), (x1 - 6, y1))
        S.label(f"SSR_{ch}", x1 - 6, y1, "L")
        K = S.add("SSR", f"K{2 + i}", 274, yq - 16, value="SSR DC-AC zero-cross")
        cx, cy = Q.pin_xy("1")
        mx, my = K.pin_xy("4")
        S.wire((cx, cy), (cx, my), (mx, my))
        px, py = K.pin_xy("3")
        S.wire((px, py), (px - 6, py))
        S.label("+5V_SW", px - 6, py, "L")
        S.nc(K, "1")
        S.nc(K, "2")
        K.ref_pos, K.val_pos = (274, yq - 20.5, "left"), (274, yq - 18.5, "left")
        Rb.ref_pos, Rb.val_pos = (251, yq - 1.5, "center"), (251, yq + 2.5, "center")
        Rp.ref_pos, Rp.val_pos = (bx - 2, yq + 5, "right"), (bx - 2, yq + 7, "right")
        Q.ref_pos, Q.val_pos = (267, yq - 1, "left"), (267, yq + 1, "left")
        S.text(274, yq - 4, f"load side: L - fuse - SSR 1/2 - {name} heater - N")
        S.text(274, yq - 2, "(mains, not drawn)")
    S.text(225, 96, "Q1 / Q2: BC337 or 2N2222 (TO-92 C-B-E). The 10k base pull-down")
    S.text(225, 98, "keeps the SSR off during reset and flashing.")
    S.text(225, 100, "SSR + inputs: the relay-switched +5 V (+5V_SW).")

    S.frame(222, 108, 270, 196, "E-stop input")
    SW1 = S.add("SW_NC", "SW1", 246, 120, value="E-stop, NC")
    x1, y1 = SW1.pin_xy("1")
    S.wire((x1, y1), (x1 - 8, y1))
    S.label("ESTOP", x1 - 8, y1, "L")
    x2, y2 = SW1.pin_xy("2")
    S.wire((x2, y2), (x2 + 4, y2))
    S.power("GND", x2 + 4, y2)
    SW1.ref_pos, SW1.val_pos = (246, 114, "center"), (246, 116, "center")
    for i, line_ in enumerate(["Normally closed contact to GND,", "internal pull-up. Open contact or",
                               "broken wire: SSRs + relay off, run", "stopped (setting safety.estop,",
                               "off by default)."]):
        S.text(225, 132 + i * 2.5, line_)

    S.frame(274, 108, 319, 196, "Alarm buzzer")
    S.wire((284, 120), (288, 120))
    S.label("BUZZER", 284, 120, "L")
    R11 = S.add("R_H", "R7", 291, 120, value="100")
    BZ1 = S.add("Buzzer", "BZ1", 304, 122, value="piezo, passive")
    S.wire(R11.pin_xy("2"), BZ1.pin_xy("1"))
    S.power("GND", *BZ1.pin_xy("2"))
    R11.ref_pos, R11.val_pos = (291, 118.5, "center"), (291, 122.5, "center")
    BZ1.ref_pos, BZ1.val_pos = (308, 121, "left"), (308, 123, "left")
    for i, line_ in enumerate(["2.7 kHz square wave (setting", "safety.sound). Passive piezo:", "directly via R7.", "",
                               "Magnetic buzzer / small speaker:", "NPN driver like Q1 (1k base)", "+ 1N4148 across it."]):
        S.text(277, 140 + i * 2.5, line_)
    return S


if __name__ == "__main__":
    report(build())
