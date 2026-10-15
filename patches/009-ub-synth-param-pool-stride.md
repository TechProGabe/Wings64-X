# 009-ub-synth-param-pool-stride

**Why:** `alSynNew` sizes its param pool with `sizeof(ALParam)` but stores `ALStartParamAlt` (larger on some ABIs) into it. Pool stride uses the larger struct. Harmless where both are 28 bytes; kept as a guard for the SH-4 ABI.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC (now WINGS64_XBOX).

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_XBOX` with the original in `#else`.

**Import:** wings64-dc `0c384bd` `patches/009-ub-synth-param-pool-stride.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). Nothing else (both structs are 28 bytes on i686 too; kept as the guard it was).
