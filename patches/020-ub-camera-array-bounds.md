# 020-ub-camera-array-bounds

**Why:** the camera struct (`UnkStruct_80204D94`, include/kernel/uv_graphics.h) declares the ground view
polygon as `Vec3F unk328[1]` followed by padding, but `func_80206318` (kernel/code_7150.c) stores up to five
points there and `_uvTerraDraw` (kernel/terrain.c) reads them back. `unk250[5]` and `unk2E0[5]` are likewise
indexed at `[5]` (the padding after each is element 5). Indexing past a declared bound is undefined; GCC 14
uses the bound to cut the copy and read loops to one iteration, so points 1..4 of the polygon read as zero.
The terrain then computes empty sub-tile masks and the island ground is not drawn (only the far sea ring).
IDO on the N64 did not exploit the bound.

**Fix:** declare the arrays at their real extent (`[6]`), absorbing the padding. Offsets are unchanged
(0x250+0x48 = 0x298, 0x2E0+0x48 = 0x328, 0x328+0x48 = 0x370); `xbox/src/game/rom/layout_checks.c` asserts them.

**Origin:** wings64-dc, own.

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).

**Import:** wings64-dc `0c384bd` `patches/020-ub-camera-array-bounds.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). `DC:` comments reworded `PORT:`. clang has no `-fno-aggressive-loop-optimizations` (docs/decisions.md), which makes this source fix the only protection for these arrays.
