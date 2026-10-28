#!/usr/bin/env python3
"""Builds the static page site (spec 7b) from web/: the same files at the same URLs as the device serves
them (tools/web_embed.py ASSETS), plus shell.html (the local app's tabs), for the local launcher
(tools/launcher) and the hosted page (GitHub Pages, a project page under /TempCtl-Duo/: every path in the page,
the wiki and the demo is relative; the same files on Cloudflare during the transition, wrangler.jsonc).
  python3 tools/build_site.py [out_dir]          (default: tools/launcher/site)
  python3 tools/build_site.py dist --web         the hosted page: without the launcher's shell.html, with _headers
                                                 and the wiki (docs/wiki -> /wiki/, tools/wiki_site.py) and the
                                                 demo (/demo/: needs demo/out/tempctl-demo.wasm, demo/build.sh)
                                                 (the release files: tools/release.py, .github/workflows/release.yml)"""
import os, shutil, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ["WEB_EMBED_NO_BUILD"] = "1"
from web_embed import ASSETS, WEB, ROOT  # noqa: E402

# hosted page on Cloudflare (transition; GitHub Pages ignores this file): headers per path. HTML and icons are checked on every visit (the
# default); the fonts never change under their name. /dl/ = the release files (tools/release.py): the device's
# own page (http://<device>/) fetches the manifest and the image from here, so they allow any origin.
HEADERS = """/*
  X-Content-Type-Options: nosniff
  Referrer-Policy: no-referrer
  X-Frame-Options: DENY
  Permissions-Policy: serial=(self), camera=(), microphone=(), geolocation=()
/f/*
  Cache-Control: public, max-age=31536000, immutable
/dl/*
  Access-Control-Allow-Origin: *
  Cache-Control: no-cache
/wiki/img/*
  Cache-Control: public, max-age=86400
"""

DEMO = os.path.join(ROOT, "demo")

def build_demo(out):
    """/demo/: the page in demo mode (data-demo) + demo.js + the firmware's code as WebAssembly (demo/README.md)"""
    wasm = os.path.join(DEMO, "out", "tempctl-demo.wasm")
    if not os.path.isfile(wasm):
        print("build_site: no %s - the site has no demo (demo/build.sh builds it)" % os.path.relpath(wasm, ROOT))
        return False
    d = os.path.join(out, "demo")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(WEB, "index.html"), encoding="utf-8") as f:
        page = f.read()
    for a, b in (('<html lang="en">', '<html lang="en" data-demo>'), ("<title>TempCtl Duo</title>", "<title>TempCtl Duo DEMO</title>"),
                 ('<span><b>TempCtl Duo</b><small id="fw">', '<span><b>TempCtl Duo <i class="demo">DEMO</i></b><small id="fw">'),
                 ("<script>", '<script src="demo.js"></script>\n<script>')):
        if page.count(a) != 1:
            raise SystemExit("build_site: demo: %r not found once in web/index.html" % a)
        page = page.replace(a, b)
    # the page's own files (fonts, icons, licence) are relative to the site's root: one level up from /demo/
    for a, b in (("url(f/", "url(../f/"), ('href="favicon', 'href="../favicon'), ('src="favicon', 'src="../favicon'),
                 ('href="apple-touch-icon', 'href="../apple-touch-icon'), ('href="LICENSE.txt"', 'href="../LICENSE.txt"'),
                 ('href="THIRD-PARTY-NOTICES.txt"', 'href="../THIRD-PARTY-NOTICES.txt"')):
        if a not in page:
            raise SystemExit("build_site: demo: %r not found in web/index.html" % a)
        page = page.replace(a, b)
    with open(os.path.join(d, "index.html"), "w", encoding="utf-8", newline="\n") as f:
        f.write(page)
    shutil.copyfile(os.path.join(DEMO, "demo.js"), os.path.join(d, "demo.js"))
    shutil.copyfile(wasm, os.path.join(d, "tempctl-demo.wasm"))
    return True

def build(out, web=False):
    if os.path.isdir(out):
        shutil.rmtree(out)
    for url, rel, _ctype, _gz, _imm in ASSETS:
        if url == "/nofonts":
            continue  # (the device's page without web fonts)
        dst = os.path.join(out, "index.html" if url == "/" else url.lstrip("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(os.path.join(WEB, rel), dst)
    for src, name in (("LICENSE", "LICENSE.txt"), ("THIRD-PARTY-NOTICES.md", "THIRD-PARTY-NOTICES.txt")):
        shutil.copyfile(os.path.join(ROOT, src), os.path.join(out, name))  # (the licence and the notices: Downloads)
    if web:
        with open(os.path.join(out, "_headers"), "w", newline="\n") as f:
            f.write(HEADERS)
        import wiki_site  # the wiki pages (the launcher leaves them out: they would make the app 5 MB larger)
        wiki_site.build(out)
        build_demo(out)
    else:  # the local app's tab window (not on the device, not hosted)
        shutil.copyfile(os.path.join(WEB, "shell.html"), os.path.join(out, "shell.html"))
    print("build_site: %s" % os.path.relpath(out, ROOT))

if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    build(os.path.abspath(args[0]) if args else os.path.join(ROOT, "tools", "launcher", "site"), "--web" in sys.argv)
