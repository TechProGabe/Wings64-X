# 002-bitfield-setimg-cmd

**Why:** `_uvExpandTexture` finds G_SETTIMG commands by reading the `setimg.cmd` bitfield. With LSB-first bitfields that reads the wrong byte, so the opcode is read as `words.w0 >> 24` (PLAN H1, kb/traps.md).

**Origin:** wings64-dc, own (Birdman64 does the same inside a 64-bit hunk).

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_XBOX` with the original in `#else`.

**Import:** wings64-dc `0c384bd` `patches/002-bitfield-setimg-cmd.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). The hunk comment `DC:` reworded `PORT:`. Code unchanged.
