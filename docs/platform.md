# Platform layer (`xbox/src/hw`)

The platform layer is the part of the XBE that talks to the Xbox: the kernel,
pbkit and the NV2A, the AC97 and the APU, the controllers, the file system,
the logs. It is built with nxdk's own triple (`i386-pc-win32`, `-O2
-march=pentium3`, see `docs/toolchain.md`) and is reached from the game side only
through `xbox/include/xhw.h`, `xgfx.h` and `xrom.h` (scalars, floats and byte
arrays only). Its design comes from Melee-X (`xhw_*` names and the splash,
video, pad, watchdog, crash and fbdump code), OpenCrossing-Xbox (the RAM lock,
clock, code repair, ROM-free audio driver and the GPU hardware notes) and
Wings64-X's own ROM and settings code. The file headers say which part is
whose; this document says what the code does.

Test-build switches and the log format are in `docs/testing.md`. Memory budget and
the boot-to-game picture are in `docs/architecture.md`.

## 1. Boot flow (`xhw_main.c`)

`main()` runs `main_body` under `xhw_crash_guard` (`xhw_crash.c`), so a fault
on the main thread writes `crash.log` instead of freezing the console. In order:

1. `xhw_ram_lock64()`: on a 128 MB console the RAM above 64 MB is held (section 4).
   Before anything allocates.
2. `xhw_set_game_thread()`: the boot thread is recorded as the game thread (the
   watchdog and the profiler report where it was when a hang or a sample happens).
3. Mount `E:` (`\Device\Harddisk0\Partition1\`) and create `E:\UDATA` and
   `E:\UDATA\57580001`. A failure logs `[BOOT] could not mount E:` and saves
   are disabled, nothing else.
4. `xhw_log_open_file()`: `boot.log` in the save folder (or `D:\` when the save
   folder cannot be written); the previous `boot.log` is kept as `boot_prev.log`.
5. The banner `[BOOT] Wings64-X (date time)`, with `test build` when
   `XHW_TEST_BUILD`. `log_save_folder()`: free space on E:, and a write test
   there (a notice is shown if the folder cannot be written).
6. `read_image_range()`: the XBE's base and size from its header (for the crash
   and hang reports). `log_previous_exit()`: reads and deletes `lastexit.txt`
   and logs why the last boot ended (section 7).
7. `xhw_ram_lock64_log()`, `xhw_code_repair()` (puts back `.text` bytes that differ
   from the file, before the clock reads the TSC), `xhw_watchdog_start()`,
   `xhw_clock_calibrate()`, `xhw_prof_start()` (test builds), `xhw_mem_log("boot")`.
8. `xhw_splash_show()`: the splash (section 3.3 below). `xhw_autopad_load()`
   (test builds: `D:\autopad.txt`).
9. `xhw_rom_load_verified()`: find, load and check the ROM (section 2). On
   failure it shows the card and goes to the dashboard after the card's wait.
10. `wx_boot(rom, size)` (`xbox/src/game/boot.c`): never returns. It loads
    `settings.ini`, sets the video mode, starts the renderer, the RDRAM arena
    and the decomp's `bootproc`, then only watches the game's state for the log.
    The boot thread is re-levelled to kernel level 2 and loops with a `[GAME]`
    line each time the state changes.

If `wx_boot` returns (it does not), `xhw_quit_to_dashboard()` runs. The
weak default of `wx_boot` in `xhw_main.c` only shows the "ROM verified" card; it
is used when the game is not linked.

Error cards (`xhw_error_card`, `xhw_fatal`): the title, the lines, "Press any
button or wait to return to the dashboard", then the dashboard after `secs`
(`xhw_error_card` takes the wait; `xhw_fatal` uses 15 s). A press counts only
after the buttons that were down when the card went up have been released.

## 2. ROM find, load and check (`xhw_rom.c`, `xrom.h`, `xrom_known.h`, `xhw_sha1.c`)

Full procedure and test in `docs/rom.md` and PLAN.md 3.1. What the code does:

- **Find**: the first file in `D:\` (the XBE's folder, `xhw_game_dir()`) whose
  extension is `.z64`, `.n64`, `.v64`, `.rom` or `.bin` (case-insensitive) and
  whose name does not start with `default`, `settings`, `boot` or `autopad`.
  Directory order decides. Test builds: `env MX_ROM=<name>` picks the file.
  None: the "No ROM found" card (30 s, then the dashboard).
- **Load**: the file is opened, its size checked against 8,388,608 bytes (the
  wrong size is a card, not a partial read), a buffer of that size is committed
  (`xhw_commit`: the kernel cannot take a demand-commit fault) and read in
  256 KB pieces under the splash's load bar.
- **Byte order** from the first word: `80 37 12 40` (z64, as is), `37 80 40 12`
  (v64, swap each pair), `40 12 37 80` (n64, reverse each 4-byte word). Anything
  else: "Not an N64 ROM". The image is normalised to z64 in place.
- **Header**: title (0x20), game code (0x3B), region (0x3E), version (0x3F),
  CRC1/CRC2 (0x10, 0x14). The code must be `NPWE`; `NPWJ` and `NPWP` get the
  "not supported" card; another code, "This ROM is not Pilotwings 64".
- **Hash**: SHA-1 of the whole normalised image (`xhw_sha1.c`, chunked). The
  only known-good value is in `xrom_known.h`. Match: `[ROM] verified: US v1.0 SHA-1
  ok`. Mismatch: "Bad or modified ROM dump" with the found and expected hashes.
- **Use**: `xhw_rom_load_verified` returns the buffer and `wx_rom_ptr` (in
  `boot.c`) serves every cart read (PI DMA, audio tables) from it. Nothing else
  reads the file.

The pure functions (order, normalise, header, verdicts, hash comparison) build on
the host with `-DXROM_HOST_TEST` (`tests/xbox/test_rom_check.c`).

## 3. Display and video (`xhw_video.c`, `xhw_splash.c`, `xhw_watchdog.c`)

### 3.1 Modes

`xhw_video_boot()` runs once, before pbkit. The dashboard's encoder settings
say what is allowed (`xhw_video_480p_allowed`, `_720p_allowed`, `_1080i_allowed`:
the mode flag, an HDTV or VGA pack, and not PAL for the progressive modes); the
preference from `settings.ini` (`output`, `progressive`) chooses:

| mode | set with | condition |
|---|---|---|
| 1080i | `XVideoSetMode(1920, 1080, 16)` | allowed, ≥ 40 MB free, `XHW_VIDEO_1080I` |
| 720p | `XVideoSetMode(1280, 720, 16)` | allowed, ≥ 32 MB free |
| 480p / 480i | `XVideoSetMode(640, 480, 32)`; 480i on an HDTV pack with `XVideoInit(0x0801010d, ...)` | always available |

Auto (`output = auto`) takes 1080i, then 720p, then 480p, as the dashboard
allows. A mode the dashboard does not allow, or that is short of memory, falls to
480. The 16-bit HD modes are the render targets' format (`XGFX_Z24_16BPP`, section
4.3). The chosen mode is logged as `[VIDEO] WxH N-bit progressive|interlaced 16:9|4:3 dashboard`.

BACK held at boot (`wx_settings_load`) forces 480i 4:3 for this boot and saves
it (`[SETTINGS] BACK held at boot`).

Test builds: `env MX_VIDEO=480i|480|720|1080` overrides the mode for the boot.

`xhw_video_fallback_480()`: the renderer's way back to 640x480 when `pb_init`
or a contiguous allocation fails in an HD mode. This is the "never a black
screen because of memory" rule.

### 3.2 Retrace and presents

The VI's retrace is pbkit's vblank once pbkit is up and the output is 60 Hz
(`XHW_VBLANK_PACE`); before that, and for a 50 Hz output, the ACPI timer paces
60.000 Hz (`[VIDEO] retrace from ...`). `XVideoWaitForVBlank` is never used (it
hooks the GPU interrupt and makes `pb_init` fail). The counters the watchdog and
the log read are `xhw_frame_count()` (retraces) and `xhw_present_count()`
(presents). `xhw_shot_request()` and `xhw_shot_take()` carry a screenshot request
to the renderer's present.

### 3.3 Splash

`xhw_splash.c` (Melee-X's, unchanged in look): "TechProGabe Presents..." on a
navy gradient, fading in, held for `XHW_SPLASH_MS` (2000); a button skips it. The
load bar under the title is driven by `xhw_splash_progress`. The splash draws
with the CPU into the linear framebuffer `XVideoSetMode` returns, so it needs no
GPU state. `xhw_splash_release()` runs before the mode changes (the mode switch
frees that framebuffer). The bottom line reads "Hold BACK for 480i (safe
video)".

### 3.4 Watchdog (`xhw_watchdog.c`)

A thread at kernel level 15 ticks once a second. It reports a hang when no
retrace comes for `XHW_WATCHDOG_BOOT_SECS` (45) after boot or
`XHW_WATCHDOG_SECS` (6) later; a game loop that keeps retracing but presents
nothing for `XHW_WATCHDOG_PRESENT_SECS` (10) is logged, and after a minute it is
also put on screen. The dump lists every thread (state, wait, priority, EIP) and
the stack words in the image, to COM1, `boot.log`, `E:\UDATA\57580001\hang.log`
and the screen. Each tick also flushes the log and every 30 s rechecks the
retrace rate against the ACPI timer (`[CLOCK] retrace`). A `[BEAT]` line every
`XHW_HEARTBEAT_SECS` (5) keeps the last seconds on disk.

## 4. Memory and clock

### 4.1 RAM lock (`xhw_ramlock.c`)

Everything is built for 64 MB: the GPU's addresses are masked with `0x03FFFFFF`,
and allocations that fail on 64 MB must fail the same way everywhere. On a
128 MB console the BIOS can keep the upper half from the title (the XBE is
marked "limit to 64 MB"); a kernel that hands it out anyway is handled by
`xhw_ram_lock64()`: commit every free page (bar a reserve) top down, keep those
at or above 64 MB, decommit the rest, repeat while a pass finds some. A 64 MB
console logs `nothing to hold`. The held pages are never returned. Kill switch:
`XHW_RAM_LOCK64=0` (diagnosis only). Log: `[MEM] boot: ...`.

### 4.2 Clock (`xhw_clock.c`)

Every time the port uses comes from `KeQueryPerformanceCounter`, which on the
Xbox kernel is the ACPI power-management timer: 3,375,000 Hz from its own
crystal, independent of the CPU (`xhw_ticks_per_sec`). This covers the log
timestamps, `osGetTime`/`osGetCount`, the frame pacer's fallback and the
watchdog. The TSC is read at one place only: `xhw_clock_calibrate()` measures it
against 100 ms of ACPI time at boot and logs `[CLOCK] cpu N MHz (...); nxdk's
guess M MHz`. `XHW_CLOCK_TSC=0` skips that read. Nothing in a hot loop depends on
the TSC.

### 4.3 Pixels: pbkit and the NV2A back end

- **pbkit** is nxdk's, patched at configure time by `tools/xbox/patch_pbkit.py`
  so a GPU fault cannot freeze the console: PGRAPH and DMA errors are reported to
  `ocx_pb_gpu_fault()` (in `nv2a_fault.c`, logged at passive level), and the
  interrupt-time waits are bounded. The patched pbkit provides `pb_init`, the frame buffers and the pushbuffer.
- **Frame buffers** are in the mode's format: 640x480x32 with Z24S8 (480); 1280x720
  and 1920x1080 with R5G6B5 colour and Z24S8 depth (720p, 1080i; `XGFX_Z24_16BPP`
  is the 16-bit colour switch, and `XGFX_TILE` the tile regions).
- **Pushbuffer**: 1 MB (`PB_BYTES`), kicked every 8192 words (`XGFX_PB_KICK`),
  with `BREAK_VERTEX_BUFFER_CACHE` at each batch (`XGFX_VB_CACHE_BREAK`). A vertex
  ring of 1.5 MB (`RING_BYTES`). The wait for idle (`wait_idle`) closes the pushbuffer and loops while the GPU is busy,
  reporting a stall after 2 s (`[NV2A] GPU stalled`).
- **Texture pool**: 8 MB at 480, 6 MB at HD (`TEX_POOL_*`; `XGFX_TEX_POOL_KB`
  overrides both), first fit, big and small blocks, deferred frees; textures are
  keyed by RAM address and tile descriptor and wiped on a level load (`nv2a.c`).
- **Flips**: the present waits for the flip (`XGFX_FLIP_WAIT`), and when pbkit
  loses a flip the newest finished frame is put on screen (`XGFX_FLIP_REPAIR`).
  The `[NV2A]` lines count the waits and the repairs.
- **Tiles and Z**: tile region 0 is enabled with pbkit's base (`XGFX_TILE` bit 4),
  and Z compression is off by default (bit 8), because the stencil shadows looked
  wrong on the console with it. The framebuffer's CPU aperture is mapped when the
  tile is set, so the CPU can read it (`fb_cpu`).
- **Combiners, blend, depth, stencil** are set by the interpreter's per-draw
  state (`xgfx.h`); `nv2a_cc.c` compiles the N64 combiner to register combiners.
- **Antialiasing**: supersampling (2x or 4x) renders larger and resolves at
  present; only at 640x480 32-bit, and only when contiguous memory is there.

The interpreter and this back end are described in `docs/renderer.md` and
`docs/architecture.md`.

## 5. Audio (`xhw_audio.c`, `asp_hle.c`, `xbox/src/game/audio.c`)

The N64's audio is a list of RSP audio commands (libultra's synthesizer, from
the decomp, unchanged):

1. An audio task reaches `osSpTaskStartGo` (`os_shim.c`) and goes to
   `wx_audio_task` (`audio.c`), which runs the list through `asp_hle_run`
   (`asp_hle.c`): the RSP's audio microcode in C, with the same commands and
   arithmetic (Birdman64's `abi.rs` as oracle). Addresses are host pointers
   (`ASP_HLE_SEGMENTS 0` on the Xbox). The ROM's resampler table is read from
   the cart at first use.
2. The AI buffer the list wrote is handed over with `osAiSetNextBuffer`. The
   virtual DAC in `os_shim.c` keeps the game's audio clock in wall-clock time.
3. `xhw_audio_write()` puts it in a lock-free ring of 32768 `int16` samples
   (16384 stereo frames).
4. The pump thread (`xhw_audio.c`, kernel level 14, `XHW_AUDIO_PUMP_LEVEL`)
   resamples 22050 Hz to 48 kHz by linear interpolation (`fill_48k`) and keeps
   the AC97's descriptor ring (8 buffers of 1024 frames, about 21 ms each) queued.
   If the ring holds more than `XHW_AUDIO_MAX_MS` (200), the oldest samples are
   skipped down to `XHW_AUDIO_TARGET_MS` (80), so latency cannot build up. Silence
   on underrun.

The hardware path is the AC97 driven **polled**, with no interrupt (nxdk's audio
IRQ handler froze a real Xbox). At boot the controller and the APU are logged as
found and brought to idle, whatever ran before. An engine that never finishes a
buffer is restarted (bus masters reset, cold reset after that) and given up on
after repeated failures, logged (`[AUDIO] AC97 ... restarting`, `stuck since
boot`). Under xemu (detected by CPUID: no VME) the port plays through a looping
APU voice instead (`XHW_AUDIO_APU`).

`[AUDIO]` lines every 10 s from `audio.c` and the driver: see `docs/testing.md` 5.1.
Costs: `[PERF]` `audio` bucket. The mixer inner loops use MMX inline assembly (`asp_hle.c`; the P3 has MMX, not SSE2, so
LLVM 20+ intrinsics are not used); `-DASP_SCALAR` builds plain C.

Nothing has been heard on the console yet (see `docs/handoff.md`).

## 6. Controllers (`xhw_pad.c`, `xbox/src/game/pad.c`)

### 6.1 Platform (`xhw_pad.c`)

Four ports through nxdk's SDL2 GameController layer, each controller in the
port it is in (the player index is the port). The state is polled, and SDL's
joystick events are switched off and the queue emptied every poll
(`XHW_PAD_DRAIN`): otherwise every stick movement queued an event for good (the
console's memory leak). `xhw_pad_get(port, &pad)` returns buttons, sticks and
triggers; `xhw_pad_rumble` drives both motors; `xhw_pad_shutdown` releases the
controllers before the XBE is left.

### 6.2 N64 mapping (`pad.c`)

Player 1 only is read by the game (ports 2-4 are read and reported). The
mapping is in the file's header; in short: left stick → N64 stick (radial dead
zone from `settings.ini`, default 20%, rescaled so full tilt is ±80); A → A;
X → B; Start, D-pad → the same; Y, B, White, Black, or the right stick past half
tilt → the C buttons; left and right triggers (from 128) → Z and R. The L button has no N64 mapping by default.

Quit and restart combos: **L + R + BACK + BLACK** quits to the dashboard,
**L + R + BACK + WHITE** relaunches the XBE. Both need both triggers past 200
and fire as soon as the buttons are down; they are logged (`[PAD] port N:
L + R + BACK + ...`) and written to `lastexit.txt`. They are checked at every
controller read the game makes, and also from the VI thread (`wx_pad_retrace`)
when the game has not read for 30 retraces, so they work on a stalled game
(`WX_PAD_COMBO_WATCH`). The action runs after the controllers are free
(`act()`), 200 ms later. BACK on the title opens the settings panel.

Rumble (`wx_pad_rumble`, from the crash handler) is sent at the next read, and
only when `rumble = 1` in `settings.ini`.

## 7. Saves, settings and logs (`E:\UDATA\57580001\`)

| file | written by | what |
|---|---|---|
| `settings.ini` | `settings.c` | the port's settings (below). Written with defaults on first boot (or when the file is missing, old or cut), and when the in-game panel saves |
| `pilotwings64.eep` | `eeprom.c` | the cartridge's 0x800-byte EEPROM image in cartridge byte order (what Project64 and Mupen read) |
| `boot.log`, `boot_prev.log`, `boot2.log`, `boot3.log` | `xhw_sys.c` | the log (`docs/testing.md` section 5) |
| `lastexit.txt` | `xhw_main.c` | why the last boot left the XBE (one line); read and deleted at the next boot |
| `crash.log`, `hang.log` | `xhw_crash.c`, `xhw_watchdog.c` | the fault or hang report, symbolised with `tools/xbox/sym.py` |
| `trace.log` | `xhw_sys.c` | `[DRAW]` lines (test builds, `XGFX_TRACE_FRAME`) |
| `shotNN.bmp` | `xhw_fbdump.c` | a screenshot (BACK in test builds; `[SHOT]` lines) |
| `debug.txt` | read by `xhw_debug.c` | test builds: `name = value` switches (`docs/testing.md` 5.2) |

Writes are atomic (`xhw_write_file_atomic`: a temp file, flushed, read back and
compared, then renamed over the old one), so a full E: or a power cut leaves the
last good file. A failed replace is logged (`[FILE]`). The save is written before
`osEepromLongWrite` returns (a DIVERGES: the N64 wrote the chip in the background).

`settings.ini` (`xbox/src/game/settings.c`):

```
[video]  output = auto|480i|480p|720p|1080i   progressive = 0|1
         widescreen = auto|on|off   video_confirmed = <mode>   fps = 0|1
         texture_filter = n64|smooth|sharp   antialiasing = off|2x|4x
[system] screenshots = 0|1
[input]  stick_deadzone = 0..60 (steps of 5)   rumble = 0|1
```

The file ends with `; end of settings`; a file without that line (an older or
cut one) is rewritten whole. Unknown keys are dropped on a rewrite. The in-game panel
edits the same struct and saves it on closing (or "Save and restart").

What is not implemented yet: `video_confirmed` is stored and read, but there is
no "new mode, press A to keep it" confirmation net (no `video_pending` file, no
revert timer), and `widescreen` is stored and shown but does not change the
picture (no horizontal-plus projection, no HUD box). PLAN.md 3.2 and 3.3 describe
both as designed.

## 8. Exit, restart and launch paths (`xhw_main.c`)

Every path that leaves the XBE first writes `lastexit.txt` (`xhw_exit_write`,
the reason set by `xhw_exit_reason`, else the path's default), then stops the
audio and the controllers.

| path | call | what the console does |
|---|---|---|
| quit to the dashboard | `xhw_quit_to_dashboard()` (combo BLACK, title "Exit to dashboard", a card's timeout, `wx_boot` returning) | `XLaunchXBE(NULL)`: the dashboard |
| restart (self) | `xhw_reboot_self()` (combo WHITE, "Save and restart" when a `*` setting changed) | relaunches the XBE by its kernel path (`XLaunchXBE` with the `\Device\` path); if that returns, `HalReturnToFirmware` |
| cold reboot | `xhw_reboot_cold()` | `HalReturnToFirmware(HalRebootRoutine)`: the GPU is reset |
| launch another XBE | `xhw_launch_xbe("F:\\...")` (the autopad `NEXT` event, console rounds) | `XLaunchXBE` with `\Device\Harddisk0\Partition6\...` for F: or `Partition1` for E:; `boot.log` is kept as `boot_<folder>.log` |
| hang | `[WDOG]` report, then a button combo or a reboot | no path writes `lastexit.txt`; the next boot normally logs `not recorded` |

The next boot logs `[BOOT] previous exit: <reason>`, or `not recorded (switched
off, reset or killed)` when the file is missing. The ROM-failure cards write
`ROM check failed (<reason>)`.

## 9. Diagnostics

- `xhw_crash.c`: an exception on any thread is caught by an SEH record and
  written to `crash.log` and the screen; the thread is parked. `XHW_CRASH_GUARD=0`
  off.
- `xhw_code_repair.c`: at boot, every `.text` byte that differs from the file is put back
  (`XHW_CODE_REPAIR=1`; a big difference means the wrong file and is only logged;
  `2` checks only). This answers the modchip rewrite of `rdtsc` into `int 2Eh`
  (OpenCrossing issue #3).
- `xhw_prof.c` (`-DXHW_PROF=1`): the sampler; `[PROF]` lines.
- `xhw_perf.c`: `[PERF]` every 5 s (on in every build).
- `xhw_fbdump.c`: `[FBDUMP]` frames over COM1 (xemu) and `shotNN.bmp` on E:.

## 10. What is not here yet

- Widescreen (horizontal-plus, the HUD box) and the 1080i/720p confirmation net
  (section 7). The modes themselves work.
- `prof.bin` (named by `console.py pull`) has no writer in the tree.
- `XBOX_PGO` is passed by the wrappers and read by nothing (PGO is milestone M9).
- The no-ROM card's example paths name `E:\Games\...`, but the game reads the ROM
  from `D:\` (the XBE's folder). The card text should say `D:\` or the XBE's
  folder; this document follows the code.
