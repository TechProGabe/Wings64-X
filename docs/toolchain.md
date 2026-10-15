# Toolchain

Two compile setups, one link (PLAN.md D3): the platform layer
(`xbox/src/hw`) with nxdk's own `i386-pc-win32` triple through CMake, and the
game side (the patched decomp, `xbox/src/game`, later `xbox/src/gfx`) with
the **game triple** through `tools/xbox/compile_game.py`. lld-link (nxdk's
`nxdk-link`) links both into `wings64_x.exe`, `cxbe` makes `default.xbe`.

Pins: nxdk `58427c078b4ccb0121f359b6b6c536b3b2914976` (Melee-X's), LLVM
21.1.8, the decomp n64decomp/pw64 `1a8558f5ceb320cce254d6da0a49a1f2bbc23e21`.

## Windows (this machine): MSYS2

```sh
tools/xbox/fetch_decomp.sh             # once, and after any change in patches/ (Git Bash)
tools/xbox/msys/build.sh               # -> build-xbox/xbe/default.xbe + default.tbn
XBOX_CFLAGS=-DXHW_AUTOPAD=1 tools/xbox/msys/build.sh    # a test build
XBOX_LTO=1 tools/xbox/msys/build.sh    # ThinLTO over the game code
```

- MSYS2 at `C:\msys64` with `pacman -S make git bison flex cmake ninja
  mingw-w64-x86_64-clang mingw-w64-x86_64-lld mingw-w64-x86_64-llvm
  mingw-w64-x86_64-python mingw-w64-x86_64-python-pillow` (MSYS2's cmake and
  ninja, not the mingw ones: those can't run nxdk's shell wrappers).
  `/mingw64/bin/clang` is LLVM 21.1.8.
- nxdk at `C:\xdev\nxdk` (`NXDK_DIR`), built with `make tools` and
  `make NXDK_ONLY=y NXDK_SDL=y NXDK_CXX=y`.
- `tools/xbox/msys/build.sh` runs from Git Bash; it hands `XBOX_CFLAGS`,
  `XBOX_LTO`, `XBOX_FORCE`, ... to an MSYS2 login shell (Git Bash's
  environment does not reach it) with `LLVM=/mingw64`, `PYTHONUTF8=1`.
- `tools/xbox/fetch_decomp.sh` needs git and GNU `patch`: Git for Windows
  ships `/usr/bin/patch`; MSYS2 needs `pacman -S patch` to run it there. It
  checks the decomp out with `core.autocrlf=false` (Git for Windows defaults
  to `true`, which would make every patch fail), and `.gitattributes` keeps
  `patches/*.patch` byte-exact.
- `compile_game.py` alone (e.g. one unit, or to see a command):

  ```sh
  NXDK_DIR=/c/xdev/nxdk LLVM=/c/msys64/mingw64 PYTHONUTF8=1 /c/msys64/mingw64/bin/python3 \
    tools/xbox/compile_game.py [--source external/pw64-decomp/src/kernel/memory.c] [--print-command <unit>]
  ```

## Docker

`tools/xbox/docker/build.sh` builds inside the SDK image (nxdk at the pin
built with LLVM 21 from apt.llvm.org, Debian; `patch` comes with
build-essential). The decomp is fetched on the host first
(`tools/xbox/fetch_decomp.sh`); the container sees the checkout at `/src`.
M0 copied the Docker files from Melee-X and they still name `melee-x:sdk`
and its `disc_lower` step: aligning them on `wings64-x:sdk` is open (handoff).

## The game triple

```
--target=i686-pc-windows-gnu -mno-ms-bitfields -march=pentium3 -msse -mfpmath=sse
-ffreestanding -nostdlibinc -isystem <nxdk>/lib/pdclib/include
-isystem <nxdk>/lib/pdclib/platform/xbox/include -isystem <nxdk>/lib/xboxrt/libc_extensions
-ffp-contract=off -fno-fast-math -fno-strict-aliasing -fwrapv -funsigned-char
-ftrivial-auto-var-init=zero -fno-auto-import -ffunction-sections -mno-stack-arg-probe
-fno-asynchronous-unwind-tables -std=gnu11 -O2
-U_WIN32 -U__MINGW32__ -U__MINGW64__ -UWIN32 -U__WIN32 -U__WIN32__ -DNXDK -D__STDC__=1
-D_LANGUAGE_C -DVERSION_US -DBUILD_VERSION=VERSION_D -D_FINALROM -DNDEBUG -DTARGET_N64
-DNON_MATCHING -DAVOID_UB -DRECOMP_BUILD -DWINGS64_XBOX
-Werror=implicit-function-declaration -Werror=implicit-int
-include xbox/include/game/wings64_prelude.h
```

Why not nxdk's `i386-pc-win32` for the game:

| | i386-pc-win32 (nxdk) | i686-pc-windows-gnu -mno-ms-bitfields |
|---|---|---|
| bit-fields of mixed types | MS layout: a new unit per type change | GCC layout, as patches 001-003 assume (checked on the Dreamcast with GCC 14) |
| `long long` / `double` in structs | 8-aligned | 8-aligned, as on the VR4300 |
| call ABI, struct return, `_name` symbols | MSVC | identical (Melee-X checked it in asm), so the two link and call each other |

The rest, flag by flag, is in docs/decisions.md ("Toolchain: the game flag
set"): single-precision SSE floats and no FMA because the VR4300 rounds that
way and the replays (`fdr.c`) depend on it; IDO's unsigned chars, wrapping
ints and loose aliasing; zeroed locals; pdclib as the libc without nxdk's
MSVC target; no `.refptr` stubs, stack probes or unwind tables.
`-fno-aggressive-loop-optimizations` (wings64-dc's GCC flag) does not exist
in clang.

Include order: `xbox/include/game` (shadow headers: `wings64_prelude.h`,
`macros.h`, `segment_symbols.h`, `pw64_endian.h`, `wings64_settings.h`,
`wings64_fbfreeze.h`), `xbox/include` (`xhw.h`), then the decomp's
`.`, `src`, `include`, `include/kernel`, `include/libultra`,
`include/libultra/PR`, then pdclib. The decomp's
`include/libultra/compiler/modern_gcc` (its own libc declarations) is not on
the path: pdclib is the libc. The prelude routes the printf family to a
float-aware formatter (`xbox/src/game/wx_log.c`; pdclib's printf has no
`%f`).

Warnings: decomp units get `-Wno-*` for the IDO-isms (int/pointer
conversions, multichar tags, missing returns); port units keep `-Wall`.
Implicit declarations and implicit int are errors everywhere: an undeclared
float function would return its value in EAX.

## compile_game.py

- Units: `external/pw64-decomp/src/{kernel,app,libultra/audio,libultra/sp}/*.c`
  (wings64-dc's list; the decomp's own `libultra/{os,io,libc,debug,vimodes}`
  and `src/rsp` are replaced by the port) and `xbox/src/game/**/*.c`,
  `xbox/src/gfx/**/*.c`, sorted case-sensitively.
- Parallel (one clang per CPU), incremental: `-MMD` depfiles; an object is
  rebuilt when its source, any header it read, the script or the flags stamp
  (`flags.txt`: the whole command) is newer. `XBOX_FORCE=1` rebuilds all.
- Output: `build-xbox/game/<path>.obj` (+ `.d`, `.log` when the compiler said
  something), `objects.txt` (the response file `xbox/CMakeLists.txt` links,
  quoted forward-slash paths, sorted), `objects.stamp` (touched when an
  object changed; the link depends on it), `report.json`.
- Environment: `XBOX_GAME_OUT` (the object folder; CMake passes
  `build-xbox/game` or `build-xbox/game-lto`), `XBOX_GAME_CFLAGS` (CMake
  passes `XBOX_CFLAGS`, so `-DWINGS64_SETTINGS_MENU=0` and the like reach
  every game unit), `XBOX_LTO=1`, `LLVM`, `NXDK_DIR`.
- A per-unit `-O0` quarantine list (`OPT_QUARANTINE`) is ready and empty.

## ThinLTO (`XBOX_LTO=1`)

`compile_game.py` emits bitcode into `build-xbox/game-lto`; CMake puts
`tools/xbox/thinlto_link.py` in front of the link (`RULE_LAUNCH_LINK`): an
index-only link decides the imports (`-import-instr-limit=10`, Melee-X's
setting), clang's ThinLTO backend compiles each module with the game's
codegen flags, and the link runs again on the native objects. Melee-X's
`.text$name` renaming is not done (it served Melee-X's `/order` file). M2:
both builds link and boot to the card; the plain build stays the default
until the game runs and both can be compared (docs/decisions.md).

## The link

`xbox/CMakeLists.txt`: `@build-xbox/game/objects.txt` plus
`-include:_wx_boot_game` (nothing calls the game in M2; that symbol
references `bootproc`, so the whole decomp must resolve), `-opt:noicf` (the
game compares function pointers), `-errorlimit:0` (a failed link lists every
undefined symbol), `-stack:1048576`, the map in `build-xbox/wings64_x.map`.
