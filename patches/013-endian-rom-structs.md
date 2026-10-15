# 013-endian-rom-structs

**Why:** Big-endian ROM structs copied raw with `_uvMediaCopy`/`uvConsumeBytes` are swapped in place right after the copy with `PW64_SWAP` (layout-checked, xbox/src/game/rom/pw64_swap.c): TABL index, UVEN, UVMD/UVCT Vtx and Mtx tables, UVTX Gfx words, UVLV/UVTR tables, UVBT u64 reads, tasks, spaths, levels, anims, user paths, demos, text, level block counts, and the code_A8C30 byte table (PLAN D3, H1).

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC (now WINGS64_XBOX).

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_XBOX` with the original in `#else`.

**Import:** wings64-dc `0c384bd` `patches/013-endian-rom-structs.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). Nothing else. Every `PW64_SWAP` layout on a header-declared type is asserted against `sizeof` under the game triple in `layout_checks.c`; the rest are checked at run time by `pw64_swap`.
