#!/usr/bin/env python3
"""Host tests for the RSP audio HLE (xbox/src/hw/asp_hle.c, PLAN.md D6/M6).

Builds tests/xbox/test_asp_hle.c + asp_ref.c + asp_hle.c with the host C
compiler in several configurations, runs each, and requires:
  - every build passes its tests against the reference (tests/xbox/asp_ref.c);
  - the MMX build (default), the plain-C build (-DASP_SCALAR), the ENVMIXER
    fast path off, -O0 and, when a 32-bit x86 compiler is around, 32-bit
    -march=pentium3 builds (the console's ISA, a second compiler) all print
    the same workload hash (PLAN.md D13: the SIMD path is the same bits as
    the C);
  - the per-tap resampler variant agrees with itself scalar and MMX.
Then prints a host benchmark of the MMX and scalar builds (not P3 numbers).

    python3 tools/xbox/test_asp_hle.py [--cc CC] [--cc32 CC32] [--bench N] [--keep]

CC defaults to $CC, then clang/gcc on PATH, then MSYS2's mingw64 clang; CC32
to i686-w64-mingw32-gcc on PATH, then MSYS2's mingw32 gcc.
No ROM is needed or read: all test data is synthetic.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SRCS = [
    os.path.join(ROOT, "tests", "xbox", "test_asp_hle.c"),
    os.path.join(ROOT, "tests", "xbox", "asp_ref.c"),
    os.path.join(ROOT, "xbox", "src", "hw", "asp_hle.c"),
]
WARN = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function"]

# name, extra flags, hash group
BUILDS = [
    ("mmx", ["-O2"], "main"),
    ("scalar", ["-O2", "-DASP_SCALAR"], "main"),
    ("mmx-nofast", ["-O2", "-DASP_HLE_ENV_FASTPATH=0"], "main"),
    ("scalar-nofast", ["-O2", "-DASP_SCALAR", "-DASP_HLE_ENV_FASTPATH=0"], "main"),
    ("mmx-O0", ["-O0"], "main"),
    ("pertap", ["-O2", "-DASP_HLE_RESAMPLE_PER_TAP=1"], "pertap"),
    ("pertap-scalar", ["-O2", "-DASP_HLE_RESAMPLE_PER_TAP=1", "-DASP_SCALAR"], "pertap"),
]
# the console's ISA, 32-bit, with a second compiler (gcc): optional, needs an i686 toolchain
BUILDS_32 = [
    ("i686-p3-mmx", ["-O2", "-march=pentium3"], "main"),
    ("i686-p3-scalar", ["-O2", "-march=pentium3", "-DASP_SCALAR"], "main"),
]


def find_cc32(arg):
    if arg:
        return arg
    for c in ("i686-w64-mingw32-gcc", "i686-linux-gnu-gcc"):
        p = shutil.which(c)
        if p:
            return p
    for p in ("C:/msys64/mingw32/bin/gcc.exe", "/c/msys64/mingw32/bin/gcc", "/mingw32/bin/gcc"):
        if os.path.exists(p):
            return p
    return None


def find_cc(arg):
    if arg:
        return arg
    if os.environ.get("CC"):
        return os.environ["CC"]
    for c in ("clang", "gcc"):
        p = shutil.which(c)
        if p:
            return p
    for p in ("C:/msys64/mingw64/bin/clang.exe", "/c/msys64/mingw64/bin/clang", "/mingw64/bin/clang"):
        if os.path.exists(p):
            return p
    sys.exit("no C compiler found (set CC)")


def cc_env(cc):
    env = dict(os.environ)
    d = os.path.dirname(cc)
    if d:  # MSYS2's compilers need their own bin/ on PATH for their DLLs
        env["PATH"] = d + os.pathsep + env.get("PATH", "")
    return env


# The console's own object (tools/xbox/msys/build.sh: nxdk clang, i386-pc-win32,
# -O2 -march=pentium3; it only imports memcpy/memset), linked into the 32-bit
# test when it is newer than the source: the exact code the XBE runs.
NXDK_OBJ = os.path.join(ROOT, "build-xbox", "CMakeFiles", "wx_hw.dir", "src", "hw", "asp_hle.c.obj")


def build(cc, name, flags, out_dir, srcs=None):
    exe = os.path.join(out_dir, "test_asp_hle_" + name + (".exe" if os.name == "nt" else ""))
    cmd = [cc] + WARN + flags + ["-o", exe] + (srcs or SRCS) + ["-lm"]
    r = subprocess.run(cmd, capture_output=True, text=True, env=cc_env(cc))
    if r.returncode:
        return None, (r.stdout + r.stderr).strip()
    return exe, ""


def run(cc, exe, args=()):
    r = subprocess.run([exe] + list(args), capture_output=True, text=True, env=cc_env(cc))
    return r.returncode, r.stdout + r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cc")
    ap.add_argument("--cc32", help="32-bit x86 compiler (default: MSYS2 mingw32 gcc if present)")
    ap.add_argument("--bench", type=int, default=300, help="frames per benchmark run (0: skip)")
    ap.add_argument("--keep", action="store_true", help="keep the build directory")
    a = ap.parse_args()
    cc = find_cc(a.cc)
    cc32 = find_cc32(a.cc32)
    out_dir = tempfile.mkdtemp(prefix="asp_hle_")
    hashes, failed, exes = {}, [], {}
    plan = [(cc, b, None) for b in BUILDS]
    if cc32:
        plan += [(cc32, b, None) for b in BUILDS_32]
        if os.path.exists(NXDK_OBJ) and os.path.getmtime(NXDK_OBJ) >= os.path.getmtime(SRCS[2]):
            plan.append((cc32, ("nxdk-object", ["-O2", "-march=pentium3"], "main"), SRCS[:2] + [NXDK_OBJ]))
        else:
            print("%-16s skipped (no up-to-date build-xbox object: run tools/xbox/msys/build.sh)" % "nxdk-object")
    else:
        print("%-16s skipped (no 32-bit x86 compiler; --cc32)" % "i686-p3")
    try:
        for comp, (name, flags, group), srcs in plan:
            exe, err = build(comp, name, flags, out_dir, srcs)
            if not exe:
                print("%-16s BUILD FAILED (%s)\n%s" % (name, comp, err))
                failed.append(name)
                continue
            rc, out = run(comp, exe)
            h = [l.split()[1] for l in out.splitlines() if l.startswith("hash ")]
            summary = [l for l in out.splitlines() if l.startswith(("ok:", "FAIL"))]
            print("%-16s %s  %s  [%s]" % (name, h[0] if h else "--------",
                                          summary[-1] if summary else out.strip()[-200:], os.path.basename(comp)))
            if rc:
                print(out)
                failed.append(name)
            if h:
                hashes.setdefault(group, {})[name] = h[0]
            exes[name] = (comp, exe)
        for group, hs in hashes.items():
            if len(set(hs.values())) != 1:
                print("hash mismatch in group %s: %s" % (group, hs))
                failed.append("hash-" + group)
        if a.bench and not failed:
            for name in ("mmx", "scalar", "i686-p3-mmx", "i686-p3-scalar", "nxdk-object"):
                if name in exes:
                    rc, out = run(exes[name][0], exes[name][1], ["--bench", str(a.bench)])
                    print("%-16s %s" % (name, out.strip()))
    finally:
        if a.keep:
            print("builds kept in", out_dir)
        else:
            shutil.rmtree(out_dir, ignore_errors=True)
    if failed:
        print("FAILED:", ", ".join(failed))
        return 1
    print("all builds pass; SIMD and scalar outputs identical")
    return 0


if __name__ == "__main__":
    sys.exit(main())
