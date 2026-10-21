#!/usr/bin/env python3
"""Builds the static page site (spec 7b) from web/: the same files at the same URLs as the device serves
them (tools/web_embed.py ASSETS), plus shell.html (the local app's tabs), for the local launcher
(tools/launcher) and the hosted page (Cloudflare Workers static assets, wrangler.jsonc).
  python3 tools/build_site.py [out_dir]          (default: tools/launcher/site)
  python3 tools/build_site.py dist --web         the hosted page: without the launcher's shell.html, with _headers
                                                 and the wiki (docs/wiki -> /wiki/, tools/wiki_site.py)
                                                 (the release files: tools/release.py, .github/workflows/release.yml)"""
import os, shutil, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ["WEB_EMBED_NO_BUILD"] = "1"
from web_embed import ASSETS, WEB, ROOT  # noqa: E402

# hosted page (Cloudflare static assets): headers per path. HTML and icons are checked on every visit (the
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

def build(out, web=False):
    if os.path.isdir(out):
        shutil.rmtree(out)
    for url, rel, _ctype, _gz, _imm in ASSETS:
        if url == "/nofonts":
            continue  # (the device's page without web fonts)
        dst = os.path.join(out, "index.html" if url == "/" else url.lstrip("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(os.path.join(WEB, rel), dst)
    if web:
        with open(os.path.join(out, "_headers"), "w", newline="\n") as f:
            f.write(HEADERS)
        import wiki_site  # the wiki pages (the launcher leaves them out: they would make the app 5 MB larger)
        wiki_site.build(out)
    else:  # the local app's tab window (not on the device, not hosted)
        shutil.copyfile(os.path.join(WEB, "shell.html"), os.path.join(out, "shell.html"))
    print("build_site: %s" % os.path.relpath(out, ROOT))

if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    build(os.path.abspath(args[0]) if args else os.path.join(ROOT, "tools", "launcher", "site"), "--web" in sys.argv)
