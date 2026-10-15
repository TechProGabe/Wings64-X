# Licensing

Wings64-X is MIT licensed (section 1). It is built from code that other people
wrote under their own terms, and those terms are listed in section 2. Read the
parts that apply to what you redistribute.

## 1. The port's own code: MIT

Everything in this repository that is not listed in section 2 is the port's own
code (the Xbox layer, the tools and the documentation). It is MIT licensed:

```
MIT License

Copyright (c) 2026 Gabe Conway

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## 2. Code from other projects

| component | where | terms |
|---|---|---|
| **pw64 decompilation** (n64decomp/pw64, Copyright (c) 2026 Garrett Smith), at commit `1a8558f`, fetched at build time and patched from `patches/`. Not vendored in this repository | `external/pw64-decomp/` (gitignored) | MIT |
| **Birdman64** (NoahVl/Birdman64, Copyright (c) 2026 Birdman64 contributors): the N64 structures, byte swapping, audio loader and layout checks the port follows, and the patches derived from them | `xbox/include/game/pw64_endian.h`, `segment_symbols.h`, `wings64_prelude.h`, `xbox/src/game/rom/` (`pw64_swap.c`, `pw64_rom_structs.c`, `pw64_audio_load.c`, `layout_checks.c`), `patches/` | MIT |
| **OpenCrossing-Xbox** (GabeConway/OpenCrossing-Xbox): the Xbox audio driver, crash reporter, code repair, RAM lock and clock code, and the renderer knowledge in `nv2a.c` | `xbox/src/hw/` (for example `xhw_audio.c`, `xhw_crash.c`, `xhw_code_repair.c`, `xhw_ramlock.c`, `xhw_clock.c`) | MIT |
| **wings64-dc** (TechProGabe/wings64-dc), the Dreamcast port by the maintainer: the Fast3D interpreter and the Xbox shim, the settings menu and the patches it was the source for. Its files are the maintainer's own; they are offered here under the terms of section 1 | `xbox/src/gfx/`, `xbox/src/game/ultra/`, `xbox/src/game/settings*.c`, `patches/` | Copyright (c) 2026 Gabe Conway; MIT |
| **Melee-X** (TechProGabe/Melee-X): the splash, the video mode selection, the NV2A code and the build and release tooling that its files were carried from (each file's header says so) | `xbox/src/hw/` (for example `xhw_splash.c`, `xhw_video.c`, `nv2a.c`), `tools/xbox/` | Copyright (c) Gabe Conway; MIT, as relicensed by the author. Melee-X's Xbox code is GPL-3.0-or-later only because it builds on melee-pc, and none of melee-pc is used here |
| **`xhw_sha1.c`**: SHA-1, written for this project | `xbox/src/hw/xhw_sha1.c` | public domain |
| **nxdk** (XboxDev/nxdk): the SDK, pbkit, SDL and pdclib | not vendored; fetched and built by `tools/xbox/setup.sh` | its own licenses (see the nxdk repository) |
| **LLVM 21** | not vendored; fetched by `tools/xbox/setup.sh` | the Apache-2.0 WITH LLVM-exception license |

Where a file names more than one origin in its header, the header is the
authority. Files copied from upstream keep their original copyright notices.

## 3. Game code and game data: not licensed

Pilotwings 64 is copyright Nintendo. This repository contains no game data:
the ROM is read at runtime from a cartridge dump the user supplies, and the
decompiled C source is the decomp's, under its MIT terms above. Nothing here
grants a licence to Nintendo's game or to any ROM.

Pilotwings is a trademark of Nintendo. Xbox is a trademark of Microsoft. This
project is not affiliated with or endorsed by either company.
