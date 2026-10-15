# 019-ub-matrix-overflow-fault

**Why:** `uvMat4CopyF2L` (kernel/matrix.c) clamps an out-of-range translation (|t| > 32767) to the s15.16
limit and then deliberately stores to address 1 (`*(s16*)1 = 0`) to stop a debug build. On the SH-4 that is
an address-error exception with no message, and large outdoor coordinates make it reachable. The store is
dropped; the clamp stays, so the matrix saturates exactly as the N64 release build would have written it.

**Origin:** wings64-dc, own (same pattern as 011).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).

**Import:** wings64-dc `0c384bd` `patches/019-ub-matrix-overflow-fault.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). `DC:` comments reworded `PORT:`. On the Xbox the store is a page fault.
