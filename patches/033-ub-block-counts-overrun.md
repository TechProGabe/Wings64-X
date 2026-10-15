# 033-ub-block-counts-overrun

**Why:** `uvLevelInit` (kernel/memory.c) copies the ROM's `UVSY` `COMM` block into `gUVBlockCounts`
(`UVBlockCounts`, 36 bytes) with the block's own length. The block can be longer than the struct, so the
copy runs into the next static object; on the N64 that was harmless, on the Xbox the linker put
`alGlobals` (libultra audio, sl.c) right behind it (0x24 bytes on), so every level init after the audio
manager's `alInit` zeroed it and the audio thread died on a NULL read in `alAudioFrame` (xemu crash
report, symbolized: `alAudioFrame+0x1c` <- `__amMain`), leaving the game silent.

**Fix:** under `WINGS64_XBOX`, the copy is clamped to `sizeof(gUVBlockCounts)`; the fields the struct
holds are copied as before (and swapped by patch 013's `PW64_SWAP`).

**Origin:** Wings64-X, own.

**N64 build:** unchanged (`#ifdef WINGS64_XBOX`).
