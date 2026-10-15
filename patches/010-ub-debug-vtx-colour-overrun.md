# 010-ub-debug-vtx-colour-overrun

**Why:** `uvDbg_802339B0` colours 0x26 vertices through `D_8024B348` (14 entries), relying on IDO placing four arrays back to back. GCC gives no layout guarantee (`-Waggressive-loop-optimizations` flags it). Each array is walked explicitly; a static assert pins the 0x26 total.

**Origin:** wings64-dc, own.

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_XBOX` with the original in `#else`.

**Import:** wings64-dc `0c384bd` `patches/010-ub-debug-vtx-colour-overrun.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). The hunk comment `DC:` reworded `PORT:` and "GCC" to "GCC/clang". Code unchanged.
