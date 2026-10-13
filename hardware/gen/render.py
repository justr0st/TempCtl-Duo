"""Draws a sheet as SVG (mm units, A3) in the look of the KiCad default theme."""
import html
import sys

from model import G

W, H = 420, 297
C = {"bg": "#FFFFFF", "wire": "#008400", "body": "#840000", "fill": "#FFFFC2", "pinname": "#006464",
     "pinnum": "#A90000", "ref": "#006464", "label": "#000000", "nc": "#0000C2", "note": "#000084",
     "frame": "#8a8a8a", "dnp": "#D00000", "border": "#840000"}
FONT = "DejaVu Sans, Arial, sans-serif"
FS = 1.42  # font-size per mm of KiCad text height


def t(x, y, s, size=1.27, color="#000", anchor="start", weight="normal", rot=0, base="auto", deco=None):
    tr = f' transform="rotate({rot} {x:.3f} {y:.3f})"' if rot else ""
    d = f' text-decoration="{deco}"' if deco else ""
    return (f'<text x="{x:.3f}" y="{y:.3f}" font-size="{size * FS:.3f}" fill="{color}" text-anchor="{anchor}" '
            f'font-weight="{weight}" dominant-baseline="{base}"{tr}{d}>{html.escape(s)}</text>')


def line(pts, color, w=0.1524, dash=None):
    p = " ".join(f"{x:.3f},{y:.3f}" for x, y in pts)
    da = f' stroke-dasharray="{dash}"' if dash else ""
    return f'<polyline points="{p}" fill="none" stroke="{color}" stroke-width="{w}" stroke-linecap="round" stroke-linejoin="round"{da}/>'


def name_txt(s):
    if s.startswith("~{") and s.endswith("}"):
        return s[2:-1], "overline"
    return s, None


def part(p, o):
    sd = p.sym
    X, Y = p.x * G, p.y * G
    sx = lambda x: X + x
    sy = lambda y: Y - y
    col = C["body"]
    xs, ys = [], []
    for g in sd.graphics:
        if g[0] == "rect":
            _, x1, y1, x2, y2, fill = g
            f = C["fill"] if fill == "background" else (col if fill == "outline" else "none")
            o.append(f'<rect x="{min(sx(x1), sx(x2)):.3f}" y="{min(sy(y1), sy(y2)):.3f}" width="{abs(x2 - x1):.3f}" '
                     f'height="{abs(y2 - y1):.3f}" fill="{f}" stroke="{col}" stroke-width="0.254"/>')
            xs += [sx(x1), sx(x2)]
            ys += [sy(y1), sy(y2)]
        elif g[0] == "poly":
            _, pts, w, fill = g
            pp = [(sx(x), sy(y)) for x, y in pts]
            if fill == "outline":
                o.append(f'<polygon points="{" ".join(f"{a:.3f},{b:.3f}" for a, b in pp)}" fill="{col}" stroke="{col}" stroke-width="{max(w, 0.15)}"/>')
            else:
                o.append(line(pp, col, max(w, 0.15)))
            xs += [a for a, _ in pp]
            ys += [b for _, b in pp]
        elif g[0] == "circle":
            _, cx, cy, r, w, fill = g
            o.append(f'<circle cx="{sx(cx):.3f}" cy="{sy(cy):.3f}" r="{r}" fill="none" stroke="{col}" stroke-width="{max(w, 0.15)}"/>')
            xs += [sx(cx) - r, sx(cx) + r]
            ys += [sy(cy) - r, sy(cy) + r]
    for pin in sd.pins:
        tx, ty = (p.x + pin.x) * G, (p.y - pin.y) * G
        d = {0: (1, 0), 180: (-1, 0), 90: (0, -1), 270: (0, 1)}[pin.angle]
        ln = pin.length * G
        ex, ey = tx + d[0] * ln, ty + d[1] * ln
        if ln:
            o.append(line([(tx, ty), (ex, ey)], col, 0.1524))
        xs += [tx]
        ys += [ty]
        show_name = not sd.names_hidden and pin.name not in ("~", "")
        nm, deco = name_txt(pin.name)
        off = sd.name_offset + 0.3
        if show_name:
            if d[1] == 0:
                o.append(t(ex + d[0] * off, ey, nm, 1.27, C["pinname"], "start" if d[0] > 0 else "end", base="central", deco=deco))
            elif d[1] > 0:  # top pin: name inside, reading upwards
                o.append(t(ex, ey + off, nm, 1.27, C["pinname"], "end", rot=-90, base="central", deco=deco))
            else:
                o.append(t(ex, ey - off, nm, 1.27, C["pinname"], "start", rot=-90, base="central", deco=deco))
        if not sd.numbers_hidden:
            mx, my = (tx + ex) / 2, (ty + ey) / 2
            if d[1] == 0:
                o.append(t(mx, my - 0.35, pin.num, 1.0, C["pinnum"], "middle"))
            else:
                o.append(t(mx - 0.35, my, pin.num, 1.0, C["pinnum"], "middle", rot=-90))
    # fields
    def field(pos, default, s, color):
        if pos:
            fx, fy, j = pos
            anchor = {"left": "start", "right": "end", "center": "middle"}[j]
        else:
            fx, fy = p.x + default[0] / G, p.y - default[1] / G
            anchor = "middle"
        o.append(t(fx * G, fy * G, s, 1.27, color, anchor, base="central"))
    if not p.hide_ref:
        field(p.ref_pos, sd.ref_at, p.ref, C["ref"])
    field(p.val_pos, sd.val_at, p.value + ("  DNP" if p.dnp else ""), C["dnp"] if p.dnp else C["ref"])
    if p.dnp and xs:
        x1, x2, y1, y2 = min(xs), max(xs), min(ys), max(ys)
        o.append(line([(x1, y1), (x2, y2)], C["dnp"], 0.3))
        o.append(line([(x1, y2), (x2, y1)], C["dnp"], 0.3))


def svg(S):
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}" font-family="{FONT}">',
         f'<rect width="{W}" height="{H}" fill="{C["bg"]}"/>']
    # page border with the reference grid
    o.append(f'<rect x="10" y="10" width="{W - 20}" height="{H - 20}" fill="none" stroke="{C["border"]}" stroke-width="0.25"/>')
    o.append(f'<rect x="12" y="12" width="{W - 24}" height="{H - 24}" fill="none" stroke="{C["border"]}" stroke-width="0.15"/>')
    for i in range(8):
        x = 12 + (W - 24) * (i + 0.5) / 8
        for y in (11, H - 11):
            o.append(t(x, y, str(i + 1), 1.27, C["border"], "middle", base="central"))
    for i in range(6):
        y = 12 + (H - 24) * (i + 0.5) / 6
        for x in (11, W - 11):
            o.append(t(x, y, "ABCDEFGH"[i], 1.27, C["border"], "middle", base="central"))
    # frames
    for x1, y1, x2, y2, title in S.frames:
        o.append(f'<rect x="{x1 * G}" y="{y1 * G}" width="{(x2 - x1) * G}" height="{(y2 - y1) * G}" fill="none" '
                 f'stroke="{C["frame"]}" stroke-width="0.254" stroke-dasharray="1.6 1.0"/>')
        o.append(t((x1 + 1) * G, (y1 + 3) * G, title, 2.0, C["note"], weight="bold"))
    for x, y, s, size, bold in S.texts:
        o.append(t(x * G, y * G, s, size, C["note"], weight="bold" if bold else "normal"))
    for (x1, y1), (x2, y2) in S.wires:
        o.append(line([(x1 * G, y1 * G), (x2 * G, y2 * G)], C["wire"], 0.1524))
    for p in S.parts:
        part(p, o)
    for x, y in S.junctions:
        o.append(f'<circle cx="{x * G}" cy="{y * G}" r="0.48" fill="{C["wire"]}"/>')
    for x, y in S.ncs:
        X, Y, r = x * G, y * G, 0.635
        o.append(line([(X - r, Y - r), (X + r, Y + r)], C["nc"], 0.2))
        o.append(line([(X - r, Y + r), (X + r, Y - r)], C["nc"], 0.2))
    for name, x, y, ang in S.labels:
        o.append(t(x * G + (0.3 if ang == 0 else -0.3), y * G - 0.35, name, 1.27, C["label"], "start" if ang == 0 else "end"))
    # title block
    bx, by, bw, bh = W - 12 - 110, H - 12 - 32, 110, 32
    o.append(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" fill="#fff" stroke="{C["border"]}" stroke-width="0.25"/>')
    for yy in (by + 8, by + 22):
        o.append(line([(bx, yy), (bx + bw, yy)], C["border"], 0.15))
    o.append(t(bx + 2, by + 5.5, S.notes, 1.27, C["border"]))
    o.append(t(bx + 2, by + 13, "Title:", 1.27, C["border"]))
    o.append(t(bx + 12, by + 13.5, S.title, 2.4, "#000", weight="bold"))
    o.append(t(bx + 2, by + 19, f"env {S.env}, firmware {S.rev}", 1.27, C["border"]))
    o.append(t(bx + 2, by + 25.5, "Size: A3", 1.27, C["border"]))
    o.append(t(bx + 22, by + 25.5, "Date: 2026-10-13", 1.27, C["border"]))
    o.append(t(bx + 54, by + 25.5, f"Rev: {S.rev}", 1.27, C["border"]))
    o.append(t(bx + 80, by + 25.5, "Sheet: 1/1", 1.27, C["border"]))
    o.append(t(bx + 2, by + 30, f"File: {S.project}.kicad_sch", 1.27, C["border"]))
    o.append("</svg>")
    return "\n".join(o)
