#!/usr/bin/env python3
"""Host test of the ROM validation (PLAN.md 3.1 step 8).

Compiles tests/xbox/test_rom_check.c with xbox/src/hw/xhw_rom.c and
xhw_sha1.c (-DXROM_HOST_TEST) using the host C compiler (cc, gcc, clang, in
that order; MSYS2's clang/gcc on Windows) and runs it. Exit code is the test's.
The test builds a random image with a made-up header: no ROM data is read or
written anywhere in the repository.
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def find_cc():
    for name in ("cc", "gcc", "clang"):
        p = shutil.which(name)
        if p:
            return p
    for p in ("/c/msys64/mingw64/bin/clang", "/c/msys64/mingw64/bin/gcc",
              "C:/msys64/mingw64/bin/clang.exe", "C:/msys64/mingw64/bin/gcc.exe"):
        if os.path.exists(p):
            return p
    return None


def main():
    cc = find_cc()
    if not cc:
        print("test_rom_check: no host C compiler (cc, gcc, clang)")
        return 2
    out = os.path.join(tempfile.mkdtemp(prefix="wx_romtest_"), "test_rom_check.exe")
    cmd = [cc, "-std=c99", "-O1", "-Wall", "-Wextra", "-Wno-unused-function", "-DXROM_HOST_TEST",
           "-I", os.path.join(ROOT, "xbox", "include"),
           os.path.join(ROOT, "tests", "xbox", "test_rom_check.c"),
           os.path.join(ROOT, "xbox", "src", "hw", "xhw_rom.c"),
           os.path.join(ROOT, "xbox", "src", "hw", "xhw_sha1.c"),
           "-o", out]
    print("test_rom_check: " + " ".join(os.path.basename(c) if i == 0 else c for i, c in enumerate(cmd[:1])) + " ...")
    r = subprocess.run(cmd)
    if r.returncode:
        return r.returncode
    r = subprocess.run([out])
    shutil.rmtree(os.path.dirname(out), ignore_errors=True)
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
