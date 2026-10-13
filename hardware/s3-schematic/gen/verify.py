"""Reads the written .kicad_sch back (independent of the generator) and prints its netlist the way KiCad
connects it: pins at symbol position + (x, -y) of the library pin, wires end to end, labels by name, power
symbols by their pin name. Also checks: every pin and wire end on the 1.27 mm grid, no dangling wire ends."""
import sys

def parse(s):
    toks, i, n = [], 0, len(s)
    while i < n:
        c = s[i]
        if c in "()":
            toks.append(c); i += 1
        elif c.isspace():
            i += 1
        elif c == '"':
            j, buf = i + 1, ""
            while s[j] != '"':
                if s[j] == "\\":
                    buf += {"n": "\n"}.get(s[j + 1], s[j + 1]); j += 2
                else:
                    buf += s[j]; j += 1
            toks.append(("str", buf)); i = j + 1
        else:
            j = i
            while j < n and not s[j].isspace() and s[j] not in "()":
                j += 1
            toks.append(s[i:j]); i = j
    pos = 0
    def rd():
        nonlocal pos
        t = toks[pos]; pos += 1
        if t == "(":
            lst = []
            while toks[pos] != ")":
                lst.append(rd())
            pos += 1
            return lst
        return t[1] if isinstance(t, tuple) else t
    return rd()

def find(lst, key):
    return [x for x in lst if isinstance(x, list) and x and x[0] == key]

def one(lst, key):
    r = find(lst, key)
    return r[0] if r else None

sch = parse(open(sys.argv[1]).read())
libs = {}
for sym in one(sch, "lib_symbols")[1:]:
    name = sym[1]
    pins = []
    power = any(x == "power" or x == ["power"] for x in sym)
    for sub in find(sym, "symbol"):
        for p in find(sub, "pin"):
            at = one(p, "at")
            pins.append((one(p, "number")[1], one(p, "name")[1], float(at[1]), float(at[2]), p[1]))
    libs[name] = (pins, power)
R = lambda v: round(v, 3)
parent = {}
def f(a):
    parent.setdefault(a, a)
    while parent[a] != a:
        parent[a] = parent[parent[a]]; a = parent[a]
    return a
def u(a, b): parent[f(a)] = f(b)
pinpts = {}
offgrid = []
def grid(x, y, what):
    for v in (x, y):
        if abs(v / 1.27 - round(v / 1.27)) > 1e-6:
            offgrid.append((what, x, y)); return
for s in find(sch, "symbol"):
    lid = one(s, "lib_id")[1]
    at = one(s, "at")
    X, Y = float(at[1]), float(at[2])
    assert at[3] == "0", "rotated symbol"
    ref = [p for p in find(s, "property") if p[1] == "Reference"][0][2]
    pins, power = libs[lid]
    for num, name, px, py, typ in pins:
        pt = (R(X + px), R(Y - py))
        grid(*pt, f"{ref}.{num}")
        pinpts[(ref, num)] = pt
        u(("pin", ref, num), ("pt", pt))
        if power and typ == "power_in":
            u(("pt", pt), ("net", name))
ends = {}
for w in find(sch, "wire"):
    pts = one(w, "pts")
    a, b = [(R(float(p[1])), R(float(p[2]))) for p in pts[1:]]
    grid(*a, "wire"); grid(*b, "wire")
    u(("pt", a), ("pt", b))
    for e in (a, b):
        ends[e] = ends.get(e, 0) + 1
for l in find(sch, "label"):
    at = one(l, "at")
    pt = (R(float(at[1])), R(float(at[2])))
    u(("pt", pt), ("net", l[1]))
    ends[pt] = ends.get(pt, 0) + 1
ncs = {(R(float(one(x, "at")[1])), R(float(one(x, "at")[2]))) for x in find(sch, "no_connect")}
dangling = [e for e, n in ends.items() if n == 1 and e not in pinpts.values()]
groups = {}
for (ref, num) in pinpts:
    groups.setdefault(f(("pin", ref, num)), []).append(f"{ref}.{num}")
names = {}
for k in list(parent):
    if k[0] == "net":
        names.setdefault(f(k), set()).add(k[1])
lines = []
for r, m in groups.items():
    m = sorted(x for x in m if not x.startswith("#"))
    nm = "/".join(sorted(names.get(r, []))) or "-"
    if len(m) + len(names.get(r, [])) > 1 or nm != "-":
        lines.append(f"{nm:12s} {' '.join(m)}")
single = [m[0] for r, m in groups.items() if len(m) == 1 and r not in names and pinpts[tuple(m[0].split('.', 1))] not in ncs]
print("\n".join(sorted(lines)))
print("unconnected pins without NC flag:", single or "none")
print("off-grid points:", offgrid or "none")
print("dangling wire ends:", dangling or "none")
