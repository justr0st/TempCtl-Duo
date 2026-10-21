#!/usr/bin/env python3
"""The wiki (docs/wiki/*.md) as HTML pages for the hosted page (<site>/wiki/): called by tools/build_site.py
--web; by hand: python3 tools/wiki_site.py <out dir>. No packages: a small Markdown converter for what the
wiki pages use (headings, paragraphs, lists, tables, code, quotes, images, links, inline HTML such as
<picture>). Links between pages (Page.md#part) become Page.html#part, Home.md the index; the navigation
comes from docs/wiki/_Sidebar.md. GitHub shows the same .md files as they are."""
import html, os, re, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIKI = os.path.join(ROOT, "docs", "wiki")


def slug(t):
    t = re.sub(r"<[^>]+>", "", t).strip().lower()
    t = re.sub(r"[^\w\- ]", "", t, flags=re.U)
    return re.sub(r"\s", "-", t)


def page_url(name):
    return "index.html" if name == "Home" else name + ".html"


def link(u):
    m = re.match(r"^([A-Za-z0-9_\-]+)\.md(#.*)?$", u)
    if m: return page_url(m.group(1)) + (m.group(2) or "")
    return u


def inline(s):
    """inline Markdown; HTML tags in the text pass through, the rest is escaped"""
    out, i = [], 0
    code = re.compile(r"`([^`]+)`")
    parts = code.split(s)
    for k, p in enumerate(parts):
        if k % 2:
            out.append("<code>" + html.escape(p) + "</code>")
            continue
        # keep inline HTML tags, escape the text between them
        toks = re.split(r"(<[a-zA-Z/][^>]*>)", p)
        t = []
        for j, x in enumerate(toks):
            if j % 2: t.append(x)
            else: t.append(html.escape(x, quote=False))
        p = "".join(t)
        p = re.sub(r"!\[([^\]]*)\]\(([^)\s]+)\)", lambda m: f'<img src="{link(m.group(2))}" alt="{m.group(1)}" loading="lazy">', p)
        p = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)",
                   lambda m: f'<a href="{link(m.group(2))}"' + (' target="_blank" rel="noopener"' if m.group(2).startswith("http") else "") + f'>{m.group(1)}</a>', p)
        p = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", p)
        p = re.sub(r"(?<![\w*])\*([^*\s][^*]*)\*(?![\w*])", r"<i>\1</i>", p)
        out.append(p)
    return "".join(out)


def convert(md):
    lines = md.replace("\r", "").split("\n")
    o, i, toc = [], 0, []
    para = []

    def flush():
        if para:
            o.append("<p>" + inline(" ".join(para)) + "</p>")
            para.clear()

    while i < len(lines):
        l = lines[i]
        s = l.strip()
        if not s:
            flush(); i += 1; continue
        if s.startswith("```"):
            flush(); j = i + 1; buf = []
            while j < len(lines) and not lines[j].strip().startswith("```"):
                buf.append(lines[j]); j += 1
            o.append("<pre><code>" + html.escape("\n".join(buf)) + "</code></pre>")
            i = j + 1; continue
        m = re.match(r"^(#{1,4})\s+(.*)$", s)
        if m:
            flush(); n = len(m.group(1)); t = m.group(2); sid = slug(t)
            if n == 2: toc.append((sid, re.sub(r"<[^>]+>", "", t)))
            o.append(f'<h{n} id="{sid}">{inline(t)}' + (f'<a class="anc" href="#{sid}" aria-label="link">#</a>' if n > 1 else "") + f"</h{n}>")
            i += 1; continue
        if s.startswith("<") and not s.startswith("<b") and not s.startswith("<i") and not s.startswith("<code"):
            flush(); buf = []
            while i < len(lines) and lines[i].strip():
                buf.append(lines[i]); i += 1
            o.append("\n".join(buf)); continue
        if s.startswith(">"):
            flush(); buf = []
            while i < len(lines) and lines[i].strip().startswith(">"):
                buf.append(lines[i].strip()[1:].strip()); i += 1
            kind = ""
            mm = re.match(r"^\*\*(Note|Warning|Tip|Safety)[:.]?\*\*", buf[0]) if buf else None
            if mm: kind = " " + mm.group(1).lower()
            o.append(f'<blockquote class="q{kind}">' + convert("\n".join(buf))[0] + "</blockquote>"); continue
        if s.startswith("|") and i + 1 < len(lines) and re.match(r"^\|?\s*:?-{2,}", lines[i + 1].strip()):
            flush()
            row = lambda x: [c.strip().replace("\0", "|") for c in x.strip().strip("|").replace("\\|", "\0").split("|")]
            head = row(lines[i]); i += 2; body = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                body.append(row(lines[i])); i += 1
            t = '<div class="tw"><table><thead><tr>' + "".join("<th>" + inline(c) + "</th>" for c in head) + "</tr></thead><tbody>"
            for r in body: t += "<tr>" + "".join("<td>" + inline(c) + "</td>" for c in r) + "</tr>"
            o.append(t + "</tbody></table></div>"); continue
        if re.match(r"^([-*]|\d+\.)\s", s):
            flush(); o.append(lists(lines, i));
            while i < len(lines) and (lines[i].strip() == "" and i + 1 < len(lines) and re.match(r"^\s*([-*]|\d+\.)\s", lines[i + 1]) or re.match(r"^\s*([-*]|\d+\.)\s", lines[i]) or (lines[i].startswith("  ") and lines[i].strip())):
                i += 1
            continue
        para.append(s); i += 1
    flush()
    return "\n".join(o), toc


def lists(lines, i):
    """a (nested) list starting at line i: items by their indentation, continuation lines join the item"""
    items = []  # (indent, ordered, text)
    while i < len(lines):
        l = lines[i]
        m = re.match(r"^(\s*)([-*]|\d+\.)\s+(.*)$", l)
        if m:
            items.append([len(m.group(1)), m.group(2)[-1] == ".", m.group(3)]); i += 1
        elif l.strip() and l.startswith("  ") and items:
            items[-1][2] += " " + l.strip(); i += 1
        elif not l.strip() and i + 1 < len(lines) and re.match(r"^\s*([-*]|\d+\.)\s", lines[i + 1]):
            i += 1
        else: break

    def build(k, ind):
        ordered = items[k][1]
        out = ["<ol>" if ordered else "<ul>"]
        while k < len(items) and items[k][0] >= ind:
            if items[k][0] > ind:
                sub, k = build(k, items[k][0]); out[-1] = out[-1][:-5] + sub + "</li>"; continue
            out.append("<li>" + inline(items[k][2]) + "</li>"); k += 1
        out.append("</ol>" if ordered else "</ul>")
        return "".join(out), k
    return build(0, items[0][0])[0] if items else ""


CSS = """@font-face{font-family:"Barlow";font-weight:400;font-display:swap;src:url(/f/barlow-400.woff2) format("woff2")}
@font-face{font-family:"Barlow";font-weight:500;font-display:swap;src:url(/f/barlow-500.woff2) format("woff2")}
@font-face{font-family:"Barlow";font-weight:600 700;font-display:swap;src:url(/f/barlow-600.woff2) format("woff2")}
@font-face{font-family:"Barlow Semi Condensed";font-weight:600;font-display:swap;src:url(/f/barlow-semi-condensed-600.woff2) format("woff2")}
:root{--bg:#EDF0F1;--panel:#FFFFFF;--sunk:#F4F6F7;--line:#D3DADD;--text:#172024;--muted:#5B6970;--acc:#1F6FB2;--amber:#A87400;--amber-bg:#FFF3D1;--danger:#B3261E;--danger-bg:#FDECEA;--code:#EEF1F3;color-scheme:light}
@media (prefers-color-scheme:dark){:root{--bg:#11161A;--panel:#192024;--sunk:#141A1E;--line:#2B353B;--text:#E3E9EB;--muted:#8D9BA2;--acc:#86C9FF;--amber:#F3C335;--amber-bg:#3A3217;--danger:#F2867E;--danger-bg:#3A1F1D;--code:#222B30;color-scheme:dark}}
*{box-sizing:border-box}html{scroll-padding-top:16px}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.55 Barlow,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
a{color:var(--acc)}
.wrap{display:grid;grid-template-columns:260px minmax(0,1fr);max-width:1320px;margin:0 auto;gap:24px;padding:20px}
aside{position:sticky;top:20px;align-self:start;max-height:calc(100vh - 40px);overflow:auto;background:var(--panel);border:1px solid var(--line);border-radius:16px;padding:16px}
.brand{display:flex;align-items:center;gap:10px;text-decoration:none;color:var(--text);margin-bottom:12px}.brand img{width:40px;height:40px}
.brand b{font:600 20px "Barlow Semi Condensed",Barlow,sans-serif;display:block}.brand small{color:var(--muted);font-size:13px}
aside nav a{display:block;padding:6px 10px;border-radius:8px;color:var(--text);text-decoration:none;font-weight:500}
aside nav a:hover{background:var(--sunk)}aside nav a.on{background:var(--sunk);box-shadow:inset 3px 0 0 var(--acc)}
aside nav .sub{margin:2px 0 8px 10px;border-left:1px solid var(--line);padding-left:6px}aside nav .sub a{font-size:14px;font-weight:400;color:var(--muted);padding:3px 8px}
aside .grp{margin:14px 0 4px 10px;font-size:12px;letter-spacing:.06em;text-transform:uppercase;color:var(--muted);font-weight:600}
aside .ext{margin-top:14px;padding-top:12px;border-top:1px solid var(--line);display:flex;flex-direction:column;gap:2px}
main{background:var(--panel);border:1px solid var(--line);border-radius:16px;padding:28px 40px 40px;min-width:0}
h1{font:600 34px/1.2 "Barlow Semi Condensed",Barlow,sans-serif;margin:0 0 12px}
h2{font:600 26px/1.25 "Barlow Semi Condensed",Barlow,sans-serif;margin:36px 0 10px;padding-top:10px;border-top:1px solid var(--line)}
h3{font-size:19px;margin:26px 0 8px}h4{font-size:16px;margin:20px 0 6px}
h2 .anc,h3 .anc{opacity:0;margin-left:8px;text-decoration:none;color:var(--muted)}h2:hover .anc,h3:hover .anc{opacity:1}
code{background:var(--code);border-radius:5px;padding:1px 5px;font:14px/1.4 ui-monospace,Consolas,Menlo,monospace}
pre{background:var(--code);border-radius:10px;padding:12px 14px;overflow:auto}pre code{padding:0;background:none}
.tw{overflow-x:auto;margin:12px 0}table{border-collapse:collapse;min-width:60%}th,td{border-bottom:1px solid var(--line);padding:6px 10px;text-align:left;vertical-align:top}th{background:var(--sunk);font-weight:600}
img,picture img{max-width:100%;height:auto;border-radius:10px}
figure{margin:16px 0}figcaption{color:var(--muted);font-size:14px;margin-top:4px}
.shot img{box-shadow:0 6px 22px -12px rgba(0,0,0,.45);border:1px solid var(--line)}
blockquote.q{margin:14px 0;padding:10px 14px;border-left:4px solid var(--acc);background:var(--sunk);border-radius:8px}
blockquote.q p{margin:4px 0}blockquote.warning,blockquote.safety{border-color:var(--danger);background:var(--danger-bg)}blockquote.tip{border-color:var(--amber);background:var(--amber-bg)}
.pn{display:flex;justify-content:space-between;gap:12px;margin-top:40px;padding-top:16px;border-top:1px solid var(--line)}
.pn a{text-decoration:none;font-weight:600}
.top{display:none}
@media (max-width:900px){.wrap{grid-template-columns:1fr;padding:10px}aside{position:static;max-height:none}aside nav{display:none}aside.open nav{display:block}
 .top{display:inline-block;margin-left:auto;border:1px solid var(--line);background:var(--sunk);color:var(--text);border-radius:8px;padding:4px 10px;font:inherit}
 .bar{display:flex;align-items:center}main{padding:18px 16px 28px}h1{font-size:28px}}
"""

TPL = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{title} · TempCtl Duo wiki</title><link rel="icon" href="/favicon.svg"><style>{css}</style></head>
<body><div class="wrap"><aside id="side"><div class="bar"><a class="brand" href="index.html"><img src="/favicon.svg" alt=""><span><b>TempCtl Duo</b><small>Wiki · firmware {ver}</small></span></a>
<button class="top" onclick="document.getElementById('side').classList.toggle('open')">Pages</button></div>
<nav>{nav}<div class="ext"><a href="/">Open the app (connect · downloads)</a></div></nav></aside>
<main>{body}<div class="pn">{prev}{next}</div></main></div></body></html>
"""


def sidebar():
    """[(group or None, title, page)] from _Sidebar.md: '### Group' lines and '- [Title](Page.md)' items"""
    out, grp = [], None
    for l in open(os.path.join(WIKI, "_Sidebar.md"), encoding="utf-8"):
        m = re.match(r"^#+\s+(.*)", l.strip())
        if m: grp = m.group(1); continue
        m = re.match(r"^\s*[-*]\s+\[([^\]]+)\]\(([A-Za-z0-9_\-]+)\.md\)", l)
        if m: out.append((grp, m.group(1), m.group(2)))
    return out


def build(out):
    ver = re.search(r'#define FW_VERSION "([^"]+)"', open(os.path.join(ROOT, "src", "version.h")).read()).group(1)
    dst = os.path.join(out, "wiki")
    if os.path.isdir(dst): shutil.rmtree(dst)
    shutil.copytree(os.path.join(WIKI, "img"), os.path.join(dst, "img"))
    nav = sidebar()
    pages = [p for _, _, p in nav]
    for f in sorted(os.listdir(WIKI)):
        if f.endswith(".md") and not f.startswith("_") and f[:-3] not in pages: pages.append(f[:-3])
    for name in pages:
        md = open(os.path.join(WIKI, name + ".md"), encoding="utf-8").read()
        body, toc = convert(md)
        m = re.search(r"^#\s+(.*)$", md, re.M)
        title = re.sub(r"<[^>]+>", "", m.group(1)) if m else name
        n, last = [], None
        for grp, t, p in nav:
            if grp != last and grp: n.append(f'<div class="grp">{html.escape(grp)}</div>')
            last = grp
            on = p == name
            n.append(f'<a href="{page_url(p)}"' + (' class="on" aria-current="page"' if on else "") + f">{html.escape(t)}</a>")
            if on and toc: n.append('<div class="sub">' + "".join(f'<a href="#{s}">{html.escape(t2)}</a>' for s, t2 in toc) + "</div>")
        k = [p for _, _, p in nav].index(name) if name in [p for _, _, p in nav] else -1
        prev = nxt = "<span></span>"
        if k > 0: prev = f'<a href="{page_url(nav[k - 1][2])}">← {html.escape(nav[k - 1][1])}</a>'
        if 0 <= k < len(nav) - 1: nxt = f'<a href="{page_url(nav[k + 1][2])}">{html.escape(nav[k + 1][1])} →</a>'
        doc = TPL.format(title=html.escape(title), css=CSS, ver=ver, nav="".join(n), body=body, prev=prev, next=nxt)
        with open(os.path.join(dst, page_url(name)), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(doc)
    print("wiki_site: %d pages in %s" % (len(pages), os.path.relpath(dst, ROOT)))


if __name__ == "__main__":
    build(os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "dist")))
