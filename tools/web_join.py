#!/usr/bin/env python3
"""Joins the web page's sources (web/src/, see web/src/README.md) into web/index.html: the single-file page that
tools/web_embed.py puts into the firmware and tools/build_site.py into the launcher and the hosted page.
web/src/index.html is the page with include lines `<!-- include: <file> -->` (a whole line, path relative to
web/src/); each is replaced by that file's text. web/index.html is generated (not in git): every tool that needs it
calls join() first. Python 3 standard library only.
  python tools/web_join.py            write web/index.html (only when its content changes)
  python tools/web_join.py --check    exit 1 if web/index.html is missing or differs from the join result
  from web_join import join           join() writes it and returns the page text; stale(): a source is newer"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "web", "src")
SKELETON = os.path.join(SRC, "index.html")
OUT = os.path.join(ROOT, "web", "index.html")
INCLUDE = re.compile(r"<!-- include: ([A-Za-z0-9_./-]+) -->")


def _read(path):
    # LF line endings whatever the checkout has (Git for Windows may write CRLF)
    with open(path, encoding="utf-8", newline="") as f:
        return f.read().replace("\r\n", "\n")


def _includes(lines):
    """(line index, relative path) of each include line of the skeleton"""
    out = []
    for i, line in enumerate(lines):
        if "<!-- include:" not in line:
            continue
        m = INCLUDE.fullmatch(line)
        if not m or ".." in m.group(1).split("/"):
            raise SystemExit("web_join: bad include line %d in web/src/index.html: %r" % (i + 1, line))
        out.append((i, m.group(1)))
    return out


def sources():
    """the skeleton and the files it includes, in page order"""
    lines = _read(SKELETON).split("\n")
    return [SKELETON] + [os.path.join(SRC, rel) for _i, rel in _includes(lines)]


def render():
    """the joined page (text, LF line endings)"""
    lines = _read(SKELETON).split("\n")
    inc = dict(_includes(lines))
    parts = []
    for i, line in enumerate(lines):
        if i not in inc:
            parts.append(line + "\n")
            continue
        path = os.path.join(SRC, inc[i])
        if not os.path.isfile(path):
            raise SystemExit("web_join: web/src/index.html line %d: %s not found" % (i + 1, inc[i]))
        text = _read(path)
        parts.append(text if text.endswith("\n") or not text else text + "\n")
    page = "".join(parts)
    return page[:-1]  # (the skeleton's last line: the split above gives an empty one after its final newline)


def stale(out=OUT):
    """True when `out` is missing or older than one of the sources (a cheap test before join())"""
    if not os.path.isfile(out):
        return True
    t = os.path.getmtime(out)
    return any(os.path.getmtime(p) > t for p in sources() if os.path.isfile(p))


def join(out=OUT):
    """Writes the joined page to `out` when its content changed (else only its time, so stale() is false);
    returns the page text."""
    page = render()
    data = page.encode("utf-8")
    old = None
    if os.path.isfile(out):
        with open(out, "rb") as f:
            old = f.read()
    if old != data:
        tmp = out + ".tmp"
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, out)
        print("web_join: wrote %s (%d bytes)" % (os.path.relpath(out, ROOT), len(data)))
    elif stale(out):
        os.utime(out)
    return page


def check(out=OUT):
    """True when `out` exists and is exactly the join result"""
    if not os.path.isfile(out):
        return False
    with open(out, "rb") as f:
        return f.read() == render().encode("utf-8")


if __name__ == "__main__":
    if "--check" in sys.argv[1:]:
        ok = check()
        print("web_join: web/index.html %s" % ("matches web/src/" if ok else "is missing or differs from web/src/"))
        sys.exit(0 if ok else 1)
    join()
