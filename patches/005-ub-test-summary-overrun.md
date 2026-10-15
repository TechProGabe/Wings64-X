# 005-ub-test-summary-overrun

**Why:** Decomp UB: `textFmtInt` writes two terminators past `sp5C[3]`. Array grown to 3+2; the length argument stays 3.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC (now WINGS64_XBOX).

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_XBOX` with the original in `#else`.

**Import:** wings64-dc `0c384bd` `patches/005-ub-test-summary-overrun.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). Nothing else.
