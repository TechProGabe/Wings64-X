#!/usr/bin/env python3
"""Deploy a build to the console and pull its logs over FTP (any OS).

    tools/xbox/console.py stage v33            # build-xbox -> hw/stage-v33 + hw/wings64_x.v33.map
    tools/xbox/console.py deploy v33           # upload hw/stage-v33, verify (deletes old logs/shots first)
    MX_ROM=<your rom> tools/xbox/console.py deploy v33   # also put your ROM next to default.xbe (once)
    tools/xbox/console.py pull v33             # logs + shots (+ prof.bin) -> hw/logs33
    tools/xbox/console.py ls                   # what's in the log folder

hw/ is ~/xemu/hw unless MX_HW is set. The console's FTP server is at
MX_FTP_HOST (its IP, shown by the dashboard), login xbox/xbox unless
MX_FTP_USER / MX_FTP_PASS say otherwise.
Deploy puts default.xbe and default.tbn in /F/Applications/Wings64-X/ (the
ROM goes next to them: D: is that folder), and TitleImage.xbx and
TitleMeta.xbx in /E/UDATA/57580001/ (UnleashX's icon cache). Logs
(boot*.log, lastexit.txt, crash.log, hang.log, shotNN.bmp) are in
/E/UDATA/57580001/. The ROM is the user's own and only ever goes to the
console: never into the tree. Keep each deployed build's map: sym.py and prof_report.py
need the map of the build that wrote the log.
"""
import ftplib
import io
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HW = Path(os.environ.get("MX_HW", Path.home() / "xemu" / "hw"))
HOST = os.environ.get("MX_FTP_HOST", "")
USER = os.environ.get("MX_FTP_USER", "xbox")
PASS = os.environ.get("MX_FTP_PASS", "xbox")
APP = os.environ.get("MX_APP", "/F/Applications/Wings64-X")
UDATA = "/E/UDATA/57580001"
LOGS = re.compile(r"^(shot\d+\.bmp|boot(\d|_prev)?\.log|lastexit\.txt|trace\.log|crash\.log|hang\.log|prof\.bin)$")
FILES = {"default.xbe": APP, "default.tbn": APP, "TitleImage.xbx": UDATA, "TitleMeta.xbx": UDATA}


def ver(v):
    return v if v.startswith("v") else "v" + v


def connect():
    if not HOST:
        sys.exit("set MX_FTP_HOST to the Xbox's IP address (the dashboard shows it)")
    try:
        f = ftplib.FTP(HOST, timeout=15)
    except OSError as e:
        sys.exit(f"console not reachable at {HOST} ({e}): is the Xbox on, with its FTP server up?")
    f.login(USER, PASS)
    return f


def logs(f):
    # the console's FTP server lists the current directory whatever path NLST is given
    f.cwd(UDATA)
    return sorted(n for n in (p.rsplit("/", 1)[-1] for p in f.nlst()) if LOGS.match(n))


def stage(v):
    v = ver(v)
    out = HW / f"stage-{v}"
    out.mkdir(parents=True, exist_ok=True)
    for name in FILES:
        shutil.copy2(ROOT / "build-xbox" / "xbe" / name, out / name)
    shutil.copy2(ROOT / "build-xbox" / "wings64_x.map", HW / f"wings64_x.{v}.map")
    print(f"staged {out} and wings64_x.{v}.map")


def mkdirs(f, path):
    cur = ""
    for part in path.strip("/").split("/"):
        cur += "/" + part
        try:
            f.mkd(cur)
        except ftplib.error_perm:
            pass   # already there (or a drive root)


def deploy(v):
    src = HW / f"stage-{ver(v)}"
    f = connect()
    mkdirs(f, APP)
    mkdirs(f, UDATA)
    old = logs(f)
    for n in old:
        f.delete(f"{UDATA}/{n}")
    print(f"deleted {len(old)} old logs/shots (pull them first if you need them)")
    for name, d in FILES.items():
        data = (src / name).read_bytes()
        f.storbinary(f"STOR {d}/{name}", io.BytesIO(data))
        back = io.BytesIO()
        f.retrbinary(f"RETR {d}/{name}", back.write)
        if back.getvalue() != data:
            sys.exit(f"{name} MISMATCH after upload")
        print(f"{name} ok ({len(data)} bytes)")
    rom = os.environ.get("MX_ROM")
    if rom:
        rom = Path(rom)
        name = rom.name
        try:
            have = f.size(f"{APP}/{name}")
        except ftplib.error_perm:
            have = None
        if have == rom.stat().st_size:
            print(f"{name} already on the console")
        else:
            with open(rom, "rb") as fh:
                f.storbinary(f"STOR {APP}/{name}", fh)
            print(f"{name} uploaded ({rom.stat().st_size} bytes; the XBE checks its SHA-1 at boot)")
    f.quit()


def pull(v):
    out = HW / f"logs{ver(v)[1:]}"
    out.mkdir(parents=True, exist_ok=True)
    f = connect()
    names = logs(f)
    for n in names:
        with open(out / n, "wb") as fh:
            f.retrbinary(f"RETR {UDATA}/{n}", fh.write)
        print(f"got {n} ({(out / n).stat().st_size} bytes)")
    f.quit()
    print(f"{len(names)} files in {out}")


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("stage", "deploy", "pull", "ls"):
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == "ls":
        f = connect()
        print("\n".join(logs(f)) or "(no logs)")
        f.quit()
        return
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    {"stage": stage, "deploy": deploy, "pull": pull}[cmd](sys.argv[2])


if __name__ == "__main__":
    main()
