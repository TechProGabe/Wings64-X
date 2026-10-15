#!/usr/bin/env python3
"""Fail if the tracked tree contains anything that looks like ROM material.

Walks `git ls-files` (the working-tree content of every tracked file) and
rejects:
  - files named *.z64 *.n64 *.v64 *.eep *.rom
  - any file starting with the N64 ROM header word 80 37 12 40, or its
    byte-swapped forms 37 80 40 12 (v64) and 40 12 37 80 (n64)
  - PNG / BMP / JPG images except xbox/assets/logo.png (no game frames)
  - any file containing the Pilotwings title field ("Pilot Wings64" padded to
    20 bytes, in any of the three byte orders) followed by the 0x3B game code
    "NPWE"/"NPWJ"/"NPWP" at header offsets, i.e. a ROM header
Exit 0 when clean, 1 otherwise. Run before every commit (CLAUDE.md).
"""
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BAD_EXT = {".z64", ".n64", ".v64", ".eep", ".rom"}
IMG_EXT = {".png", ".bmp", ".jpg", ".jpeg"}
ALLOWED_IMG = {"xbox/assets/logo.png"}
TITLE = b"Pilot Wings64       "   # header 0x20, 20 bytes
HEADER_WORDS = (b"\x80\x37\x12\x40", b"\x37\x80\x40\x12", b"\x40\x12\x37\x80")


def swap_v64(b):
    out = bytearray(b)
    out[0::2], out[1::2] = b[1::2], b[0::2]
    return bytes(out)


def scan(data):
    """A reason string if data holds a ROM header, else None."""
    if data[:4] in HEADER_WORDS:
        return "starts with the N64 ROM header word %s" % data[:4].hex()
    title = TITLE
    # the header is 0x40 bytes; title at 0x20, game code at 0x3B..0x3E. In v64
    # order every byte pair is swapped, in n64 order every 4 bytes reversed:
    # compute the 0x40-byte window from the title's position for each order.
    for name, conv in (("z64", lambda b: b), ("v64", swap_v64), ("n64", None)):
        if conv is None:
            # n64: reverse each 4-byte word of the 0x20..0x3F region (aligned)
            blk = b"".join(title[i:i + 4][::-1] for i in range(0, 20, 4))
        else:
            blk = conv(title)
        start = 0
        while True:
            i = data.find(blk, start)
            if i < 0:
                break
            start = i + 1
            # data[i] is the title field at 0x20: the header begins at i - 0x20
            h = i - 0x20
            if h < 0 or h + 0x40 > len(data):
                continue
            hdr = data[h:h + 0x40]
            if name == "v64":
                hdr = swap_v64(hdr)
            elif name == "n64":
                hdr = b"".join(hdr[k:k + 4][::-1] for k in range(0, 0x40, 4))
            if hdr[0x3B:0x3B + 3] == b"NPW":
                return "contains a Pilotwings 64 ROM header (%s order) at offset 0x%x" % (name, h)
    return None


def main():
    files = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True).stdout
    problems = []
    n = 0
    for rel in files.split(b"\0"):
        if not rel:
            continue
        rel = rel.decode("utf-8", "surrogateescape")
        path = os.path.join(ROOT, rel)
        if not os.path.isfile(path):
            continue   # deleted in the working tree, or a submodule
        n += 1
        ext = os.path.splitext(rel)[1].lower()
        if ext in BAD_EXT:
            problems.append((rel, "ROM/save file extension %s" % ext))
        if ext in IMG_EXT and rel.replace("\\", "/") not in ALLOWED_IMG:
            problems.append((rel, "image file (only xbox/assets/logo.png is allowed; no game frames)"))
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError as e:
            problems.append((rel, "unreadable: %s" % e))
            continue
        why = scan(data)
        if why:
            problems.append((rel, why))
    for rel, why in problems:
        print("LEAK: %s: %s" % (rel, why))
    print("rom_leak_check: %d tracked files, %d problem(s)" % (n, len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
