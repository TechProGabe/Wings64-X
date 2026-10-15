# 006-ub-sicontstatus-array

**Why:** `osContInit` writes MAXCONTROLLERS (4) `OSContStatus` entries; the decomp declares one. IDO left slack after it; GCC packs globals. Declared as `[4]`.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC (now WINGS64_XBOX).

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_XBOX` with the original in `#else`.

**Import:** wings64-dc `0c384bd` `patches/006-ub-sicontstatus-array.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). Nothing else.
