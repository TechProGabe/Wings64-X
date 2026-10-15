# 018-sched-rsp-status-write

**Why:** `_uvScDlistRecover` (RSP/RDP timeout recovery) writes the N64 SP status register
(`IO_WRITE(SP_STATUS_REG)`, 0xA4040010). On the Dreamcast that address is unmapped. There is no RSP;
the recovery's message posting is kept, the register write is dropped.

**Origin:** wings64-dc, own (Birdman64 replaces the line with a trace log).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).

**Import:** wings64-dc `0c384bd` `patches/018-sched-rsp-status-write.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). The hunk comment `DC:` reworded `PORT:`. On the Xbox 0xA4040010 is just as unmapped.
