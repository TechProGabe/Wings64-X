#!/usr/bin/env python3
"""Build and run tests/xbox/test_swap.c on the host: the big-endian -> host
struct swap (xbox/src/game/rom/pw64_swap.c, PW64_SWAP) on synthetic buffers.
No ROM data is read or written.

pw64_swap.c calls pw64_fatal without declaring it (the game units get it from
wings64_prelude.h, force-included); the runner force-includes the same
prototype from a temporary header so the file compiles unchanged.

Compiler: $CC, else clang or gcc from PATH, else /c/msys64/mingw64/bin/{clang,gcc}.
Run:  python3 tools/xbox/test_swap.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

PROTO = "__attribute__((noreturn)) void pw64_fatal(const char* msg, unsigned int value);\n"


def find_cc():
    for cand in (os.environ.get("CC"), "clang", "gcc", "cc",
                 "/c/msys64/mingw64/bin/clang", "/c/msys64/mingw64/bin/gcc",
                 "C:/msys64/mingw64/bin/clang.exe", "C:/msys64/mingw64/bin/gcc.exe"):
        if cand and shutil.which(cand):
            return shutil.which(cand)
    sys.exit("no host C compiler found (set CC)")


def main():
    cc = find_cc()
    with tempfile.TemporaryDirectory() as tmp:
        stub = os.path.join(tmp, "pw64_fatal_decl.h")
        with open(stub, "w", encoding="ascii") as f:
            f.write(PROTO)
        exe = os.path.join(tmp, "test_swap.exe" if os.name == "nt" else "test_swap")
        cmd = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-g0",
               "-include", stub,
               os.path.join(ROOT, "tests", "xbox", "test_swap.c"),
               os.path.join(ROOT, "xbox", "src", "game", "rom", "pw64_swap.c"),
               "-o", exe]
        r = subprocess.run(cmd)
        if r.returncode:
            sys.exit("compile failed")
        r = subprocess.run([exe])
        sys.exit(r.returncode)


if __name__ == "__main__":
    main()
