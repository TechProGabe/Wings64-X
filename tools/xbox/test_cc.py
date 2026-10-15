#!/usr/bin/env python3
"""Build and run tests/xbox/test_cc.c on the host: the N64 combiner -> NV2A
register combiner compiler (xbox/src/hw/nv2a_cc.c) against a software model of
the RDP's combiner (tests/xbox/cc_ref.c) and a model of the register combiners.
Usage: test_cc.py [random words, default 2000] [inputs per word, default 200]
CC selects the host compiler (default: cc, else clang, else gcc)."""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def host_cc():
    if os.environ.get("CC"):
        return os.environ["CC"]
    for c in ("cc", "clang", "gcc"):
        if shutil.which(c):
            return c
    return "cc"


def main():
    cc = host_cc()
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "test_cc.exe" if os.name == "nt" else "test_cc")
        cmd = [cc, "-O1", "-Wall", "-Wextra", "-Werror", "-o", exe,
               os.path.join(ROOT, "tests/xbox/test_cc.c"),
               os.path.join(ROOT, "tests/xbox/cc_ref.c"),
               os.path.join(ROOT, "xbox/src/hw/nv2a_cc.c"),
               "-I" + os.path.join(ROOT, "xbox/src/hw"), "-I" + os.path.join(ROOT, "tests/xbox"), "-lm"]
        r = subprocess.run(cmd)
        if r.returncode:
            return r.returncode
        return subprocess.run([exe] + sys.argv[1:]).returncode


if __name__ == "__main__":
    sys.exit(main())
