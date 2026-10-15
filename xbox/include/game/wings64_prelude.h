/* wings64_prelude.h - force-included (-include) first into every game unit:
 * the decomp, xbox/src/game and xbox/src/gfx (tools/xbox/compile_game.py).
 *
 * From wings64-dc's src/dc/decomp/wings64_prelude.h (the port switch, the
 * endian swap API, the RDRAM arena, pw64_fatal) plus what the game triple
 * over nxdk's pdclib needs (Melee-X's xbox_game_prelude.h). It runs ahead of
 * <ultra64.h>, so it declares rather than includes wherever it can: anything
 * here changes every game object. The Xbox is ILP32 and little-endian like
 * the Dreamcast, so the DC patches' pointer-width and byte-order assumptions
 * hold unchanged. */
#ifndef WINGS64_PRELUDE_H
#define WINGS64_PRELUDE_H

/* The guard every patch hunk tests (patches/README.md). */
#ifndef WINGS64_XBOX
#define WINGS64_XBOX 1
#endif

/* nxdk's libc headers are written for its i386-pc-win32 (MSVC) triple, where
 * __int64 is a keyword. The game triple is i686-pc-windows-gnu. */
#define __int64 long long

/* ultratypes.h only typedefs size_t for a known MIPS long width; the game
 * triple is ILP32 as the VR4300 build (unsigned int, as clang's and pdclib's
 * size_t, so the typedefs agree). */
#ifndef _MIPS_SZLONG
#define _MIPS_SZLONG 32
#define _MIPS_SZINT 32
#define _MIPS_SZPTR 32
#endif

/* ---- endianness (PLAN.md D2) ----
 * ROM data is big-endian; scalars read through uvMemRead/uvConsumeBytes are
 * converted already. Structs copied raw are swapped in place right after the
 * copy: PW64_SWAP(p, count, T, layout) swaps `count` elements of sizeof(T) at
 * p, each described by `layout` (`[N]c`, c = b keep / h 2-byte / w 4-byte /
 * d 8-byte). xbox/src/game/rom/pw64_swap.c stops the game if the layout does
 * not add up to sizeof(T). Names match Birdman64 so its hunks apply as is. */
void pw64_swap(void* p, unsigned int count, unsigned int stride, const char* layout);
#define PW64_SWAP(p, count, T, layout) pw64_swap((p), (unsigned int)(count), (unsigned int)sizeof(T), (layout))
#define PW64_SWAP16(p, count) pw64_swap((p), (unsigned int)(count), 2, "h")
#define PW64_SWAP32(p, count) pw64_swap((p), (unsigned int)(count), 4, "w")

/* ---- the N64's 4 MB memory map (patches/017, xbox/src/game/rom/rdram.c) ----
 * Every fixed KSEG0 address 0x80000000..0x80400000 the game names lives in a
 * 4 MB arena: N64_K0(0x803DA800) is the arena address of N64 0x803DA800. The
 * XBE loads at 0x10000 and the arena is an ordinary allocation, so there is
 * no address trick; every literal goes through this macro. */
extern unsigned int gWings64RdramBase;
#define N64_RDRAM_SIZE 0x400000u
#define N64_K0(a) (gWings64RdramBase + ((unsigned int)(a) - 0x80000000u))

/* ---- stopping (xbox/src/game/ultra/fatal.c) ----
 * The decomp-side "stop with a message": the patches, the ROM loaders, the
 * swap checks and every libultra entry point that is not written yet. Logs
 * WINGS64_FATAL and shows the platform's error card (xhw_fatal). */
__attribute__((noreturn)) void pw64_fatal(const char* msg, unsigned int value);

/* ---- printf family -> the port log ----
 * pdclib's printf has no %f (it skips the conversion without taking its
 * double, so every later argument is read 4 bytes off) and its stdout is a
 * dead handle on nxdk. The game's few printf-family calls go to a float-aware
 * formatter (xbox/src/game/wx_log.c, from Melee-X's log.c) and the log.
 * Declared here without <stdio.h>: pdclib's own prototypes, included later
 * by a unit, are renamed by the same macros and agree with these. wx_log.c
 * #undefs the macros to reach pdclib's own snprintf. */
int wx_printf(const char* fmt, ...);
int wx_vprintf(const char* fmt, __builtin_va_list ap);
int wx_sprintf(char* out, const char* fmt, ...);
int wx_vsprintf(char* out, const char* fmt, __builtin_va_list ap);
int wx_snprintf(char* out, unsigned int cap, const char* fmt, ...);
int wx_vsnprintf(char* out, unsigned int cap, const char* fmt, __builtin_va_list ap);
int wx_puts(const char* s);
#define printf wx_printf
#define vprintf wx_vprintf
#define sprintf wx_sprintf
#define vsprintf wx_vsprintf
#define snprintf wx_snprintf
#define vsnprintf wx_vsnprintf
#define puts wx_puts

#endif
