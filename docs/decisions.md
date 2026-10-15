# Decisions

Decisions taken while building the port, with the reason and where they
live. PLAN.md section 2 (D1-D12) is the architecture; this file records what
the code settled within it. Newest sections at the end of each topic.

## Edits to imported code

Every change to the decomp is a patch in `patches/` (applied by
`tools/xbox/fetch_decomp.sh` in name order, `patch -p1 --fuzz=0`), wrapped
`#ifdef WINGS64_XBOX ... #else <original> #endif` so the N64 build is
byte-identical. 001-024 and 026-028 are wings64-dc's (commit `0c384bd`) with
its guard `WINGS64_DC` renamed; 025 (its in-game splash) is not taken; 029 is
Wings64-X's own. Each patch's `.md` says why, where it came from and what
changed on import.

| # | file(s) (under `src/` unless noted) | reason | guard / switch |
|---|---|---|---|
| 001 | `include/kernel/uv_filesystem.h` | IDO packs bit-fields MSB-first, GCC layout LSB-first: the u16 flag word declared in reverse | `WINGS64_XBOX` |
| 002 | `kernel/texture.c` | `setimg.cmd` bit-field read as `w0 >> 24` | `WINGS64_XBOX` |
| 003 | `app/snap.c` | EEPROM photo album: explicit to/from-IDO byte image of `Unk8033F050` | `WINGS64_XBOX` |
| 004 | `kernel/audio_manager.c` | UB: block-scope `customFxParams` used after its lifetime; made static | `WINGS64_XBOX` |
| 005 | `app/test_summary.c` | UB: `textFmtInt` writes past `sp5C[3]` | `WINGS64_XBOX` |
| 006 | `kernel/system.c` | UB: `osContInit` writes 4 `OSContStatus`, one declared | `WINGS64_XBOX` |
| 007 | `kernel/system.c` | UB: `app_entrypoint` prototype/call mismatch | `WINGS64_XBOX` |
| 008 | `libultra/audio/reverb.c` | UB: negated u32 index | `WINGS64_XBOX` |
| 009 | `libultra/audio/synthesizer.c` | UB: param pool stride uses the smaller struct | `WINGS64_XBOX` |
| 010 | `kernel/debug.c` | UB: colour loop over four arrays IDO placed back to back | `WINGS64_XBOX` |
| 011 | `kernel/memory.c` | `_uvMediaCopy`'s deliberate store to address 1 -> `pw64_fatal` | `WINGS64_XBOX` |
| 012 | `app/snap.c`, `app/title_screen.c`, `kernel/audio_seq.c` | busy-wait spins yield | `WINGS64_XBOX` |
| 013 | `app/{code_A8C30,demo,demo_attitude,level,spath,task,text_data,user_paths}.c`, `kernel/{anim,audio_seq,memory,texture}.c` | endianness: raw-copied ROM structs swapped in place (`PW64_SWAP`) | `WINGS64_XBOX` |
| 014 | `kernel/code_8170.c` | endianness: texel tint loops swap 16-bit images around the loop | `WINGS64_XBOX` |
| 015 | `kernel/audio_manager.c` | endianness: audio bank and sequence file deserialisers | `WINGS64_XBOX` |
| 016 | `kernel/font.c` | endianness: font `FRMT` words, `BITM` records | `WINGS64_XBOX` |
| 017 | `kernel/{audio_manager,debug,graphics,memory,sched,system,user_file}.c` | memory map: every fixed KSEG0 literal through `N64_K0` (the 4 MB arena) | `WINGS64_XBOX` |
| 018 | `kernel/sched.c` | the SP status register write in the RSP recovery path dropped | `WINGS64_XBOX` |
| 019 | `kernel/matrix.c` | `uvMat4CopyF2L`'s debug fault store dropped (the clamp stays) | `WINGS64_XBOX` |
| 020 | `include/kernel/uv_graphics.h` | camera arrays declared shorter than the game indexes them | `WINGS64_XBOX` |
| 021 | `app/game.c`, `app/title_screen.c`, `kernel/font.c`, `kernel/graphics.c` | Settings item on the title, frame hook, overlay font helper (`DIVERGES`) | `WINGS64_XBOX && WINGS64_SETTINGS_MENU` (default 1) |
| 022 | `kernel/audio_manager.c` | the audio command list runs on the audio thread, not through the scheduler (`DIVERGES`); **changed for the Xbox**: runs the list (`wings64_audio_task`) | `WINGS64_XBOX` |
| 023 | `kernel/system.c` | the kernel thread's idle spin blocks instead | `WINGS64_XBOX` |
| 024 | `kernel/system.c` | `_uvDebugPrintf` forwarded to the log, rate-limited | `WINGS64_XBOX` |
| 026 | `app/replay_screen.c` | screen fade over the frozen last frame (`DIVERGES`) | `WINGS64_XBOX && WINGS64_FB_FREEZE` (default 1) |
| 027 | `app/code_D2B10.c` | debug camera aimed at the landing pad | `WINGS64_XBOX && WINGS64_DBG_LOOK_AT_PAD > 0` (default off) |
| 028 | `app/snap.c` | photo album keeps a persistent frame (`DIVERGES`) | `WINGS64_XBOX && WINGS64_FB_PERSIST` (default 1) |
| 029 | `include/libultra/PR/os_convert.h`, `include/libultra/PR/R4300.h`, `kernel/geometry.c` | **Wings64-X's own**: KSEG0/physical conversions are the identity, `geometry.c:347`'s `+ 0x80000000` dropped (host pointers stay host pointers; "Memory map" below) | `WINGS64_XBOX` |

Changes made on import, beyond the guard: 022 runs the command list (the
Dreamcast's AICA played the voices, so its list was a placeholder; the Xbox
keeps libultra's synthesizer, PLAN.md D6); 021's helper renamed
`uvFontOverlayStr_port`; comments that described the SH-4, KOS or the PVR
reworded (one line for one line, so no hunk moved). The SD-card-log parts
of the Dreamcast menu were never in patch 021 (they are in its `settings.c`,
ported in M7 without them).

Not edits, but the same discipline: shadow headers in `xbox/include/game/`
(first on the game include path) replace two decomp headers without
touching them: `segment_symbols.h` (linker symbols as constants, below) and
`macros.h` (`#include_next`, then `#undef __attribute__`).

## Toolchain: the game flag set (M2)

`tools/xbox/compile_game.py` compiles the decomp and `xbox/src/game` (later
`xbox/src/gfx`) with one flag set; docs/toolchain.md explains the triple.

| flag | why |
|---|---|
| `--target=i686-pc-windows-gnu -mno-ms-bitfields -march=pentium3` | GCC bit-field layout (patches 001-003 assume it; `layout_checks.c` asserts it), nxdk's call ABI, the console's CPU |
| `-msse -mfpmath=sse -ffp-contract=off -fno-fast-math` | single-precision floats round as on the VR4300 (no x87 excess precision), no FMA (the VR4300 has none): replays (`fdr.c`) stay deterministic |
| `-fno-strict-aliasing -fwrapv -funsigned-char` | IDO semantics the decomp relies on |
| `-ftrivial-auto-var-init=zero` | uninitialised locals read as IDO left them (clang turned one into a trap: wings64-dc) |
| `-ffreestanding -nostdlibinc -isystem <nxdk pdclib>` | nxdk's libc headers without its MSVC triple (Melee-X's SDK_FLAGS) |
| `-U_WIN32 -U__MINGW32__ ...` | nothing in the game may think it runs on Windows |
| `-fno-auto-import -mno-stack-arg-probe -fno-asynchronous-unwind-tables -ffunction-sections` | static link without `.refptr` stubs, no `__chkstk`, no unwind tables, `-opt:ref` can drop dead functions |
| `-std=gnu11 -O2` | as wings64-dc |
| `-D_LANGUAGE_C -DVERSION_US -DBUILD_VERSION=VERSION_D -D_FINALROM -DNDEBUG -DTARGET_N64 -DNON_MATCHING -DAVOID_UB -DRECOMP_BUILD -DWINGS64_XBOX` | the decomp's NON_MATCHING/AVOID_UB build (`RECOMP_BUILD`: every static exported, as Birdman64 and wings64-dc build it) and the patch guard |
| `-Werror=implicit-function-declaration -Werror=implicit-int` (+ `return-mismatch`, `incompatible-function-pointer-types`) | an undeclared function returns int: a float result would be read from EAX (Melee-X's rule) |
| `-Wno-*` for decomp units only | IDO-isms that are lossless on ILP32: int/pointer conversions, multichar tags, missing returns; port units keep `-Wall` |
| `-include xbox/include/game/wings64_prelude.h` | `WINGS64_XBOX`, `PW64_SWAP`, `N64_K0`, `pw64_fatal`, the printf family renamed to the float-aware formatter |

**`-fno-aggressive-loop-optimizations` is not in the set**: it is a GCC
flag and clang 21 rejects it. wings64-dc used it against loops the compiler
shortens from IDO-era array bounds; the case that bit the Dreamcast is fixed
at the source (patches/020), and `-Wno-array-bounds` hides nothing that the
patches did not already address there. If a decomp TU misbehaves from a
loop bound, the remedy is a patch (the array's real extent), not a flag.

**`va_start` on promoted types** (`kernel/chan.c:365`, `kernel/audio_emitter.c:179`,
`kernel/code_1050.c:48`): the decomp brackets each with a clang pragma
(`-Wvarargs` ignored) and uses `__builtin_va_start` (`include/libc/stdarg.h`
when not IDO). On i386 the builtin finds the first variadic slot from the
call frame, not from `sizeof(parmN)`, so a `u16`/`f32` last parameter
(passed in a 4-byte slot) is handled correctly; `-Wvarargs` stays on for
everything else.

**`.text$name` sections**: the mingw triple names each function's section
`.text$<name>`; Melee-X renamed them for its `/order` file. Wings64-X has no
order file, so they stay (lld-link merges them into `.text`).

**ThinLTO (`XBOX_LTO=1`)**: `compile_game.py` emits bitcode into
`build-xbox/game-lto` and `tools/xbox/thinlto_link.py` (Melee-X's, minus the
section renaming) runs the per-module backends at link time. Both paths link
and boot to the card in xemu (M2). The default stays the plain build until
the game runs (M3) and can be compared both ways; PLAN.md D3 makes ThinLTO
the default after that.

## Memory map and addresses (M2; the lead's address-model decision, 2026-10-15)

The game tells RAM from ROM by bit 31: `_uvMediaCopy` and `uvMemRead`
(`kernel/memory.c:181,234`) and `uvFile` reads (`filesystem.c:89`) treat an
address with bit 31 set as RAM and anything else as a ROM offset (ROM
addresses are below 8 MB). On the N64 RAM was KSEG0 (0x80xxxxxx) and on the
Dreamcast host RAM was P1 (0x8Cxxxxxx); on the Xbox only contiguous memory
(mapped at 0x80000000 | physical) and kernel thread stacks have bit 31 set.

1. **The arena is contiguous.** `xbox/src/game/rom/rdram.c` takes the 4 MB
   from `xhw_rdram_alloc` (`xbox/src/hw/xhw_sys.c`:
   `MmAllocateContiguousMemoryEx(bytes, 0, 0x03FFFFFF, 4096, PAGE_READWRITE)`:
   cached, not write-combined, below 64 MB, zeroed), and stops with
   `pw64_fatal` if the address lacks bit 31. Every N64 KSEG0 literal goes
   through `N64_K0` into it (patches/017), so the game's own heap, scratch
   areas, frame buffers and audio heap pass the RAM test, and the tests stay
   unpatched.
2. **Address conversions are the identity** (patches/029):
   `OS_K0_TO_PHYSICAL`, `OS_PHYSICAL_TO_K0` and the K1 forms
   (`os_convert.h`), `K0_TO_PHYS`/`PHYS_TO_K0` and friends (`R4300.h`), and
   `osVirtualToPhysical` (`xbox/src/game/ultra/stubs.c`). Display lists and
   RSP task data hold plain host pointers, which the interpreters resolve as
   they are (plus the segment table): no 0x80000000 wrap that an arena
   pointer and a converted static pointer could both produce. The one
   hand-written conversion, `geometry.c:347`'s `+ 0x80000000`, is dropped.
   A patch to the headers rather than shadow headers: `rcp.h` includes
   `"R4300.h"` with quotes, which finds the original next to it before any
   shadow directory, and both headers' conversions belong in one reviewed
   place.
3. **Static data has bit 31 clear.** The XBE's `.data`/`.bss` (from 0x10000)
   reads as a ROM offset if it reaches those tests as a *source*. Sites found
   by reading every `_uvMediaCopy`/`uvMemRead` call in the built units (M3
   decides; a `WINGS64_XBOX` patch per site, or routing the PI shim's ROM
   reads by range, are the options):
   - `app/save.c`: `_uvMediaCopy(sSaveFilesMirror, &sSaveFiles, ...)` and
     `_uvMediaCopy(sSaveFilesMirror[i].raw, saveFile->raw, ...)` (a pointer
     into the static `sSaveFiles`): RAM-to-RAM copies of a static source.
   - `app/task.c`: `_uvMediaCopy(D_8035078C->dataTPAD, &sTaskTakeoffPads[mapIdx], ...)`
     (static source).
   - Stack locals as sources: `app/task.c` `_uvMediaCopy(strId, &sp20, ...)`
     and `kernel/memory.c`'s `uvMemRead((u8*)&temp2, ...)` (inside
     `uvMemRead`'s own unaligned path). These pass only if the game threads
     run on kernel-allocated stacks (bit 31 set), not on the game's own
     `STACK()` arrays in `.bss`: a constraint on M3's `osCreateThread`.
   - Destinations do not matter (only the source is tested); the ROM loaders
     (`pw64_audio_load.c`) and the static `pw64_arena` are never sources.

## Linker-script symbols (M2)

The N64 build gets `kernel_ROM_START`, `app_TEXT_START` and the other segment
symbols from its linker script. The port shadows `segment_symbols.h` with
constants (wings64-dc's, from Birdman64): ROM offsets for the ROM segments
(bit 31 clear, read from the ROM image), N64 addresses for the code segments,
which only `uvSysInit` prints. The app overlay load (`system.c:312`) becomes
a zero-length copy (`DIVERGES`: the app code is linked into the XBE). So no
segment symbol reaches the linker and no C file has to define one;
`memory.c`'s own `extern kernel_TEXT_START` and `D_803805E0` are arena
addresses (patches/017).

## libultra surface (M2)

`xbox/src/game/ultra/stubs.c` holds exactly the 42 libultra functions the
link reported undefined, each `pw64_fatal("TODO: <name>")`; M3's OS shim
replaces them. The decomp's own `src/libultra/{os,io,libc,debug,vimodes}`
and `src/rsp` are not built (wings64-dc's list): the port supplies that
layer. `src/libultra/audio` is built whole (unlike wings64-dc, which drops
the synthesizer for its AICA voices): the Xbox runs libultra's synthesizer
(PLAN.md D6).
