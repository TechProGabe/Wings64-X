/* macros.h - shadows the decomp's include/macros.h (xbox/include/game is first
 * on the game include path) and then makes sure __attribute__ is the
 * compiler's own.
 *
 * libultra's PRinternal/macros.h defines `__attribute__(x)` empty for
 * compilers without __GNUC__; anything that undefined __GNUC__, or a header
 * copied from IDO-era sources, would silently drop aligned(8) from the
 * matrix and DMA types (wings64-dc kb/traps.md, "Compiler / decomp"). clang
 * defines __GNUC__, so this is a guard, not a fix. */
#include_next <macros.h>

#ifdef __attribute__
#undef __attribute__
#endif
