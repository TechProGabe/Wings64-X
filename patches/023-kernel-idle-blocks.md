# 023-kernel-idle-blocks

**Why:** `Thread_Kernel` (kernel/system.c) ends by dropping to priority 0 and spinning in `while (1) { }`:
the N64's idle loop, which only runs when every other thread waits. KOS v2.2.2's `thd_schedule` searches the
run queue *before* re-queuing the running thread on a timer tick, so the running thread hands the CPU to the
first other ready thread whatever its priority. The always-ready spin therefore got the CPU at every tick
(1 kHz here, os_shim.c): in Flycast ~44% of each display-list window ran the spin (the interpreter thread was
parked mid-work), and the game logic lost time the same way. The first hardware runs (15-25 fps, CPU-bound in
the interpreter) paid for it too. Under `WINGS64_XBOX` the thread blocks forever on a private message queue.

**Origin:** wings64-dc, own (frame-rate work after hardware run 1, docs/fps-plan.md).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).

**Import:** wings64-dc `0c384bd` `patches/023-kernel-idle-blocks.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). The comment reworded (KOS's tick is history here). Kept for the Xbox per PLAN.md D5: the Xbox kernel is strictly priority-preemptive, but an always-ready spin at the lowest priority still burns the CPU the idle loop would halt, and nothing on the N64 side needs it to run.
