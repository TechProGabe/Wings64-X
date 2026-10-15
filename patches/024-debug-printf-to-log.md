# 024-debug-printf-to-log

**Why:** `_uvDebugPrintf` (kernel/system.c) is an empty function in the retail game, so every diagnostic the
game has ("_uvSortAdd: sort list full", "uvDobjModel ... model does not exist in level", "uvaSeqStop timed
out", "no free emitters", ...) vanished. The second hardware run hung at a test overview with nothing in the
SD log about why. Under `WINGS64_XBOX` the function forwards to `wings64_uv_debug`, which logs
each distinct message a few times (rate-limited per format string).

**Origin:** wings64-dc, own (hardware hang triage, 2026-10-11).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).

**Import:** wings64-dc `0c384bd` `patches/024-debug-printf-to-log.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). The comment reworded. `wings64_uv_debug` is `xbox/src/game/wx_log.c`: each distinct format logged at most four times as `uv: ...` lines through `xhw_logf`, formatted by the float-aware formatter (pdclib has no `%f`; `uvMat4CopyF2L` prints floats).
