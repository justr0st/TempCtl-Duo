#!/usr/bin/env python3
"""Adds a release to the hosted page (spec 9.13): the firmware images and the desktop app go to <site>/dl/ under
names with the version, and <site>/dl/manifest.json lists them (read by the page and by the S3, src/release.h).
Run by .github/workflows/release.yml after tools/build_site.py <site> --web:
  python3 tools/release.py <site> <artifacts> [--no-app]   (--no-app: firmware only, e.g. a test release
                                                          from a PC without Go: tools/release_local.sh)
<artifacts>/fw/<env>/firmware.bin    the update image of each env (OTA, web upload, SD card)
<artifacts>/fw/<env>/full.bin        the merged image for the first USB flash (address 0x0)
<artifacts>/app/tempctl-duo-<v>.exe, -macos-arm64, -macos-x64, -linux-x64   (tools/launcher/build.sh all)
Every image must carry its env and this version (TCDUO-IMAGE|<env>|<version>|end), or the release stops."""
import datetime, hashlib, io, json, os, re, sys, tarfile, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENVS = ["s3", "cyd-1xlocal-1x485", "cyd-2xlocal", "cyd-2xlocal-2x485"]
CHIP = {"s3": 9}  # ESP image header chip id (byte 12): ESP32-S3 9, ESP32 0


def version():
    m = re.search(r'#define FW_VERSION "([^"]+)"', open(os.path.join(ROOT, "src", "version.h")).read())
    return m.group(1)


def digest(b):
    return {"size": len(b), "md5": hashlib.md5(b).hexdigest(), "sha256": hashlib.sha256(b).hexdigest()}


def check_image(b, env, v):
    if len(b) < 65536 or b[0] != 0xE9:
        raise SystemExit("release: %s firmware.bin is not an ESP32 image" % env)
    if b[12] != CHIP.get(env, 0):
        raise SystemExit("release: %s firmware.bin is for another chip (%d)" % (env, b[12]))
    ids = set(re.findall(rb"TCDUO-IMAGE\|([^|\0]+)\|([^|\0]+)\|end", b))
    if (env.encode(), v.encode()) not in ids:
        raise SystemExit("release: %s firmware.bin carries %s, not %s %s" % (env, sorted(ids), env, v))


def main(site, art, no_app=False):
    v = version()
    dl = os.path.join(site, "dl")
    os.makedirs(dl, exist_ok=True)
    put = lambda name, data: open(os.path.join(dl, name), "wb").write(data)
    man = {"version": v,
           "date": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
           "commit": os.environ.get("GITHUB_SHA", "")[:7], "firmware": {}, "app": []}
    for env in ENVS:
        img = open(os.path.join(art, "fw", env, "firmware.bin"), "rb").read()
        check_image(img, env, v)
        name = "tempctl-duo-%s-%s.bin" % (v, env)
        put(name, img)
        e = dict(url="/dl/" + name, **digest(img))
        full = os.path.join(art, "fw", env, "full.bin")
        if os.path.exists(full):
            fb = open(full, "rb").read()
            fname = "tempctl-duo-%s-%s-full.bin" % (v, env)
            put(fname, fb)
            e["full"] = dict(url="/dl/" + fname, **digest(fb))
        man["firmware"][env] = e
    # desktop app: Windows as is; macOS zipped and Linux tarred (they keep the "executable" mark)
    app = os.path.join(art, "app")
    stamp = (2026, 1, 1, 0, 0, 0)
    apps = [] if no_app else [("windows", "x64", "tempctl-duo-%s.exe" % v), ("macos", "arm64", "tempctl-duo-%s-macos-arm64" % v),
                              ("macos", "x64", "tempctl-duo-%s-macos-x64" % v), ("linux", "x64", "tempctl-duo-%s-linux-x64" % v)]
    for os_, arch, src in apps:
        p = os.path.join(app, src)
        if not os.path.exists(p):
            raise SystemExit("release: desktop app missing: " + src)
        b = open(p, "rb").read()
        if os_ == "windows":
            name, data = "tempctl-duo-%s-windows-x64.exe" % v, b
        elif os_ == "macos":
            name = "tempctl-duo-%s-macos-%s.zip" % (v, arch)
            buf = io.BytesIO()
            with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
                zi = zipfile.ZipInfo("TempCtl Duo", stamp)
                zi.external_attr = (0o100755 << 16)
                zi.create_system = 3  # (unix: the mode above counts)
                z.writestr(zi, b, zipfile.ZIP_DEFLATED)
            data = buf.getvalue()
        else:
            name = "tempctl-duo-%s-linux-x64.tar.gz" % v
            buf = io.BytesIO()
            with tarfile.open(fileobj=buf, mode="w:gz") as t:
                ti = tarfile.TarInfo("tempctl-duo")
                ti.size, ti.mode, ti.mtime = len(b), 0o755, 1767225600
                t.addfile(ti, io.BytesIO(b))
            data = buf.getvalue()
        put(name, data)
        man["app"].append(dict(os=os_, arch=arch, url="/dl/" + name, **digest(data)))
    with open(os.path.join(dl, "manifest.json"), "w", newline="\n") as f:
        json.dump(man, f, indent=1)
    total = sum(os.path.getsize(os.path.join(dl, n)) for n in os.listdir(dl))
    print("release %s: %d files in %s (%.1f MB)" % (v, len(os.listdir(dl)), os.path.relpath(dl, ROOT), total / 1048576))


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        raise SystemExit(__doc__)
    main(os.path.abspath(args[0]), os.path.abspath(args[1]), "--no-app" in sys.argv)
