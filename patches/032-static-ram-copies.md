# 032-static-ram-copies

**Why:** `_uvMediaCopy` (kernel/memory.c) tells RAM from ROM by bit 31 of the source: set is RAM, clear is
a ROM offset (ROM addresses are below 8 MB). On the Xbox the XBE's static data sits below 0x80000000
(docs/decisions.md, "Memory map"), so three copies whose source is a static array read the ROM instead:
- `app/save.c` `saveModuleInit`: `sSaveFilesMirror <- sSaveFiles` (the mirror held ROM bytes, so every
  `saveFileWrite` found a difference and wrote the EEPROM again).
- `app/save.c` `saveFileWrite`: `sSaveFilesMirror[i].raw <- saveFile->raw` (a pointer into `sSaveFiles`).
- `app/task.c` `taskBirdmanPad`: `D_8035078C->dataTPAD <- sTaskTakeoffPads[mapIdx]` (the Birdman take-off
  pad came from ROM bytes).
- `app/task.c` `taskInit`: `sTaskTakeoffPads[classIdx] <- obj->dataTPAD`, the boot-time fill of that table:
  the task's parsed objects are host memory below 0x80000000 too, so the table held ROM bytes and a
  Birdman star started the bird at a NaN position, the game calling it a crash at once (console, rc2;
  xemu: the pad words garbage after the switch, the position NaN).

**Fix:** under `WINGS64_XBOX`, these four are plain RAM copies (`__builtin_memcpy`). The other
`_uvMediaCopy` sources are ROM addresses or the arena (bit 31 set); stack sources are on kernel stacks
(bit 31 set, M3's `osCreateThread`).

**Origin:** Wings64-X, own (the sites listed in docs/decisions.md since M2).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).
