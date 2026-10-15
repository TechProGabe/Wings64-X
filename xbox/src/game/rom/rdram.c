/* rdram.c - the 4 MB "RDRAM" arena that stands in for the N64's fixed memory
 * map (patches/017, PLAN.md D2): N64_K0(a) = gWings64RdramBase + (a -
 * 0x80000000). Allocated once, before bootproc. From wings64-dc's
 * src/dc/ultra/rdram.c.
 *
 * The game tells RAM from ROM by bit 31 (_uvMediaCopy and uvMemRead in
 * kernel/memory.c, filesystem.c:89): ROM addresses are small offsets, RAM
 * addresses were N64 KSEG0 (0x80xxxxxx). On the Dreamcast host RAM had bit 31
 * set; on the Xbox only contiguous memory does (0x80000000 | physical), so
 * the arena is contiguous (xhw_rdram_alloc) and those tests stay unpatched.
 * The XBE's own .data/.bss (from 0x10000) has bit 31 clear: a static object
 * used as a RAM *source* of those functions would be read as ROM
 * (docs/decisions.md, "Memory map", lists the sites).
 *
 * DIVERGES: the N64 code/data window 0x80200000..0x80380000 is reserved in
 * the arena but unused (the code lives in the XBE); ~1.5 MB to reclaim later. */
#include "xhw.h"

unsigned int gWings64RdramBase;

void wings64_rdram_init(void) {
    void* p;
    if (gWings64RdramBase) return;
    p = xhw_rdram_alloc(N64_RDRAM_SIZE);   /* zeroed, page-aligned, never freed */
    if (!p) pw64_fatal("rdram: cannot allocate the 4 MB N64 arena (contiguous)", N64_RDRAM_SIZE);
    if (!((unsigned int)p & 0x80000000u)) pw64_fatal("rdram: arena address without bit 31", (unsigned int)p);
    gWings64RdramBase = (unsigned int)p;
}
