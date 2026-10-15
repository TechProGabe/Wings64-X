# 016-endian-font-bitmaps

**Why:** UVFT font `FRMT` words and `BITM` Bitmap records are big-endian; read through `uvMemRead` and `pw64_load_bitmaps` (xbox/src/game/rom/pw64_rom_structs.c).

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC (now WINGS64_XBOX).

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_XBOX` with the original in `#else`.

**Import:** wings64-dc `0c384bd` `patches/016-endian-font-bitmaps.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). Nothing else.
