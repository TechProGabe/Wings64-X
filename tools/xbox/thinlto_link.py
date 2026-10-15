#!/usr/bin/env python3
"""ThinLTO for the game triple, distributed by hand (PLAN.md D3; Melee-X's
tools/xbox/thinlto_link.py without the .text$<name> renaming, which only
served Melee-X's /order file: Wings64-X has none yet).

xbox/CMakeLists.txt puts this in front of the link command when XBOX_LTO is
on (RULE_LAUNCH_LINK): `thinlto_link.py <link command...>`.

1. The link command once more with /thinlto-index-only: lld-link reads every
   input, bitcode or not, and writes each bitcode object's index
   (<obj>.thinlto.bc: what it imports from the other modules).
2. clang's ThinLTO backend per bitcode object, in parallel, with the game's
   codegen flags, into <obj>.lto.obj.
3. The link command with the native objects in place of the bitcode ones
   (the response file rewritten next to the original).

lld-link's own ThinLTO would do the same in one step, but the per-module
backends here run with exactly the game triple's codegen flags and keep the
native objects for inspection (sym.py, objdump)."""
import concurrent.futures
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys

LLVM = pathlib.Path(os.environ.get('LLVM', '/opt/llvm21'))
# codegen flags of the game triple (compile_game.py CFLAGS; the rest is in the bitcode)
BACKEND = ['--target=i686-pc-windows-gnu', '-mno-ms-bitfields', '-march=pentium3', '-msse', '-mfpmath=sse',
           '-O2', '-ffp-contract=off', '-ffunction-sections', '-fno-auto-import', '-mno-stack-arg-probe',
           '-fno-asynchronous-unwind-tables', '-Wno-everything',
           *os.environ.get('XBOX_LTO_BACKEND', '').split()]


def native_path(p):
    """MSYS2's /c/x as C:/x for this (Windows) Python; anything else as is"""
    if os.name == 'nt' and len(p) > 2 and p[0] == '/' and p[2] == '/' and p[1].isalpha():
        return p[1].upper() + ':' + p[2:]
    return p


def native_arg(a):
    """a link argument with MSYS2 paths made native: /c/x, @/c/x, -flag:/c/x, -flag:@/c/x"""
    m = re.match(r'^(-[A-Za-z0-9_-]+:)?(@)?(/[A-Za-z]/.*)$', a)
    if os.name != 'nt' or not m:
        return a
    return (m.group(1) or '') + (m.group(2) or '') + native_path(m.group(3))


def lld_command(script):
    """nxdk-link (a shell script: `lld -flavor link <fixed options> "$@"`) as a
    command line: started from this Python through MSYS2's sh, the @response
    file would be expanded into the command line and overflow it"""
    text = pathlib.Path(native_path(script)).read_text().replace('\\\n', ' ')
    line = next(l for l in text.splitlines() if l.strip().startswith('lld '))
    return [t for t in shlex.split(line) if t != '$@']


def is_bitcode(path):
    try:
        with open(path, 'rb') as f:
            head = f.read(4)
    except OSError:
        return False
    return head in (b'BC\xc0\xde', b'\xde\xc0\x17\x0b')


def run(cmd):
    r = subprocess.run(cmd)
    if r.returncode:
        sys.exit(r.returncode)


def rsp_lines(path):
    """a response file's entries (compile_game.py writes one quoted path a line)"""
    return [l.strip().strip('"') for l in pathlib.Path(path).read_text().splitlines() if l.strip()]


def main():
    cmd = sys.argv[1:]
    if cmd and not cmd[0].lower().endswith('.exe'):
        cmd = lld_command(cmd[0]) + cmd[1:]
    cmd = [native_arg(a) for a in cmd]
    # the inputs: direct arguments and @response files
    inputs, rsp = [], {}
    for i, a in enumerate(cmd):
        if a.startswith('@') and pathlib.Path(native_path(a[1:])).is_file():
            lines = rsp_lines(native_path(a[1:]))
            rsp[i] = lines
            inputs += lines
        elif a.lower().endswith('.obj'):
            inputs.append(a)
    bitcode = [p for p in inputs if is_bitcode(native_path(p))]
    if not bitcode:
        run(cmd)
        return
    index_list = pathlib.Path(native_path(bitcode[0])).parent / 'thinlto-native.txt'
    # Which functions each module imports. Melee-X measured LLVM's default
    # (100 instructions) at +30% .text and settled on 10 for the console's
    # code cache; the same limit here until a Wings64-X profile says
    # otherwise. XBOX_LTO_INDEX replaces the option.
    index = os.environ.get('XBOX_LTO_INDEX', '-mllvm:-import-instr-limit=10').split()
    run(cmd + [f'-thinlto-index-only:{index_list}', *index])

    clang = str(LLVM / 'bin/clang')
    if not shutil.which(clang):
        clang = shutil.which('clang') or 'clang'   # MSYS2's /mingw64 is on PATH but not a path this Python reads

    def backend(p):
        p = native_path(p)
        out = p + '.lto.obj'
        idx = p + '.thinlto.bc'
        r = subprocess.run([clang, *BACKEND, '-x', 'ir', p, f'-fthinlto-index={idx}', '-c', '-o', out],
                           capture_output=True, text=True)
        return p, (r.stderr if r.returncode else None)

    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        for p, err in pool.map(backend, bitcode):
            if err:
                sys.stderr.write(f'{p}: ThinLTO backend failed\n{err}')
                sys.exit(1)
    native = {p: native_path(p) + '.lto.obj' for p in bitcode}
    final = []
    for i, a in enumerate(cmd):
        if i in rsp:
            new = pathlib.Path(native_path(a[1:]) + '.lto.txt')
            new.write_text(''.join(f'"{native.get(l, l)}"\n' for l in rsp[i]))
            final.append('@' + str(new))
        else:
            final.append(native.get(a, a))
    print(f'thinlto_link: {len(bitcode)} bitcode objects through the ThinLTO backend', flush=True)
    run(final)


if __name__ == '__main__':
    main()
