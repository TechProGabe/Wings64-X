# 017-memory-map-arena

**Why:** The game hard-codes its 4 MB N64 memory map: heap 0x8004181C..0x80400000 with reserved blocks,
framebuffers 0x800DA800/0x80100000, Z-buffer/scratch 0x803DA800, audio heap 0x80000400 (PLAN M2,
kb/traps.md). On the Dreamcast 0x80000000 is P1 boot ROM and the MMU stays off, so every literal becomes
`N64_K0(addr)` = RDRAM arena base + (addr - 0x80000000). The arena is 4 MB allocated before `bootproc`
(xbox/src/game/rom/rdram.c). Arena addresses keep bit 31 set (the "RAM, not ROM" test in `_uvMediaCopy`
still works) and survive `K0_TO_PHYS`/`OS_PHYSICAL_TO_K0`. The `_uvMemFreeScratch` switch becomes an if
chain (arena addresses are not constant expressions); `gAudioHeapStart` becomes a macro (it was a
constant initializer). `kernel_TEXT_START`/`D_803805E0` (N64 linker addresses) become arena lvalues.
`geometry.c:347`'s `+ 0x80000000` is a K0-to-physical conversion and needs no change.

**Origin:** wings64-dc, own. Birdman64 maps the same addresses onto a reserved host window instead.

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).

**Import:** wings64-dc `0c384bd` `patches/017-memory-map-arena.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). `DC:` comments reworded `PORT:`. Code unchanged. On the Xbox the arena is contiguous memory from `xhw_rdram_alloc` (MmAllocateContiguousMemoryEx, mapped at 0x80000000 | physical), so arena addresses keep bit 31 set and the "RAM, not ROM" tests in `_uvMediaCopy`, `uvMemRead` and `filesystem.c` hold as above. `K0_TO_PHYS`/`OS_PHYSICAL_TO_K0` are the identity on the Xbox (patches/029), which the `K0_TO_PHYS` round trip mentioned above does not need. The XBE's static data has bit 31 clear (docs/decisions.md, "Memory map"). Unlike on the Dreamcast, `geometry.c:347`'s `+ 0x80000000` is dropped on the Xbox (patches/029).
