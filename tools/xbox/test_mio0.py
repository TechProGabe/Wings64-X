#!/usr/bin/env python3
"""Build and run tests/xbox/test_mio0.c on the host: the MIO0 decompressor
(xbox/src/game/ultra/mio0.c) against a reference MIO0 writer in the test.
Synthetic streams only; no ROM data is read or written.

Compiler: $CC, else clang or gcc from PATH, else /c/msys64/mingw64/bin/{clang,gcc}.
Run:  python3 tools/xbox/test_mio0.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


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
        exe = os.path.join(tmp, "test_mio0.exe" if os.name == "nt" else "test_mio0")
        cmd = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-g0",
               os.path.join(ROOT, "tests", "xbox", "test_mio0.c"),
               os.path.join(ROOT, "xbox", "src", "game", "ultra", "mio0.c"),
               "-I" + os.path.join(ROOT, "xbox", "src", "game", "ultra"), "-o", exe]
        r = subprocess.run(cmd)
        if r.returncode:
            sys.exit("compile failed")
        r = subprocess.run([exe])
        sys.exit(r.returncode)


if __name__ == "__main__":
    main()
