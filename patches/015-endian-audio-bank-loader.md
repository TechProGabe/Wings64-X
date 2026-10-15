# 015-endian-audio-bank-loader

**Why:** `alBnkfNew`/`alSeqFileNew` patch offsets into pointers in place inside a raw big-endian copy. They are replaced by field-wise deserialisers (`pw64_alBnkfLoad`, `pw64_alSeqFileLoad`, xbox/src/game/rom/pw64_audio_load.c) that build host-order structs.

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC (now WINGS64_XBOX).

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_XBOX` with the original in `#else`.

**Import:** wings64-dc `0c384bd` `patches/015-endian-audio-bank-loader.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). The hunk comments point at `xbox/src/game/rom/pw64_audio_load.c`. The loader there lost wings64-dc's AICA hook (`aud_synth_bank_table`): the Xbox runs libultra's own synthesizer (PLAN.md D6).
