"""Sheet model (grid units, y down): parts, wires, labels, power symbols + the generator's own connectivity check."""
from model import G, lib, uid

L = lib()


class Part:
    def __init__(self, sym, ref, x, y, value=None, dnp=False, ref_pos=None, val_pos=None, hide_ref=False):
        self.sym, self.ref, self.x, self.y = sym, ref, x, y
        self.value = value or sym.value
        self.dnp = dnp
        self.ref_pos, self.val_pos = ref_pos, val_pos  # (x, y, justify) grid; None = default
        self.hide_ref = hide_ref

    def pin_xy(self, num):
        p = self.sym.pin(num)
        return (self.x + p.x, self.y - p.y)

    def pin_out(self, num):
        """direction (dx, dy) pointing away from the body"""
        a = self.sym.pin(num).angle
        return {0: (-1, 0), 180: (1, 0), 270: (0, -1), 90: (0, 1)}[a]


class Sheet:
    def __init__(self, project, title, env, rev, notes, ktitle, kcomment):
        """project: file name; title / notes: the drawn title block (notes = the line above the title);
        ktitle / kcomment: KiCad's title block; env: firmware env; rev: firmware version"""
        self.project, self.title, self.env, self.rev, self.notes = project, title, env, rev, notes
        self.ktitle, self.kcomment = ktitle, kcomment
        self.parts, self.wires, self.labels, self.junctions, self.ncs = [], [], [], [], []
        self.texts, self.frames = [], []
        self.npwr = 0
        self.nflg = 0

    def add(self, key, ref, x, y, **kw):
        p = Part(L[key], ref, x, y, **kw)
        self.parts.append(p)
        return p

    def power(self, name, x, y):
        if name == "PWR_FLAG":
            self.nflg += 1
            ref = f"#FLG{self.nflg:02d}"
        else:
            self.npwr += 1
            ref = f"#PWR{self.npwr:03d}"
        return self.add(name, ref, x, y, hide_ref=True)

    def wire(self, *pts):
        for a, b in zip(pts, pts[1:]):
            assert a[0] == b[0] or a[1] == b[1], (a, b)
            self.wires.append((a, b))

    def label(self, name, x, y, side):
        self.labels.append((name, x, y, 180 if side == "L" else 0))

    def junction(self, x, y):
        self.junctions.append((x, y))

    def nc(self, part, num):
        self.ncs.append(part.pin_xy(num))

    def text(self, x, y, s, size=1.27, bold=False):
        self.texts.append((x, y, s, size, bold))

    def frame(self, x1, y1, x2, y2, title):
        self.frames.append((x1, y1, x2, y2, title))

    def stub(self, part, num, net, length=6):
        """pin -> short wire outward -> net label, power symbol ('+5V', '+3V3', 'GND') or no-connect ('NC')"""
        x, y = part.pin_xy(num)
        if net == "NC":
            self.ncs.append((x, y))
            return
        dx, dy = part.pin_out(num)
        if net in ("+5V", "+3V3", "GND"):
            if length:
                ex, ey = x + dx * length, y + dy * length
                self.wire((x, y), (ex, ey))
            else:
                ex, ey = x, y
            self.power(net, ex, ey)
            return
        ex, ey = x + dx * length, y + dy * length
        self.wire((x, y), (ex, ey))
        self.label(net, ex, ey, "L" if dx < 0 else "R")


# ---------------------------------------------------------------- checks

def nets(S):
    """union-find over pin tips, wire ends, labels (same name), power symbols (same name); junctions only mark."""
    parent = {}

    def f(a):
        parent.setdefault(a, a)
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def u(a, b):
        parent[f(a)] = f(b)

    pins = {}
    for p in S.parts:
        for pin in p.sym.pins:
            pt = p.pin_xy(pin.num)
            pins[(p.ref, pin.num)] = pt
            f(("pt", pt))
            u(("pin", p.ref, pin.num), ("pt", pt))
            if p.sym.power and p.sym.value != "PWR_FLAG":
                u(("pt", pt), ("net", p.sym.value))
    for a, b in S.wires:
        u(("pt", a), ("pt", b))
    for name, x, y, _ in S.labels:
        u(("pt", (x, y)), ("net", name))
    # wire ends that touch the middle of another wire are not connections: report them
    problems = []
    seg = S.wires
    for a, b in seg:
        for e in (a, b):
            for c, d in seg:
                if (c, d) == (a, b):
                    continue
                if c[0] == d[0] == e[0] and min(c[1], d[1]) < e[1] < max(c[1], d[1]):
                    problems.append(f"T-joint on a wire middle at {e}")
                if c[1] == d[1] == e[1] and min(c[0], d[0]) < e[0] < max(c[0], d[0]):
                    problems.append(f"T-joint on a wire middle at {e}")
    groups = {}
    for (ref, num) in pins:
        groups.setdefault(f(("pin", ref, num)), []).append(f"{ref}.{num}")
    named = {}
    for k in list(parent):
        if k[0] == "net":
            named.setdefault(f(k), set()).add(k[1])
    out = {}
    for root, members in groups.items():
        names = named.get(root, set())
        out[" / ".join(sorted(names)) or "(unnamed:" + members[0] + ")"] = sorted(members)
    # pins without any connection (and not marked NC)
    ncset = set(S.ncs)
    counts = {}
    for k in parent:
        if k[0] == "pt":
            counts.setdefault(f(k), 0)
    for (ref, num), pt in pins.items():
        r = f(("pin", ref, num))
        others = [m for m in groups[r] if m != f"{ref}.{num}"]
        if not others and pt not in ncset and r not in named:
            problems.append(f"unconnected pin {ref}.{num}")
        if pt in ncset and (others or r in named):
            problems.append(f"NC pin connected {ref}.{num}")
    # junction sanity: a junction must sit on >= 3 connection ends
    for j in S.junctions:
        ends = sum(1 for a, b in S.wires for e in (a, b) if e == j) + sum(1 for pt in pins.values() if pt == j)
        if ends < 3:
            problems.append(f"junction with {ends} ends at {j}")
    # points where 3+ ends meet without a junction
    ends = {}
    for a, b in S.wires:
        for e in (a, b):
            ends[e] = ends.get(e, 0) + 1
    for pt in pins.values():
        if pt in ends:
            ends[pt] += 1
    for e, n in ends.items():
        if n >= 3 and e not in S.junctions:
            problems.append(f"missing junction at {e} ({n} ends)")
    return out, problems


def report(S):
    out, problems = nets(S)
    for k in sorted(out):
        print(f"{k:28s} {' '.join(out[k])}")
    print("\n".join(problems) or "no problems")
    return problems
