# Wings64-X

Native original-Xbox port of Pilotwings 64 (N64, US `NPWE`) built from the
n64decomp/pw64 decompilation (pinned at `1a8558f`) with nxdk and LLVM 21. It is
not an emulator: the decompiled game is compiled for the Pentium III, its
Fast3D display lists are interpreted on the CPU and drawn on the NV2A, and the
RSP audio microcode is reimplemented in C. Target: retail Xbox, stock 64 MB.

`PLAN.md` is binding (architecture §2, requirements §3, milestones §4,
maintainer decisions §7). Then `docs/handoff.md` (where the port is),
`docs/README.md` (index), `docs/decisions.md`.

## Layout

| path | what | triple |
|---|---|---|
| `external/pw64-decomp/` | the decomp at the pin, patched from `patches/` (gitignored; `tools/xbox/fetch_decomp.sh`) | game |
| `patches/` | `NNN-<reason>.patch` + `.md`, every hunk `#ifdef WINGS64_XBOX ... #else <original> #endif` | |
| `xbox/include/game/` | shadow headers, `wings64_prelude.h` (force-included into game units) | game |
| `xbox/src/game` | libultra shim, ROM/arena glue, settings, VI/pad glue | game: `i686-pc-windows-gnu -mno-ms-bitfields` |
| `xbox/src/gfx` | Fast3D interpreter, texture import | game |
| `xbox/src/hw` | kernel, pbkit, AC97, USB, NV2A back end, RSP audio HLE | nxdk `i386-pc-win32` |
| `xbox/include/xhw.h`, `xgfx.h`, `xrom.h` | the only structs crossing the triples: 32-bit scalars, floats, byte arrays | both |
| `tools/xbox` | build, xemu runner, host tests, icon, console tooling | host |

## Build and test

```sh
tools/xbox/msys/build.sh                          # Windows (MSYS2): -> build-xbox/xbe/default.xbe + default.tbn
XBOX_CFLAGS="-DXHW_AUTOPAD=1" tools/xbox/msys/build.sh   # a test build (switch table: docs/testing.md)
tools/xbox/docker/build.sh                        # Docker (wings64-x:sdk image)
tools/xbox/fetch_decomp.sh [--refresh]            # the decomp at the pin + patches/
python3 tools/xbox/test_rom_check.py              # host tests (tests/xbox/*.c); full list: docs/testing.md
python3 tools/xbox/rom_leak_check.py              # tree check: no ROM material
```

xemu (Windows, Git Bash; one slot per run, `/c/xemu/slot.sh take|give`):

```sh
MX_RUN=/c/xemu/wx-run$slot MX_XEMU=/c/xemu/xemu/xemu.exe \
MX_XISO=/c/xdev/nxdk/tools/extract-xiso/build/extract-xiso.exe \
MX_XEMU_ARGS="-config_path C:/xemu/slots/$slot/xemu.toml" MX_ROM=<your rom or none> \
  tools/xbox/xemu_run.sh <secs> '<stop regex>'     # serial log: $MX_RUN/serial.log
tools/xbox/fbdump_to_png.py $MX_RUN/serial.log out # [FBDUMP] screenshots (autopad SHOT lines)
```

## Hard rules

- **Never commit ROM material** or anything derived from it: no `.z64/.n64/.v64`,
  no `.eep`, no extracted asset, **no images of game frames** (FBDUMP PNGs and
  BMP shots stay out of the tree). `tools/xbox/rom_leak_check.py` enforces it.
  The known-good SHA-1 lives only as a constant.
- **Never edit `external/pw64-decomp/` in place.** Change `patches/` and
  re-fetch. One patch per reason with a `.md`; every hunk guarded
  `#ifdef WINGS64_XBOX`.
- **Every port-side departure from the game** is marked `DIVERGES: <reason>`.
- **Every optimisation and every risky change has a kill switch**; the default
  is the good build.
- The splash is Melee-X's `xhw_splash.c` **as is**. The settings menu is in the
  game's own style (wings64-dc patch 021).
- **64 MB is enforced** (`xhw_ramlock.c`). **Time comes from the ACPI timer and
  the vblank**, never an assumed CPU clock (`xhw_clock.c`).
- **Nothing runs on a ROM that has not passed the SHA-1 check** (`xhw_rom.c`).
- The decomp is consumed downstream under MIT at a pinned commit; **nothing is
  sent upstream** (its CONTRIBUTING.md bans AI-generated contributions and asks
  agents not to analyse it for contributions; we never contribute).
- xemu is for crashes and pictures, not timing or audio: say when a result is
  emulator-only. Report what was verified and how.
- Commit only when asked; never push to `main` without the maintainer's
  go-ahead.
