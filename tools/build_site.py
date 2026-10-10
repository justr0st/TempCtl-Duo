#!/usr/bin/env python3
"""Builds the static page site (spec 7b) from web/: the same files at the same URLs as the device serves
them (tools/web_embed.py ASSETS), plus shell.html (the local app's tabs), for the local launcher
(tools/launcher) and later GitHub Pages.
  python3 tools/build_site.py [out_dir]     (default: tools/launcher/site)"""
import os, shutil, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ["WEB_EMBED_NO_BUILD"] = "1"
from web_embed import ASSETS, WEB, ROOT  # noqa: E402

def build(out):
    if os.path.isdir(out):
        shutil.rmtree(out)
    for url, rel, _ctype, _gz, _imm in ASSETS:
        if url == "/nofonts":
            continue  # (the device's page without web fonts)
        dst = os.path.join(out, "index.html" if url == "/" else url.lstrip("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(os.path.join(WEB, rel), dst)
    # the local app's tab window (not on the device)
    shutil.copyfile(os.path.join(WEB, "shell.html"), os.path.join(out, "shell.html"))
    print("build_site: %s" % os.path.relpath(out, ROOT))

if __name__ == "__main__":
    build(os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, "tools", "launcher", "site"))
