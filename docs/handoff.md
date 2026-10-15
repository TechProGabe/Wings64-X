# Handoff

Newest first. Each milestone entry says what was verified and how; "xemu"
results are emulator-only (no timing, audio or memory claims from them).

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
