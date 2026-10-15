#!/usr/bin/env python3
"""Set the dashboard's video flags in an Xbox EEPROM image (for xemu).

Usage: eeprom_video.py <eeprom.bin> [flags]   # edits the file in place: use a copy

flags: a comma list of 480p, 720p, 1080i, widescreen (default: all four), or
"none" to clear them (a 480i 4:3 dashboard). Sets the user section's video
flags (offset 0x94: 0x00010000 widescreen, 0x00020000 720p, 0x00040000 1080i,
0x00080000 480p) and its checksum (0x60, over 0x64-0xBF), the layout on
xboxdevwiki's EEPROM page. With xemu's default HDTV AV pack the game then
runs `env MX_VIDEO=720` / `1080` (docs/testing.md "Video modes in xemu").
From Melee-X's tools/xbox/eeprom_video.py, with 1080i and the flag list
(PLAN.md M7).
"""
import struct
import sys

VIDEO_FLAGS = 0x94
CHECKSUM = 0x60
FLAGS = {'widescreen': 0x00010000, '720p': 0x00020000, '1080i': 0x00040000, '480p': 0x00080000}
MASK = sum(FLAGS.values())


def section_checksum(data):
    """The EEPROM's checksum: 32-bit words summed with the carries folded in, inverted."""
    high = low = 0
    for i in range(0, len(data), 4):
        v = struct.unpack_from('<I', data, i)[0]
        s = ((high << 32) | low) + v
        high = (s >> 32) & 0xFFFFFFFF
        low = (low + v) & 0xFFFFFFFF
    return ~(high + low) & 0xFFFFFFFF


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    path = sys.argv[1]
    names = sys.argv[2] if len(sys.argv) == 3 else ','.join(FLAGS)
    want = 0
    if names != 'none':
        for n in names.split(','):
            if n not in FLAGS:
                sys.exit(f'unknown flag {n!r} (480p, 720p, 1080i, widescreen, none)')
            want |= FLAGS[n]
    b = bytearray(open(path, 'rb').read())
    if len(b) != 256:
        sys.exit(f'{path}: {len(b)} bytes, not an EEPROM image')
    if section_checksum(b[CHECKSUM + 4:0xC0]) != struct.unpack_from('<I', b, CHECKSUM)[0]:
        sys.exit(f'{path}: user section checksum is wrong already; not touching it')
    old = struct.unpack_from('<I', b, VIDEO_FLAGS)[0]
    new = (old & ~MASK & 0xFFFFFFFF) | want
    struct.pack_into('<I', b, VIDEO_FLAGS, new)
    struct.pack_into('<I', b, CHECKSUM, section_checksum(b[CHECKSUM + 4:0xC0]))
    open(path, 'wb').write(b)
    print(f'{path}: video flags {old:08x} -> {new:08x}')


if __name__ == '__main__':
    main()
