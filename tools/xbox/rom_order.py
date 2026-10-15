#!/usr/bin/env python3
"""Convert an N64 ROM between byte orders: z64 (big-endian), v64 (16-bit
byte-swapped) and n64 (32-bit little-endian words).

    rom_order.py <in> <out> [--to z64|v64|n64]

The input's order is detected from its first four bytes (the extension is
ignored); --to defaults from the output's extension. A converter only: it
contains no ROM data. Keep ROMs outside the repository (docs/rom.md).
"""
import argparse
import sys

MAGIC = {b"\x80\x37\x12\x40": "z64", b"\x37\x80\x40\x12": "v64", b"\x40\x12\x37\x80": "n64"}


def to_z64(data, order):
    b = bytearray(data)
    if order == "v64":
        b[0::2], b[1::2] = data[1::2], data[0::2]
    elif order == "n64":
        for k in range(4):
            b[k::4] = data[3 - k::4]
    return bytes(b)


def from_z64(data, order):
    return to_z64(data, order)   # both swaps are their own inverse


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--to", choices=["z64", "v64", "n64"])
    a = ap.parse_args()
    data = open(a.src, "rb").read()
    have = MAGIC.get(data[:4])
    if not have:
        print("rom_order: %s is not an N64 ROM (first bytes %s)" % (a.src, data[:4].hex()), file=sys.stderr)
        return 1
    want = a.to or a.dst.lower().rsplit(".", 1)[-1]
    if want not in ("z64", "v64", "n64"):
        print("rom_order: give --to z64|v64|n64 (or a .z64/.v64/.n64 output name)", file=sys.stderr)
        return 1
    if len(data) % 4:
        print("rom_order: size %d is not a multiple of 4" % len(data), file=sys.stderr)
        return 1
    out = from_z64(to_z64(data, have), want)
    open(a.dst, "wb").write(out)
    print("rom_order: %s (%s) -> %s (%s), %d bytes" % (a.src, have, a.dst, want, len(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
