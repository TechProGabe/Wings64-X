# 029-host-pointer-identity

**Why:** the N64 game converts between KSEG0 virtual and physical addresses when it builds display lists
and RSP task data (`OS_PHYSICAL_TO_K0` in `geometry.c`, `graphics.c`, `sprite.c`, `texture.c`;
`K0_TO_PHYS` in libultra audio's `load.c`), and `geometry.c:347` adds 0x80000000 to a vertex pointer by
hand. On the Xbox those lists are read by CPU-side interpreters (the Fast3D interpreter, the RSP audio
HLE), which take an address as it is plus the segment table. The RDRAM arena is contiguous memory at
0x80000000 | physical (`xhw_rdram_alloc`, so the game's bit-31 RAM-not-ROM tests hold), while the XBE's
static data sits below 0x80000000: adding or masking 0x80000000 would make the two ranges ambiguous.
So every conversion is the identity, and a host pointer stays a host pointer all the way into the
interpreters (the lead's address-model decision, 2026-10-15; `osVirtualToPhysical` is the identity in the
OS shim).

**What:**
- `include/libultra/PR/os_convert.h`: `OS_K0_TO_PHYSICAL`, `OS_K1_TO_PHYSICAL`, `OS_PHYSICAL_TO_K0`,
  `OS_PHYSICAL_TO_K1` are the identity.
- `include/libultra/PR/R4300.h` (C branch): `K0_TO_K1`, `K1_TO_K0`, `K0_TO_PHYS`, `K1_TO_PHYS`, `KDM_TO_PHYS`,
  `PHYS_TO_K0`, `PHYS_TO_K1` are the identity. `IO_READ`/`IO_WRITE` (rcp.h) go through `PHYS_TO_K1`; the
  game's only use in the built units (`sched.c`'s SP status write) is already gone (patches/018).
- `src/kernel/geometry.c:347`: the hand-written `+ 0x80000000` dropped.

Checked (grep over `src/app`, `src/kernel`, `src/libultra/{audio,sp}` for `0x80000000`, `0xA0000000`,
`0x1FFFFFFF` and the conversion macros): `geometry.c:347` is the only hand-written conversion of a host
pointer. Unchanged on purpose: the bit-31 tests (`memory.c:181,234`, `filesystem.c:89`: they now work on
the contiguous arena), `debug.c`'s and `memory.c`'s KSEG0 literals (patches/017's `N64_K0`), `hud.h`'s
`HUD_CAM_RENDER_RETICLE` (a flag bit). `texture.c:859-861` ORs a base pointer into `setimg.dram`: with the
identity it ORs the host pointer, the same arithmetic as on the N64 minus bit 31.

**Origin:** Wings64-X, own.

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).
