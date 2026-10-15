# 027-debug-look-at-pad

**Why:** the landing target bug (striped / "clipped" bullseye on hardware) needs the target on screen in
the emulator, and scripted flights never reach it.

**What:** `app/code_D2B10.c` `func_8034B624` (the flight camera push): with `WINGS64_DBG_LOOK_AT_PAD=<units>`
the camera matrix is replaced, before `uvChan_80204B34`, by a fixed camera looking at the first used landing
pad from that distance (26.6 degrees down, facing +y). Culling and drawing then use that camera. Debug only.

**Kill switch:** off unless `XBOX_CFLAGS=-DWINGS64_DBG_LOOK_AT_PAD=100` (LOD0) or `=300` (LOD1).

**Origin:** wings64-dc, own (plan from the 2026-10-12 Fable consult).

**N64 build:** unchanged (the hook is `#if defined(WINGS64_XBOX) && ... WINGS64_DBG_LOOK_AT_PAD > 0`).

**Import:** wings64-dc `0c384bd` `patches/027-debug-look-at-pad.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). Nothing else. Debug only: `XBOX_CFLAGS=-DWINGS64_DBG_LOOK_AT_PAD=100`.
