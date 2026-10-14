"""Schematic model (grid units, 1 grid = 1.27 mm): symbol definitions, placed parts, wires, labels.
The same model is written as a KiCad 7 schematic (kicad.py) and drawn as SVG (render.py)."""
import uuid

G = 1.27  # mm per grid unit
NS = uuid.UUID("6f1e2c64-0000-4000-8000-54656d70434c")


def uid(*parts):
    return str(uuid.uuid5(NS, "/".join(str(p) for p in parts)))


class Pin:
    def __init__(self, num, name, typ, x, y, angle, length=2, name_hidden=False):
        self.num, self.name, self.typ = num, name, typ
        self.x, self.y, self.angle, self.length = x, y, angle, length  # lib grid coords, y up
        self.name_hidden = name_hidden


class SymDef:
    """lib: 'TempCtlDuo' (all symbols, also the power ones). graphics in lib mm (y up):
    ('rect', x1, y1, x2, y2, fill) fill: 'background' | 'none' | 'outline'
    ('poly', [(x, y), ...], width, fill)   ('circle', cx, cy, r, width, fill)"""

    def __init__(self, name, lib="TempCtlDuo", prefix="U", value=None, power=False, pins=(), graphics=(),
                 numbers_hidden=False, names_hidden=False, name_offset=0.508, ref_at=(0, 0, 0), val_at=(0, 0, 0),
                 desc=""):
        self.name, self.lib, self.prefix, self.power = name, lib, prefix, power
        self.value = value or name
        self.pins, self.graphics = list(pins), list(graphics)
        self.numbers_hidden, self.names_hidden, self.name_offset = numbers_hidden, names_hidden, name_offset
        self.ref_at, self.val_at = ref_at, val_at  # lib mm (x, y, angle)
        self.desc = desc
        self.box = None  # (w_mm, h_mm) for box symbols (label placement)

    @property
    def lib_id(self):
        return f"{self.lib}:{self.name}"

    def pin(self, num):
        for p in self.pins:
            if p.num == num:
                return p
        raise KeyError(f"{self.name}: no pin {num}")


def box(name, w, left=(), right=(), top=(), bottom=(), prefix="U", value=None, numbers_hidden=True, desc="",
        plen=2):
    """Module / connector box. w in grid units (even). left / right: [(num, name, type) or None (gap)], top /
    bottom: [(num, name, type, x)] with x in grid units from the left edge. Body: lib (0,0) .. (w, -h)."""
    rows = max(len(left), len(right))
    h = (rows + 1) * 2
    pins = []
    for i, p in enumerate(left):
        if p:
            pins.append(Pin(p[0], p[1], p[2], -plen, -(i + 1) * 2, 0, plen))
    for i, p in enumerate(right):
        if p:
            pins.append(Pin(p[0], p[1], p[2], w + plen, -(i + 1) * 2, 180, plen))
    for p in top:
        pins.append(Pin(p[0], p[1], p[2], p[3], plen, 270, plen))
    for p in bottom:
        pins.append(Pin(p[0], p[1], p[2], p[3], -h - plen, 90, plen))
    s = SymDef(name, prefix=prefix, value=value, pins=pins,
               graphics=[("rect", 0, 0, w * G, -h * G, "background")], numbers_hidden=numbers_hidden,
               ref_at=(0, 3.81 if top else 1.27, 0), val_at=(0, 1.27 if not top else 1.27, 0), desc=desc)
    s.box = (w, h)
    return s


# ---------------------------------------------------------------- library

def lib():
    L = {}
    # resistors (vertical / horizontal: everything is placed unrotated)
    L["R_V"] = SymDef("R_V", prefix="R", value="R", numbers_hidden=True, names_hidden=True, name_offset=0,
                      pins=[Pin("1", "~", "passive", 0, 3, 270, 1), Pin("2", "~", "passive", 0, -3, 90, 1)],
                      graphics=[("rect", -1.016, -2.54, 1.016, 2.54, "none")], desc="Resistor, vertical")
    L["R_H"] = SymDef("R_H", prefix="R", value="R", numbers_hidden=True, names_hidden=True, name_offset=0,
                      pins=[Pin("1", "~", "passive", -3, 0, 0, 1), Pin("2", "~", "passive", 3, 0, 180, 1)],
                      graphics=[("rect", -2.54, -1.016, 2.54, 1.016, "none")], desc="Resistor, horizontal")
    # NPN, pins B C E = 2 1 3 (BC337 / 2N2222 in TO-92: C B E)
    L["Q_NPN"] = SymDef("Q_NPN", prefix="Q", value="BC337", numbers_hidden=True, names_hidden=True, name_offset=0,
                        pins=[Pin("2", "B", "passive", -2, 0, 0, 2.5),
                              Pin("1", "C", "passive", 2, 4, 270, 2), Pin("3", "E", "passive", 2, -4, 90, 2)],
                        graphics=[("circle", 1.27, 0, 2.8, 0.254, "none"),
                                  ("poly", [(0.635, 1.905), (0.635, -1.905)], 0.508, "none"),
                                  ("poly", [(0.635, 0.635), (2.54, 2.54)], 0.254, "none"),
                                  ("poly", [(0.635, -0.635), (2.54, -2.54)], 0.254, "none"),
                                  ("poly", [(1.27, -1.778), (1.778, -1.27), (2.286, -2.286), (1.27, -1.778)], 0.254, "outline")],
                        ref_at=(5.08, 1.27, 0), val_at=(5.08, -1.27, 0), desc="NPN transistor, B C E = 2 1 3")
    # E-stop: normally closed push button
    L["SW_NC"] = SymDef("SW_NC", prefix="SW", value="E-stop NC", numbers_hidden=False, names_hidden=True, name_offset=0,
                        pins=[Pin("1", "1", "passive", -4, 0, 0, 2), Pin("2", "2", "passive", 4, 0, 180, 2)],
                        graphics=[("circle", -2.032, 0, 0.508, 0, "none"), ("circle", 2.032, 0, 0.508, 0, "none"),
                                  ("poly", [(-2.54, -0.762), (2.54, -0.762)], 0.254, "none"),
                                  ("poly", [(0, -0.762), (0, 2.54)], 0.254, "none"),
                                  ("poly", [(-1.27, 2.54), (1.27, 2.54)], 0.254, "none")],
                        ref_at=(0, 5.08, 0), val_at=(0, 3.556, 0), desc="Push button, normally closed (E-stop)")
    # piezo buzzer
    L["Buzzer"] = SymDef("Buzzer", prefix="BZ", value="Buzzer", numbers_hidden=False, names_hidden=False, name_offset=0.254,
                         pins=[Pin("1", "+", "passive", -4, 2, 0, 2), Pin("2", "-", "passive", -4, -2, 0, 2)],
                         graphics=[("rect", -2.54, 3.81, 0, -3.81, "background"),
                                   ("poly", [(0, 3.81), (2.54, 6.35), (2.54, -6.35), (0, -3.81)], 0.254, "none")],
                         ref_at=(5.08, 1.27, 0), val_at=(5.08, -1.27, 0), desc="Piezo buzzer")
    # power (in the project library, not KiCad's "power" library: its copy changes between KiCad versions and
    # ERC then reports every power symbol as "doesn't match copy in library")
    pw = lambda name, pts, ty, vy: SymDef(name, lib="TempCtlDuo", prefix="#PWR" if name != "PWR_FLAG" else "#FLG", value=name,
                                          power=True, names_hidden=True, numbers_hidden=True, name_offset=0,
                                          pins=[Pin("1", name if name != "PWR_FLAG" else "pwr", ty, 0, 0,
                                                    270 if name == "GND" else 90, 0, True)],
                                          graphics=[("poly", pts, 0, "none")], val_at=(0, vy, 0),
                                          ref_at=(0, -6.35 if name == "GND" else 6.35, 0))
    L["GND"] = pw("GND", [(0, 0), (0, -1.27), (1.27, -1.27), (0, -2.54), (-1.27, -1.27), (0, -1.27)], "power_in", -3.81)
    up = [(-0.762, 1.27), (0, 2.54), (0.762, 1.27)]
    L["+5V"] = pw("+5V", [(0, 0), (0, 2.54)] + up[0:1] + [(0, 2.54), (0.762, 1.27)], "power_in", 3.556)
    L["+3V3"] = pw("+3V3", [(0, 0), (0, 2.54)] + up[0:1] + [(0, 2.54), (0.762, 1.27)], "power_in", 3.556)
    L["PWR_FLAG"] = pw("PWR_FLAG", [(0, 0), (0, 1.27), (-1.016, 1.905), (0, 2.54), (1.016, 1.905), (0, 1.27)],
                       "power_out", 3.175)
    for k in ("+5V", "+3V3"):  # the arrow head: two lines from the tip
        L[k].graphics = [("poly", [(0, 0), (0, 2.54)], 0, "none"), ("poly", [(-0.762, 1.27), (0, 2.54), (0.762, 1.27)], 0, "none")]

    # modules (boxes)
    L["DevKitC"] = box(
        "ESP32-S3-DevKitC-1", 28,
        left=[None, ("J1-3", "RST", "input")] + [(f"J1-{n}", lab, "bidirectional") for n, lab in
              ((4, "GPIO4"), (5, "GPIO5"), (6, "GPIO6"), (7, "GPIO7"), (8, "GPIO15"), (9, "GPIO16"), (10, "GPIO17"),
               (11, "GPIO18"), (12, "GPIO8"), (13, "GPIO3"), (14, "GPIO46"), (15, "GPIO9"), (16, "GPIO10"),
               (17, "GPIO11"), (18, "GPIO12"), (19, "GPIO13"), (20, "GPIO14"))] + [None],
        right=[None] + [(f"J3-{n}", lab, "bidirectional") for n, lab in
               ((2, "GPIO43 TX0"), (3, "GPIO44 RX0"), (4, "GPIO1"), (5, "GPIO2"), (6, "GPIO42"), (7, "GPIO41"),
                (8, "GPIO40"), (9, "GPIO39"), (10, "GPIO38"), (11, "GPIO37"), (12, "GPIO36"), (13, "GPIO35"),
                (14, "GPIO0"), (15, "GPIO45"), (16, "GPIO48"), (17, "GPIO47"), (18, "GPIO21"), (19, "GPIO20"),
                (20, "GPIO19"))] + [None],
        top=[("J1-1", "3V3", "power_out", 13), ("J1-2", "3V3", "passive", 18), ("J1-21", "5V", "power_out", 23)],
        bottom=[("J1-22", "GND", "passive", 6), ("J3-1", "GND", "passive", 11), ("J3-21", "GND", "passive", 17),
                ("J3-22", "GND", "passive", 22)],
        prefix="U", value="ESP32-S3-DevKitC-1", numbers_hidden=False,
        desc="ESP32-S3-DevKitC-1 v1.0 N16R8: J1 (left header) and J3 (right header), pin numbers J1-n / J3-n")
    L["Display"] = box(
        "Display_MSP4031", 22,
        right=[None, ("3", "LCD_CS", "input"), ("4", "LCD_RST", "input"), ("5", "LCD_RS", "input"), ("6", "SDI(MOSI)", "input"),
               ("7", "SCK", "input"), ("8", "LED", "input"), ("9", "SDO(MISO)", "tri_state"), ("10", "CTP_SCL", "input"),
               ("11", "CTP_RST", "input"), ("12", "CTP_SDA", "bidirectional"), ("13", "CTP_INT", "output"),
               ("14", "SD_CS", "input"), None],
        top=[("1", "VCC", "power_in", 8)], bottom=[("2", "GND", "power_in", 8)], prefix="DS",
        value="4.0in ST7796 MSP4031", numbers_hidden=False,
        desc="4.0in SPI display module MSP4031: ST7796S, FT6336U touch, micro-SD slot on the same SPI bus")
    L["RS485"] = box(
        "RS485_module_3V3", 16,
        left=[None, ("1", "DI", "input"), ("2", "RO", "output"), ("3", "DE", "input"), ("4", "~{RE}", "input"), None],
        right=[None, ("6", "A", "bidirectional"), ("7", "B", "bidirectional")],
        top=[("5", "VCC", "power_in", 12)], bottom=[("8", "GND", "power_in", 12)], prefix="U",
        value="RS485 3.3V (MAX3485)", desc="3.3 V RS485 transceiver module (MAX3485 / SP3485), not a 5 V MAX485")
    L["PMA"] = box(
        "PMA_KS4x_bus", 14, left=[("17", "DATA A", "bidirectional"), ("15", "DATA B", "bidirectional"),
                                  ("13", "RGND", "passive")],
        prefix="J", value="PMA KS 4x-1 bus", numbers_hidden=False,
        desc="PMA KS 40-1 / 41-1 / 42-1 Modbus terminals (one or two controllers on the bus)")
    L["MAX31855"] = box(
        "MAX31855_module", 20, left=[None, ("3", "CS", "input"), ("4", "SCK", "input"), ("5", "SO", "tri_state"), None],
        right=[None, ("6", "T+", "passive"), ("7", "T-", "passive")],
        top=[("1", "VIN", "power_in", 10)], bottom=[("2", "GND", "power_in", 10)], prefix="U",
        value="MAX31855 module", desc="MAX31855 thermocouple converter breakout (3.3 V)")
    L["TC"] = box("Thermocouple_K", 8, left=[("1", "+", "passive"), ("2", "-", "passive")], prefix="TC",
                  value="Type K", numbers_hidden=True, desc="Type K thermocouple")
    L["SSR"] = box("SSR_DA", 14, left=[("3", "+", "passive"), ("4", "-", "passive")],
                   right=[("1", "~", "passive"), ("2", "~", "passive")], prefix="K", value="SSR DC-AC, zero-cross",
                   numbers_hidden=False, desc="Solid state relay, DC input 3-32 V, AC output (e.g. SSR-25DA)")
    L["Relay"] = box(
        "SSR_DC_supply", 16, left=[None, ("3", "IN+", "passive"), ("4", "IN-", "passive")],
        right=[None, ("1", "OUT+", "passive"), ("2", "OUT-", "passive")], prefix="K",
        value="DC SSR, active high", numbers_hidden=True,
        desc="SSR supply relay: DC solid-state relay, input 3-32 V DC (driven directly by a GPIO), output >= 0.5 A DC")
    # ---- CYD (ESP32-2432S028R): the connectors as one box; pin numbers = connector-pin
    def cyd(name, led_pads):
        right = [None, ("P3-1", "IO21 (backlight)", "bidirectional"), ("P3-2", "IO22", "bidirectional"),
                 ("P3-3", "IO35 (input only)", "input"), None,
                 ("CN1-2", "IO27 (10k pull-up)", "bidirectional"), ("CN1-3", "IO22 (= P3-2)", "bidirectional"), None,
                 ("SD-2", "IO5 CD/DAT3", "bidirectional"), ("SD-3", "IO23 CMD", "bidirectional"),
                 ("SD-5", "IO18 CLK", "bidirectional"), ("SD-7", "IO19 DAT0", "bidirectional"), None]
        if led_pads:
            right += [("LED-R", "IO4 (LED pad R)", "bidirectional"), ("LED-G", "IO16 (LED pad G)", "bidirectional"),
                      ("LED-B", "IO17 (LED pad B)", "bidirectional"), None]
        right += [("P4-1", "SPK+", "output"), ("P4-2", "SPK-", "output"), None,
                  ("P1-2", "TX (IO1)", "output"), ("P1-3", "RX (IO3)", "input"), None]
        return box(name, 30, right=right, plen=4,
                   top=[("CN1-1", "3V3", "power_out", 6), ("SD-4", "VDD", "passive", 12),
                        ("P1-1", "VIN", "power_out", 18)],
                   bottom=[("P1-4", "GND", "power_in", 4), ("P3-4", "GND", "passive", 10),
                           ("CN1-4", "GND", "passive", 16), ("SD-6", "VSS", "passive", 22)],
                   prefix="U", value="ESP32-2432S028R (CYD)", numbers_hidden=False,
                   desc="ESP32-2432S028R (Cheap Yellow Display): connectors P1, P3, CN1, P4, the micro-SD slot (via a "
                        "breakout adapter)" + (" and the RGB LED pads (LED removed)" if led_pads else "") +
                        "; pin numbers = connector-pin")
    L["CYD"] = cyd("CYD_ESP32-2432S028R", False)
    L["CYD_LED"] = cyd("CYD_ESP32-2432S028R_LEDpads", True)
    L["Speaker"] = SymDef("Speaker", prefix="LS", value="Speaker", numbers_hidden=False, names_hidden=False,
                          name_offset=0.254,
                          pins=[Pin("1", "+", "passive", -4, 2, 0, 2), Pin("2", "-", "passive", -4, -2, 0, 2)],
                          graphics=[("rect", -2.54, 3.81, 0, -3.81, "background"),
                                    ("poly", [(0, 3.81), (3.81, 7.62), (3.81, -7.62), (0, -3.81)], 0.254, "none")],
                          ref_at=(6.35, 1.27, 0), val_at=(6.35, -1.27, 0), desc="Loudspeaker (8 ohm) or passive piezo")
    L["WS2812"] = box("WS2812B", 12, left=[None, ("4", "DIN", "input"), None],
                      right=[None, ("2", "DOUT", "output"), None],
                      top=[("1", "VDD", "power_in", 6)], bottom=[("3", "VSS", "power_in", 6)], prefix="D",
                      value="WS2812B", numbers_hidden=False, desc="Addressable RGB LED WS2812B (status LED)")
    return L
