#!/usr/bin/env python3
"""Zip a release build for the Releases page.

    tools/xbox/package_release.py <tag>      # e.g. v1.0 -> dist/Wings64-X-v1.0.zip

From build-xbox/ (build first with XBOX_LTO=1 and no XBOX_CFLAGS: a release has
no autopad, profiler or other test switch). The zip holds what README.md
describes:

    Wings64-X/default.xbe     the game
    Wings64-X/default.tbn     dashboard icon (XBMC-style dashboards)
    README.md, LICENSE.md
    tools/make-xiso           packs default.xbe and your own ROM into a burnable ISO

No ROM is ever put in the zip: the ROM is the user's own (docs/rom.md). The
link map (build-xbox/wings64_x.map) is attached to the release by the workflow,
not zipped.

The script refuses a build made with XBOX_CFLAGS (test switches) and a build
made without ThinLTO (XBOX_LTO=0), or without the game (WX_GAME=OFF). It reads
the choices from build-xbox/CMakeCache.txt, which build/xbox/build.sh rewrites
on every run (xbox/build.sh).
"""
import pathlib
import re
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
ROM_EXTENSIONS = ('.z64', '.n64', '.v64', '.rom', '.bin', '.eep')
TAG_RE = re.compile(r'^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$')


def fail(msg):
    sys.exit(f'package_release: {msg}')


def cached(cache, name):
    """The value of a CMakeCache entry (`NAME:TYPE=value`), '' if absent."""
    m = re.search(rb'^' + re.escape(name.encode()) + rb'=(.*)$', cache, re.MULTILINE)
    return m.group(1).strip() if m else b''


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    tag = sys.argv[1]
    if not TAG_RE.match(tag):
        fail(f'bad tag {tag!r}: letters, digits, dots, dashes and underscores')

    build = ROOT / 'build-xbox'
    xbe = build / 'xbe'
    for need in ('default.xbe', 'default.tbn'):
        if not (xbe / need).exists():
            fail(f'no {xbe / need}: build first (tools/xbox/msys/build.sh or tools/xbox/docker/build.sh)')
    cache_path = build / 'CMakeCache.txt'
    if not cache_path.exists():
        fail('no build-xbox/CMakeCache.txt: build first')
    cache = cache_path.read_bytes()

    if cached(cache, 'CMAKE_C_FLAGS:STRING'):
        fail('this build has XBOX_CFLAGS set (a test build): rebuild without them for a release')
    if cached(cache, 'XBOX_LTO:BOOL') != b'ON':
        fail('this build is not ThinLTO (XBOX_LTO=0): a release is built with XBOX_LTO=1 (PLAN.md D3)')
    if cached(cache, 'WX_GAME:BOOL') != b'ON':
        fail('this build has no game linked (WX_GAME=OFF): not a release')

    out = ROOT / 'dist' / f'Wings64-X-{tag}.zip'
    out.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(xbe / 'default.xbe', 'Wings64-X/default.xbe')
        z.write(xbe / 'default.tbn', 'Wings64-X/default.tbn')
        info = zipfile.ZipInfo('tools/make-xiso')
        info.external_attr = 0o100755 << 16   # executable when unzipped on Linux/macOS
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, (ROOT / 'tools' / 'make-xiso').read_bytes())
        z.write(ROOT / 'README.md', 'README.md')
        z.write(ROOT / 'LICENSE.md', 'LICENSE.md')
    with zipfile.ZipFile(out) as z:
        names = z.namelist()
    if any(n.lower().endswith(ROM_EXTENSIONS) for n in names):
        out.unlink()
        fail('a ROM-like file would be in the zip: refusing')
    print(f'wrote {out} ({out.stat().st_size // 1024} KB): {", ".join(names)}')


if __name__ == '__main__':
    main()
