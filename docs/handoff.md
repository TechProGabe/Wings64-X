# Handoff

Newest first. Each milestone entry says what was verified and how; "xemu"
results are emulator-only (no timing, audio or memory claims from them).

## M2 — the decomp compiles and links (2026-10-15)

Branch: a worktree branch off `port` (M0); nothing pushed.

- `tools/xbox/fetch_decomp.sh [--refresh]`: n64decomp/pw64 at
  `1a8558f5ceb320cce254d6da0a49a1f2bbc23e21` into `external/pw64-decomp`
  (gitignored), checked out with `core.autocrlf=false`, then `patches/*.patch`
  in name order with `patch -p1 --fuzz=0`; the tree replaces the old one only
  once every patch applied; a stamp (pin + patch hash) makes reruns no-ops.
- `patches/`: wings64-dc 001-024 and 026-028 (its `0c384bd`), guard renamed
  `WINGS64_XBOX`, each `.md` with an Import line. Changed for the Xbox: 022
  runs the audio command list (`wings64_audio_task`, RSP audio HLE in M6)
  instead of dropping it; 021's helper renamed `uvFontOverlayStr_port`;
  SH-4/KOS/PVR comments reworded. New: 029 (KSEG0/physical conversions are
  the identity, `geometry.c:347`'s `+ 0x80000000` dropped; the lead's
  address model). Table: docs/decisions.md.
- `xbox/include/game/`: `wings64_prelude.h` (force-included: `WINGS64_XBOX`,
  `PW64_SWAP`, `N64_K0`, `pw64_fatal`, printf family -> `wx_*`), shadow
  `macros.h`, `segment_symbols.h` (constants: no linker symbols needed),
  `pw64_endian.h`, `wings64_settings.h`, `wings64_fbfreeze.h`.
- `xbox/src/game/`: `rom/` (`pw64_swap.c`, `pw64_rom_structs.c`,
  `pw64_audio_load.c` without the AICA hook, `pw64_arena.c`, `rdram.c`: the
  arena from the new `xhw_rdram_alloc` = contiguous, cached, bit 31 set,
  `layout_checks.c`), `ultra/` (`mio0.c`, `ultra_data.c`, `fatal.c`,
  `stubs.c`, `hooks.c`), `wx_log.c` (Melee-X's float-aware formatter, the
  game's printf family and `_uvDebugPrintf` forwarding), `wx_boot.c`
  (`wx_boot_game`: arena + `bootproc`; not called yet).
- `tools/xbox/compile_game.py` (game triple, parallel, `-MMD`, flags stamp,
  `XBOX_GAME_OUT`, `XBOX_GAME_CFLAGS`, `XBOX_LTO`), `tools/xbox/thinlto_link.py`
  (Melee-X's minus the `.text` renaming), CMake: `-include:_wx_boot_game`,
  `-errorlimit:0`, `XBOX_LTO` option + `RULE_LAUNCH_LINK`; `xbox/build.sh` and
  `tools/xbox/msys/build.sh` pass `XBOX_LTO`. `xhw.h`: `xhw_rdram_alloc`;
  `xhw_logf`'s format attribute spelled `__printf__` (the prelude renames
  `printf`).
- Undefined at the first link, now in `ultra/stubs.c` as
  `pw64_fatal("TODO: <name>")` (41): `__osGetCurrFaultedThread`,
  `osAiGetLength`, `osAiSetFrequency`, `osAiSetNextBuffer`,
  `osContGetReadData`, `osContInit`, `osContStartReadData`,
  `osCreateMesgQueue`, `osCreatePiManager`, `osCreateThread`,
  `osCreateViManager`, `osEepromLongRead`, `osEepromLongWrite`,
  `osEepromProbe`, `osGetCount`, `osInitialize`, `osInvalDCache`,
  `osPiRawReadIo`, `osPiReadIo`, `osPiStartDma`, `osRecvMesg`, `osSendMesg`,
  `osSetEventMesg`, `osSetIntMask`, `osSetThreadPri`, `osSetTimer`,
  `osSpTaskLoad`, `osSpTaskStartGo`, `osSpTaskYield`, `osSpTaskYielded`,
  `osStartThread`, `osViBlack`, `osViGetCurrentFramebuffer`,
  `osViGetNextFramebuffer`, `osViSetEvent`, `osViSetMode`,
  `osViSetSpecialFeatures`, `osViSwapBuffer`, `osWritebackDCache`,
  `osWritebackDCacheAll`, `osYieldThread`; plus `osVirtualToPhysical`, already
  the identity (42 in all).
- Verified: fetch from an empty `external/` applies all 28 patches with zero
  fuzz; 197 decomp + 13 port units compile with no compiler output at all
  (`layout_checks.c`'s 60 `_Static_assert`s hold under the game triple); the
  link has no undefined symbols; `default.xbe` 1,861,632 bytes (`.text`
  0x1993FC = 1,676,284 bytes); ThinLTO build 1,820,672 bytes (`.text`
  0x1909DC). xemu (64 MB, no ROM): both boot to the splash and the M0
  "Nothing to run" card (the card's text still says the game is not linked:
  `xhw_main.c` is the M1 agent's).
- Not verified: anything running game code (nothing calls it);
  `xhw_rdram_alloc` at run time (not called yet).
- Open for M3: the OS shim replaces `stubs.c` (threads on kernel stacks, so
  stack locals pass the bit-31 RAM test); static RAM sources of
  `_uvMediaCopy` in `save.c` and `task.c` read as ROM (docs/decisions.md,
  "Memory map"); `hooks.c` stubs: Settings (M7, `-DWINGS64_SETTINGS_MENU=0`
  until then or the title stops at a TODO card), frame persistence (renderer),
  audio task (M6); ThinLTO as the default once the game runs both ways; the
  Docker files still name Melee-X's image and `disc_lower` (M0 copy).

## M1 — ROM validation (2026-10-15)

Branch `worktree-agent-af49dbd909c92a453` (from `port` at M0; nothing pushed). PLAN 3.1 in full; procedure in `docs/rom.md`.

- New: `xbox/include/xrom.h` (result struct, pure API), `xrom_known.h` (the
  only place the SHA-1 lives), `xhw_sha1.c` (incremental), `xhw_rom.c` (pure
  functions plus the Xbox find/load/hash/card half behind `XROM_HOST_TEST`).
  `main_body` calls `xhw_rom_load_verified`, then `wx_boot(rom, size)`; the weak
  default shows the "ROM verified: US v1.0" card. `xhw_error_card` keeps an
  exit reason a caller already set (the ROM check sets `ROM check failed (...)`).
- Tools: `rom_order.py` (z64/v64/n64 converter), `rom_leak_check.py`,
  `test_rom_check.py` + `tests/xbox/test_rom_check.c`.
- Verified, host: the test (55 checks, 0 failed, clang, no warnings at -Wall -Wextra):
  SHA-1 vectors, z64/v64/n64 identical, every verdict and message.
  `rom_leak_check.py` clean. The user's `.z64` has the table's SHA-1 and MD5 and
  its `.v64` is byte-identical to `rom_order.py`'s conversion.
- Verified, xemu only (no timing claims): the real ROM as z64, v64 and n64 each
  `[ROM] verified: US v1.0 SHA-1 ok` with the same SHA-1; truncated, one-byte
  corrupt, NPWJ, other game code, bad magic and no-ROM each show their card
  with the `[ROM]`/`[BOOT]` lines in the serial log.
- Not verified: the console; `lastexit.txt` content after a failure (xemu slots
  start from a fresh HDD copy; the log line "lastexit.txt will say" shows the
  reason set); load and hash time on real hardware (xemu: 32 ms, ~210 ms, not
  meaningful); the `MX_ROM` getenv path (xemu has no environment).

## M0 — toolchain, skeleton, splash, first XBE (2026-10-15)

Branch `port` (local; nothing pushed).

- Platform layer copied from Melee-X v4.1 (`abdfd0f`) with the Melee parts
  removed: `xhw_main.c`, `xhw_splash.c` (unchanged), `xhw_video.c` (480i/480p,
  720p and 1080i chosen once before pbkit; 1080i behind `-DXHW_VIDEO_1080I`),
  `xhw_sys.c` (log, files, threads), `xhw_pad.c`, `xhw_watchdog.c`,
  `xhw_crash.c`, `xhw_string.c`, `xhw_fbdump.c`, `xhw_autopad.c`;
  `nv2a_fault.c` holds the hooks the patched pbkit calls.
- New: `xhw_ramlock.c` (OpenCrossing's; 128 MB consoles run as 64 MB,
  `-DXHW_RAM_LOCK64=0` to disable), `xhw_clock.c` (every clock is the ACPI
  timer; the TSC is only measured at boot for `[CLOCK] cpu N MHz`;
  `-DXHW_CLOCK_TSC=0` skips that read; the watchdog re-checks the retrace
  rate against ACPI time every 30 s).
- Title ID `57580001` ("WX"), written into the certificate by
  `tools/xbox/xbe_title_image.py`; saves and logs in `E:\UDATA\57580001\`.
- Verified (xemu, 64 MB, no ROM staged): boots, splash, `[CLOCK] cpu 733 MHz
  (TSC against the ACPI timer, 3375000 Hz)`, `[MEM] boot: 65536 KB of RAM,
  nothing to hold`, then the "Nothing to run" card and the dashboard.
- Not verified: the console (no hardware run yet), a 128 MB xemu run, the
  dashboard icon on a real dashboard.
