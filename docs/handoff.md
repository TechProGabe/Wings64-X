# Handoff

Newest first. Each milestone entry says what was verified and how; "xemu"
results are emulator-only (no timing, audio or memory claims from them).

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
