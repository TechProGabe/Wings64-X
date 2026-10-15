# 012-yield-busy-waits

**Why:** Busy-wait spins on the app clock (`snap.c` x3, `title_screen.c`, `uvaSeqStop`) relied on N64 preemption by the scheduler/audio threads. KOS only round-robins equal priorities, so the spins yield with `osYieldThread()` (PLAN D5, H4).

**Origin:** Birdman64 (NoahVl, MIT) crates/pw64-game/patches, decomp pin 2529dab; hunk carried over, wrapped in WINGS64_DC (now WINGS64_XBOX).

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_XBOX` with the original in `#else`.

**Import:** wings64-dc `0c384bd` `patches/012-yield-busy-waits.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). Comments reworded (Birdman64's "the native scheduler is cooperative" and its `native-build.md` references). The yields stay: on the Xbox the game threads are kernel threads (PLAN.md D5), and a spin on the app thread must still not keep equal-priority threads (the scheduler's work, the audio thread) waiting for a time slice. `osYieldThread` is a stub until M3.
