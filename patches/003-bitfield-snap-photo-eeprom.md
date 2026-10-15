# 003-bitfield-snap-photo-eeprom

**Why:** The EEPROM photo album stores the first 172 bits of each `Unk8033F050` as IDO lays it out (big-endian, MSB-first bitfields, 0x18 bytes). GCC lays the struct out differently, so explicit to/from IDO byte-image helpers build the saved bits; unset fields are zeroed so saves are deterministic.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC (now WINGS64_XBOX).

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_XBOX` with the original in `#else`.

**Import:** wings64-dc `0c384bd` `patches/003-bitfield-snap-photo-eeprom.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). Nothing else. `layout_checks.c` asserts `Unk8033F050` is 0x18 under the game triple (with MS bit-fields it would not be).
