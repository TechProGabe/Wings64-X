# 011-ub-mediacopy-odd-fault

**Why:** `_uvMediaCopy` deliberately stores to address 1 on an odd copy request (IDO split it into byte stores). On SH-4 that is an address-error exception with no message. Replaced with `pw64_fatal`.

**Origin:** wings64-dc, own (kb/traps.md).

**N64 build:** unchanged; every change sits under `#ifdef WINGS64_XBOX` with the original in `#else`.

**Import:** wings64-dc `0c384bd` `patches/011-ub-mediacopy-odd-fault.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). The hunk comment reworded: on the Xbox a store to address 1 is a page fault with no message, as the SH-4 address error was. `pw64_fatal` is `xbox/src/game/ultra/fatal.c` (log line + the platform error card).
