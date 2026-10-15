# 001-bitfield-uv-filesystem

**Why:** IDO packs bitfields MSB-first; GCC on little-endian SH-4 packs LSB-first. The u16 flag word in `uv_filesystem.h` (format = (flags>>8)&7, flag = flags&0x800) is declared in reverse order so field reads match the ROM bytes.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC (now WINGS64_XBOX).

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_XBOX` with the original in `#else`.

**Import:** wings64-dc `0c384bd` `patches/001-bitfield-uv-filesystem.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). Nothing else. The game triple lays bit-fields out as GCC does (`-mno-ms-bitfields`), which is what this hunk was written and checked for; `layout_checks.c` asserts `UnkCommStruct` is 0x18.
