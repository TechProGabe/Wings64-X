# 008-ub-reverb-unsigned-index

**Why:** `&r->input[-d->input]` negates a u32 index. Cast to s32 before negating so the pointer arithmetic is defined.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC (now WINGS64_XBOX) (the 64-bit-only PW64_U32 in the ramalign line dropped).

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_XBOX` with the original in `#else`.

**Import:** wings64-dc `0c384bd` `patches/008-ub-reverb-unsigned-index.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). Nothing else.
