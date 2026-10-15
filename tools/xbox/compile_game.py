#!/usr/bin/env python3
"""Compile the game side (the patched decomp, xbox/src/game, xbox/src/gfx) to
i386 COFF objects for default.xbe, with the game triple.

From Melee-X's tools/xbox/compile_game.py without its disc-lowering and CP932
steps: Pilotwings 64's assets stay big-endian in memory and are swapped at the
enumerated sites (patches 013-016, PLAN.md D2), so every unit is one plain
`clang -c`.

The triple is i686-pc-windows-gnu -mno-ms-bitfields (PLAN.md D3,
docs/toolchain.md): GCC bit-field packing (what patches 001-003 were written
and checked for on the Dreamcast), and the same call and struct-return ABI as
nxdk's i386-pc-win32, so these objects link with nxdk's lld-link next to the
platform layer.

Objects land in build-xbox/game (build-xbox/game-lto with XBOX_LTO=1:
ThinLTO bitcode); objects.txt (a response file, sorted case-sensitively) and
objects.stamp are what xbox/CMakeLists.txt links and depends on.
  tools/xbox/compile_game.py [--jobs N] [--source external/pw64-decomp/src/kernel/x.c ...]
Environment: XBOX_GAME_OUT (object folder), XBOX_GAME_CFLAGS (extra flags,
e.g. -DXHW_AUTOPAD=1: CMake passes XBOX_CFLAGS), XBOX_LTO=1, XBOX_FORCE=1
(rebuild everything), LLVM, NXDK_DIR.
"""
import argparse
import concurrent.futures
import json
import os
import pathlib
import shlex
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def native_path(p):
    """MSYS2's /c/x as C:/x for this (Windows) Python; anything else as is"""
    p = str(p)
    if os.name == 'nt' and len(p) > 2 and p[0] == '/' and p[2] == '/' and p[1].isalpha():
        return p[1].upper() + ':' + p[2:]
    return p


def find_clang():
    llvm = os.environ.get('LLVM')
    if llvm:
        for name in ('clang', 'clang.exe'):
            c = pathlib.Path(native_path(llvm)) / 'bin' / name
            if c.exists():
                return c
    c = shutil.which('clang')
    if c:
        return pathlib.Path(c)
    for guess in ('/opt/llvm21/bin/clang', 'C:/msys64/mingw64/bin/clang.exe'):
        if pathlib.Path(guess).exists():
            return pathlib.Path(guess)
    sys.exit('compile_game: no clang (set LLVM to the LLVM 21 prefix)')


# one spelling for one compiler, so the flags stamp does not change with how it was found
CLANG = pathlib.Path(os.path.normcase(find_clang().resolve()))
NXDK = pathlib.Path(native_path(os.environ.get('NXDK_DIR', '/opt/nxdk')))
LTO = os.environ.get('XBOX_LTO', '') not in ('', '0')
# bitcode and native objects never share a folder (CMake passes game or game-lto)
_default_out = 'build-xbox/game-lto' if LTO else 'build-xbox/game'
OUT = pathlib.Path(native_path(os.environ.get('XBOX_GAME_OUT') or _default_out))
if not OUT.is_absolute():
    OUT = ROOT / OUT
DECOMP = ROOT / 'external/pw64-decomp'

TRIPLE = ['--target=i686-pc-windows-gnu', '-mno-ms-bitfields', '-march=pentium3']

# nxdk's libc headers (pdclib), minus its i386-pc-win32 target (Melee-X's
# SDK_FLAGS). -nostdlibinc keeps clang's own freestanding headers.
SYSTEM_FLAGS = [
    '-ffreestanding', '-nostdlibinc',
    f'-isystem{NXDK}/lib/pdclib/include',
    f'-isystem{NXDK}/lib/pdclib/platform/xbox/include',
    f'-isystem{NXDK}/lib/xboxrt/libc_extensions',
    '-DNXDK', '-D__STDC__=1', '-U__STDC_NO_THREADS__',
    # The mingw triple predefines these; nothing in the game may think it is
    # running on Windows.
    '-U_WIN32', '-U__MINGW32__', '-U__MINGW64__', '-UWIN32', '-U__WIN32', '-U__WIN32__',
]

# The decomp's build defines (its Makefile's NON_MATCHING/AVOID_UB build, as
# wings64-dc's src/game/Makefile) plus the port guard every patch hunk tests.
DEFINES = [
    '-D_LANGUAGE_C', '-DVERSION_US', '-DBUILD_VERSION=VERSION_D', '-D_FINALROM', '-DNDEBUG', '-DTARGET_N64',
    '-DNON_MATCHING', '-DAVOID_UB', '-DRECOMP_BUILD', '-DWINGS64_XBOX',
]

# Shadow headers first (xbox/include/game: wings64_prelude.h, macros.h,
# segment_symbols.h, ...), then the decomp's own include path as wings64-dc
# uses it, minus include/libultra/compiler/modern_gcc (pdclib is the libc).
INCLUDES = [
    'xbox/include/game', 'xbox/include',
    'external/pw64-decomp', 'external/pw64-decomp/src', 'external/pw64-decomp/include',
    'external/pw64-decomp/include/kernel', 'external/pw64-decomp/include/libultra',
    'external/pw64-decomp/include/libultra/PR',
]

# __FILE__ relative to the checkout: no builder's home directory in the XBE
PREFIX_MAP = [f'-ffile-prefix-map={ROOT.as_posix()}/=', f'-ffile-prefix-map={ROOT}{os.sep}=']

CFLAGS = [
    *TRIPLE, *SYSTEM_FLAGS, *PREFIX_MAP, '-std=gnu11', '-O2',
    # IDO semantics (PLAN.md D3, wings64-dc kb/traps.md): unsigned char,
    # wrapping ints, no aliasing assumptions, no FMA (the VR4300 has none),
    # floats in SSE registers (single precision as on the VR4300, no x87
    # excess precision), uninitialised locals zero (clang turned one into a
    # trap). GCC's -fno-aggressive-loop-optimizations (no loop shortening from
    # IDO-era array bounds) has no clang spelling: clang 21 rejects it. The
    # known case is fixed at the source (patches/020); docs/decisions.md.
    '-msse', '-mfpmath=sse', '-ffp-contract=off', '-fno-fast-math',
    '-fno-strict-aliasing', '-fwrapv', '-funsigned-char',
    '-ftrivial-auto-var-init=zero',
    # Everything links statically: data goes straight to its symbol, not
    # through a mingw .refptr stub; no stack probes (nxdk has no __chkstk for
    # this triple); no unwind tables; one section per function.
    '-fno-auto-import', '-mno-stack-arg-probe', '-fno-asynchronous-unwind-tables', '-fno-exceptions',
    '-ffunction-sections',
    *DEFINES, *[f'-I{i}' for i in INCLUDES],
    '-include', 'xbox/include/game/wings64_prelude.h',
    # An implicitly declared function returns int: a float result would be
    # read from EAX instead of st(0)/xmm0. Never silently (Melee-X's rule).
    '-Werror=implicit-function-declaration', '-Werror=implicit-int',
    '-Werror=return-mismatch', '-Werror=incompatible-function-pointer-types',
    '-Wno-builtin-macro-redefined', '-Wno-unknown-warning-option', '-ferror-limit=20',
]

# IDO-isms the decomp is full of and that are lossless on ILP32 (as on the
# VR4300): int<->pointer conversions, mismatched pointer types, multichar
# 'FORM' tags, missing returns in functions whose result is never used.
DECOMP_WARNINGS = [
    '-Wno-int-conversion', '-Wno-incompatible-pointer-types', '-Wno-pointer-sign', '-Wno-multichar',
    '-Wno-return-type', '-Wno-int-to-pointer-cast', '-Wno-pointer-to-int-cast', '-Wno-implicit-int-conversion',
    '-Wno-constant-conversion', '-Wno-tautological-compare', '-Wno-tautological-constant-out-of-range-compare',
    '-Wno-parentheses-equality', '-Wno-unused-value', '-Wno-dangling-else', '-Wno-shift-op-parentheses',
    '-Wno-bitwise-op-parentheses', '-Wno-logical-op-parentheses', '-Wno-unsequenced', '-Wno-format',
    '-Wno-main-return-type', '-Wno-non-literal-null-conversion', '-Wno-implicit-const-int-float-conversion',
    '-Wno-sometimes-uninitialized', '-Wno-uninitialized', '-Wno-array-bounds', '-Wno-unused-but-set-variable',
    '-Wno-single-bit-bitfield-constant-conversion', '-Wno-deprecated-non-prototype',
    '-Wno-strict-prototypes', '-Wno-gnu-folding-constant', '-Wno-empty-body', '-Wno-absolute-value',
    '-Wno-unknown-pragmas', '-Wno-ignored-attributes', '-Wno-visibility', '-Wno-switch',
    '-Wno-literal-conversion', '-Wno-float-conversion', '-Wno-string-plus-int',
    '-Wno-incompatible-library-redeclaration', '-Wno-builtin-requires-header', '-Wno-int-in-bool-context',
    '-Wno-unused-function', '-Wno-pointer-bool-conversion', '-Wno-self-assign',
]
# Port code (xbox/src/game, xbox/src/gfx) is ours: warnings stay on.
PORT_WARNINGS = ['-Wall', '-Wno-unused-function', '-Wno-multichar']

# Per-unit -O0 quarantine (OpenCrossing pattern; PLAN.md section 5): a unit
# clang miscompiles at -O2 goes here with its evidence in docs/decisions.md.
# Starts empty.
OPT_QUARANTINE = set()

EXTRA = shlex.split(os.environ.get('XBOX_GAME_CFLAGS', ''))
if LTO:
    EXTRA.append('-flto=thin')

# The decomp units: what wings64-dc's src/game/Makefile builds (DECOMP_DIRS).
# libultra os/io/libc/debug/vimodes and the RSP microcode are replaced by the
# port's libultra (xbox/src/game/ultra); the decomp has no src/libultra/gu.
# Unlike wings64-dc (which drops the synthesizer for its AICA voices when
# WINGS64_AUDIO is on), the whole of src/libultra/audio is built: the Xbox
# runs libultra's own synthesizer and executes its command lists (PLAN.md D6).
DECOMP_DIRS = ['src/kernel', 'src/app', 'src/libultra/audio', 'src/libultra/sp']
PORT_DIRS = ['xbox/src/game', 'xbox/src/gfx']


def posix(p):
    return p.as_posix()


def game_sources():
    # sorted case-sensitively, as on Linux (link order = code layout)
    decomp = sorted((s for d in DECOMP_DIRS for s in (DECOMP / d).glob('*.c')), key=posix)
    port = sorted((s for d in PORT_DIRS if (ROOT / d).is_dir() for s in (ROOT / d).rglob('*.c')), key=posix)
    return decomp, port


def command(source, obj, dep, is_port):
    flags = [*CFLAGS, *(PORT_WARNINGS if is_port else DECOMP_WARNINGS), *EXTRA]
    rel = posix(source.relative_to(ROOT))
    if rel in OPT_QUARANTINE:
        flags.append('-O0')
    return [str(CLANG), *flags, '-MMD', '-MF', str(dep), '-MT', str(obj), '-c', str(source), '-o', str(obj)]


def up_to_date(obj, dep, source, flags_stamp):
    """The object is newer than the source, every header it included (from
    the compiler's depfile), this script and the flags stamp."""
    if not obj.exists() or not dep.exists():
        return False
    stamp = obj.stat().st_mtime
    # ': ' ends the target (a Windows target has a drive colon of its own)
    deps = dep.read_text().replace('\\\n', ' ').split(': ', 1)[-1].split()
    for d in [str(source), __file__, str(flags_stamp), *deps]:
        path = pathlib.Path(d) if os.path.isabs(d) else ROOT / d
        try:
            if path.stat().st_mtime > stamp:
                return False
        except (FileNotFoundError, OSError):
            return False
    return True


def build(source, is_port, flags_stamp):
    rel = source.relative_to(ROOT)
    base = OUT / rel
    base.parent.mkdir(parents=True, exist_ok=True)
    obj, log, dep = base.with_suffix('.obj'), base.with_suffix('.log'), base.with_suffix('.d')
    if not os.environ.get('XBOX_FORCE') and up_to_date(obj, dep, source, flags_stamp):
        return {'source': posix(rel), 'status': 'passed', 'cached': True}
    with log.open('w') as err:
        r = subprocess.run(command(source, obj, dep, is_port), cwd=ROOT, stdout=err, stderr=err)
    if r.returncode:
        if obj.exists():
            obj.unlink()
        return {'source': posix(rel), 'status': 'failed', 'log': posix(log.relative_to(ROOT) if log.is_relative_to(ROOT) else log)}
    if log.stat().st_size == 0:
        log.unlink()
    return {'source': posix(rel), 'status': 'passed'}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--jobs', type=int, default=os.cpu_count())
    parser.add_argument('--source', action='append', help='compile only this unit (repeatable)')
    parser.add_argument('--print-command', metavar='SOURCE', help='print the compile command of one unit and exit')
    args = parser.parse_args()

    if not (DECOMP / 'src').is_dir():
        sys.exit('compile_game: no external/pw64-decomp (run tools/xbox/fetch_decomp.sh)')
    decomp, port = game_sources()
    is_port = {s: False for s in decomp} | {s: True for s in port}
    if args.print_command:
        s = (ROOT / args.print_command).resolve()
        print(shlex.join(command(s, OUT / 'x.obj', OUT / 'x.d', s not in is_port or is_port[s])))
        return
    sources = [(ROOT / s).resolve() for s in args.source] if args.source else [*decomp, *port]
    OUT.mkdir(parents=True, exist_ok=True)

    # Any change of flags (XBOX_GAME_CFLAGS, LTO, a new warning) rebuilds every unit.
    flags_stamp = OUT / 'flags.txt'
    flags_text = shlex.join([str(CLANG), *CFLAGS, '|', *DECOMP_WARNINGS, '|', *PORT_WARNINGS, '|', *EXTRA,
                             '|', *sorted(OPT_QUARANTINE)]) + '\n'
    if not flags_stamp.exists() or flags_stamp.read_text() != flags_text:
        flags_stamp.write_text(flags_text)

    if not args.source:
        expected = {OUT / s.relative_to(ROOT).with_suffix('.obj') for s in sources}
        for stale in OUT.rglob('*.obj'):
            if stale not in expected:
                stale.unlink()

    results = []
    # Threads: each job is a clang process, so the GIL is not in the way.
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for result in pool.map(lambda s: build(s, is_port.get(s, True), flags_stamp), sources):
            results.append(result)
            if result['status'] == 'failed':
                print(f"{result['source']}: FAILED (see {result['log']})", flush=True)

    report = OUT / ('report-partial.json' if args.source else 'report.json')
    report.write_text(json.dumps(results, indent=2) + '\n')
    passed = sum(r['status'] == 'passed' for r in results)
    built = sum(r['status'] == 'passed' and not r.get('cached') for r in results)
    if not args.source:
        # sorted case-sensitively: the link order (and so the code layout) is
        # the same on every host. Forward slashes, quoted: lld-link's
        # response-file syntax.
        objs = sorted(posix(OUT / pathlib.Path(r['source']).with_suffix('.obj')) for r in results
                      if r['status'] == 'passed')
        listing = ''.join(f'"{o}"\n' for o in objs)
        objects = OUT / 'objects.txt'
        changed = not objects.exists() or objects.read_text() != listing
        if changed:
            objects.write_text(listing)
        # the link depends on this stamp (xbox/CMakeLists.txt LINK_DEPENDS)
        if changed or built or not (OUT / 'objects.stamp').exists():
            (OUT / 'objects.stamp').touch()
    print(f'game: {passed} / {len(results)} units compiled ({built} rebuilt){" [ThinLTO bitcode]" if LTO else ""}')
    raise SystemExit(passed != len(results))


if __name__ == '__main__':
    main()
