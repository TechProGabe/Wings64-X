# Handoff

Newest first. Each milestone entry says what was verified and how; "xemu"
results are emulator-only (no timing, audio or memory claims from them).

## M5 — Real 60 fps: measured, rate patches 038-045, the Frame rate row (2026-10-15)

Worktree branch off `port` (367d94b); nothing pushed. All run results below are xemu-only.

- **Measured first** (PLAN.md M5 risk note). The game is delta-time driven: `func_80313D74` takes
  `D_8034F854` from the CPU counter between two `uvGfxEnd`s, clamped [0.01, 0.1] s, and physics, cameras,
  timers and the world integrate with it. `_uvScCreateScheduler` asks for a retrace message every field
  (`numFields` 1) and `_uvScRunGfx` starts a display list once the last swap latched: there is no
  "every other retrace" anywhere, the port has drawn up to 60 since M3 (xemu: 45-52 fps in menus,
  36-50 in flight; emulator-bound, not a console figure). So the game's speed was already right at 60;
  what differs is code that steps once per frame. Birdman64's audit (`docs/notes/framerate.md`) lists
  those; the ones that matter at a 60 cap are imported as patches 038-045 (table in patches/README.md,
  each `.md` says what it scales and what was left out). Game seconds run 2.46 % fast against the wall
  clock on the N64 and here alike (46.875 MHz counter, 45.75 MHz in `uv_clocks.h`): measured 2.5-2.7 %
  in xemu at both rates.
- **Helpers**: `xbox/include/game/wings64_rate.h`, `xbox/src/game/rate.c` (own `powf`, no libm);
  `WX_REF_DT` = 0.034152 s (a 30 fps N64 frame on the game's clock). Host test `tools/xbox/test_rate.py`
  (144 checks: powf vs libm, decay/step/ticks/approach per second equal at 20-60 Hz).
- **Frame rate row** (Port settings, "Frame rate 60 / 30", `[video] fps_mode`, default 60, applied at
  once): at 30 `os_shim.c` `vi_thread` latches a new swap 2 retraces after the last, so display lists
  start at most every other retrace; the scheduler and audio still get every retrace. Test builds:
  `env MX_FPS_MODE=30|60`. `[RATE]` lines (test builds, `boot.c`): wall/game time, dt, scale, HUD timer,
  altitude, speed, position, once a second in flight/results/demos.
- **Kill switches**: `-DWX_RATE=0` (038-045 compute the N64's per-frame steps), `-DWX_FPS_MODE_DEFAULT=30`.
- **Verified (xemu, slots 2 and 4, scenarios in a scratch dir)**: hang glider class A test 1 with no
  input in flight, at 60 (three runs, two builds) and 30: the glide matches to ~2 cm in position at the
  same HUD time through 13 s; the crash comes at HUD time 20.315/20.316/20.316 s (60) and 20.329 s (30),
  under a 30 fps frame apart; `WX_RATE=0` at 60 gives the same 20.316 (nothing in this flight hits a
  patched step). PERF: ~28-30 fps at 30, 40-52 at 60. The panel row toggles 60 -> 30 -> 60 live (PERF
  follows, shots of the panel at 10 rows). Attract demos (states 8/9/5) play to the end and return to
  the title at 60 and 30; their poses at the same demo time agree within a frame's travel. Release and
  test builds clean; `rom_leak_check.py` clean.
- **Not verified**: anything that exercises the patched steps (birdman flaps, gyrocopter trim, JH, a HG
  ground scrape, the replay's length) side by side at 30 and 60 -- the autopad counts presented frames,
  so the same script lands in different places at 30 and 60 (menus' timed fades take fewer frames at
  30: s60's last menu press landed in flight at 30 and pitched the glider up; that was the only
  "divergence" seen). The console (feel per vehicle, a locked 60 in flight: M9's job). The PLAN's
  `XGFX_FDR_HASH` replay oracle is not built: the `[RATE]` positions were used instead.
- **Seen once** (1 run in ~14, 30 fps, a diagnostic build): at the pilot-select -> test transition the
  game thread sat in `uvaSeqStop`'s 2 s spin for minutes while the audio thread (level 14) was busy in
  `alAudioFrame` / `__CSPVoiceHandler`, starving it (WDOG dumps; symbolized with `sym.py`). Not
  reproduced on a rerun; the 30 fps cap does not touch audio, so likely an older audio/priority race
  (`WX_AUDIO_THREAD_LEVEL`). Open.

## M7 (part, follow-up) — "Port settings" in Option, title menu at the bottom (2026-10-15)

Branch `port`; nothing pushed. Maintainer's call: the panel is "Port settings" in the game's Option menu
(patches/036, above "Return"); the title menu is "Start game", "Option", "Exit to dashboard" at the game's
font size, in a box at the bottom of the screen (patches/035), where the copyright line was (not drawn,
`WINGS64_TITLE_COPYRIGHT=1` brings it back); BACK on the title still opens the panel (patches/034).
Kill switch `WINGS64_SETTINGS_IN_OPTIONS=0` (the four-item title of the entry below). Verified in xemu
by shots: the title (three items, faces clear), the Option menu with "Port settings", the panel from it
and back to the Option menu, and "Exit to dashboard" (`[TITLE] Exit to dashboard`). Not checked: the
five-item Option menu of a completed game (no save that has it).

## M7 (part) — Settings menu, "Exit to dashboard", BACK on the title, combos in every state (2026-10-15)

Worktree branch off `port` (5888cf6); nothing pushed. All picture and
behaviour results below are xemu-only (no console yet).

- **Settings panel** (`xbox/src/game/settings_menu.c`, wings64-dc's
  `settings.c` re-targeted; `ultra/hooks.c` stubs removed;
  `WINGS64_SETTINGS_MENU` now defaults to 1). The Option screen's level,
  backdrop, cursor, chime and sounds; rows through `menuUtil*` with our own
  item callback (menu.c's wrapper holds 5 items, the panel has 6 in test
  builds). Rows: Video output `*` (Auto/480i/480p/720p/1080i, shown "Auto,
  720p" / "480p > 480i" from the dashboard's allowance; font 6 draws `>` as
  a double chevron), FPS counter, Screenshots (test builds), Stick dead zone
  (0-60 by 5, D-pad repeat by retrace count, live "Stick N" readout; font 6
  has no `%`), Save and restart (relaunches when a `*` row differs from the
  boot value, otherwise as Return), Return (and B): `settings.ini` saved
  when something changed. Legend "* After a restart" (yellow while a
  restart is pending). Widescreen and Buttons rows omitted until their
  features exist.
- **FPS counter** (maintainer via the lead): the row switches the renderer's
  counter (`nv2a.c` `fps_overlay`, identical to Melee-X's: presented frames
  over 0.5 s of the ACPI clock, 5x7 yellow at 2x on black, top-left at 1/16
  of the framebuffer); key `[video] fps` (Melee-X's name), default on in
  test builds only; applied after `xgfx_init` and at once on a toggle
  (`wx_settings_apply_fps`). wings64-dc's game-font counter stays behind
  `-DWX_FPS_GAME_FONT=1`.
- **"Exit to dashboard"** (`patches/031`, `WINGS64_TITLE_EXIT`, default 1):
  fourth title item, the box grows down two rows and right to fit; writes
  lastexit.txt and quits. No confirm prompt (PLAN.md asks for none).
- **BACK on the title** opens the panel (`patches/034`; ignored while a
  trigger is held, so the combos don't open it).
- **Combos (D8)** were right (any port, both triggers > 200 as Melee-X,
  fire at once, logged, lastexit.txt) but only checked when the game read
  the controllers. Now also from the VI thread when the game has not read
  for 30 retraces (a try-acquire flag keeps the threads out of SDL together;
  the VI thread never waits). `-DWX_PAD_COMBO_WATCH=0` turns that off;
  `-DWX_PAD_COMBO_VI_ONLY=1` is a test switch that leaves only the VI path.
- `settings.c`: the "; end of settings" check ran after `parse()` had cut
  the text up, so `settings.ini` was rewritten on every boot; fixed.
- Verified (xemu, slot 2, autopad scenarios kept outside the repo): title
  with four items in the box; Settings and BACK open the panel; FPS
  counter toggles off/on and the counter appears/disappears; dead zone
  25/30 and the readout (Stick 100 with the stick held); video Auto -> 480i
  -> 480p shows "480p > 480i", Save and restart relaunched and the next boot
  read output 480p and logged "previous exit: restart from the settings
  menu"; Exit item: `[TITLE] Exit to dashboard`, the next boot logged
  "previous exit: exit to the dashboard from the title menu"; L+R+BACK+WHITE
  on the title restarted (normal build) and, with `WX_PAD_COMBO_VI_ONLY=1`,
  WHITE restarted and BLACK quit from the panel via the VI thread;
  `WINGS64_TITLE_EXIT=0` shows three items, `WINGS64_SETTINGS_MENU=0` the
  game's two; release and test builds clean; `rom_leak_check.py` clean.
- Not verified: the console (pad feel, dead-zone repeat rate, 720p/1080i
  rows on an HDTV pack, the dashboard after Exit); the release build's panel
  (no autopad); a real level load with the VI combo path (exercised through
  the test switch instead). Open: picking 1080i/720p in the panel has no
  confirm-or-revert yet (PLAN.md 3.3 net 3); BACK at boot is the way back.

## M6 (first light) + M3 items — audio, ThinLTO default, static-RAM copies (2026-10-15)

Branch `port`; nothing pushed. xemu-only results.

- Audio wired (PLAN.md D6): `xbox/src/game/audio.c` runs each audio task's
  Acmd list through `asp_hle_run` on the game's audio thread (identity
  addresses: `asp_hle.c` builds with `ASP_HLE_SEGMENTS 0` on the console,
  patches/029), installs the ROM's A_RESAMPLE table (US 0x51C30) at the first
  task, and hands each AI buffer to `xbox/src/hw/xhw_audio.c` (Melee-X's AC97
  polled driver + xemu APU voice, as is but for the comment, the 22050 Hz
  default and `xhw_audio_set_rate`). `[AUDIO]` line every 10 s: tasks, us per
  task, buffers, frames in, ring drops, peak, AI underruns, bad addresses,
  unknown commands. Kill switch `-DWX_AUDIO=0`.
- The audio thread had been dying since M3 (unnoticed: the game runs on
  silently): `uvLevelInit` copied the ROM's COMM block into the 36-byte
  `gUVBlockCounts` with the block's length and overwrote `alGlobals` right
  behind it; `alAudioFrame` then read NULL. patches/033 clamps the copy.
  Found from the crash report symbolized with `tools/xbox/sym.py` (its
  default map name fixed to `wings64_x.map`).
- Verified (xemu, 90 s, menus): ~59 audio tasks/s, ~21.5k frames/s in (22050
  Hz), peak ~24000 (signal, not silence), 0 ring drops, 0 bad addresses, 0
  unknown commands, 3-7 AI underruns (start-up); xemu APU voice path. Not
  verified: what it sounds like (nobody listened yet), the AC97 path
  (hardware), the 10-minute soak.
- patches/032: the three `_uvMediaCopy` calls with a static-data source
  (save mirror x2, Birdman take-off pad) are RAM copies on the Xbox.
- ThinLTO is the default build (`XBOX_LTO=0` for plain objects; PLAN.md D3).
  Verified: the LTO build reaches the title, menus, flight and the results in
  xemu with the same pictures as the plain build (shots compared by eye).
- Saves: not checked (the maintainer will on hardware). `file_load: checksum
  failure` repeats in menus: the game loads both slots and an empty slot
  fails the checksum before its magic check (N64 behaviour, by reading).

## M3 (in progress) — in-game picture fixes from the first watched sessions (2026-10-15)

Branch `port`; nothing pushed. The game boots to the title, the menus and
flight (commit a3bc3cf); the maintainer watched xemu while an autopad
scenario played (`tools/xbox/scenarios/autopad.txt`, frames = presented
frames) and reported issues. All results below are xemu-only.

- Freeze at the start of the glider: nxdk's `lrintf` is a stub that asserts;
  `pad.c` rounds by hand. Verified: the map no longer references `_lrintf`,
  flight runs.
- Black fades (`patches/026`'s hook was a no-op): `f3d_interp.c` `hold_frame`
  + `nv2a.c` `xgfx_capture_last` keep the last presented picture under each
  fade frame. `-DXGFX_HOLD_LAST=0`. Verified by log: the 2-triangle fade
  frames over a cleared screen are gone at every menu change.
- Whole-screen flashes in flight ("white / another colour / an older frame"):
  pbkit sometimes retires a queued flip without writing PCRTC (its DPC runs a
  vblank the ISR did not see), so the screen stays on an older buffer, which
  is the next back buffer; the old 4-vblank guard then drew the next frame on
  the buffer being shown. Now the present waits for the flip
  (`XGFX_FLIP_WAIT`) and, with the GPU idle for 2 vblanks, puts the newest
  finished frame on screen itself (`XGFX_FLIP_REPAIR`; the `[NV2A]` stats line
  counts "flip waits (N repaired)": 3-21 per 600 frames in xemu). Found with a
  display-order probe (test builds, `nv2a_vbl_probe` from `xhw_wait_vblank`):
  before, the vblank showed the buffer being drawn every few seconds; after,
  0 times through menus and flight. Maintainer: the flashing is gone.
  Hardware must re-check (the race depends on interrupt/DPC latency).
- Terrain pop-in at the screen edges on fast turns: the interpreter rejected
  triangles beyond the N64 viewport (+-w), but the port draws past the
  SUBSCREEN viewport to the screen edges (18 px at the bottom), so big
  terrain triangles in that band vanished. Rejection is now at 1.2 w
  (`XGFX_REJECT_RATIO`; found by a Fable consult). `patches/030` widens the
  game's culling frustum by 1.25 (`WINGS64_CULL_WIDEN`) for the same band;
  alone it did not fix the pop-in. Maintainer: pop-in fixed.
- L + R + BACK + WHITE restarts the XBE (`pad.c`, `xhw_reboot_self`).
- Test-build logging: `[GFX] thin frame` / `odd frame` (frames that drew next
  to nothing / far less than the one before), `[NV2A] N frames with fewer
  than 3 draws`.
- Open: saves persist across a reboot (not checked); ThinLTO as the default;
  texture churn (tens of MB uploaded per 600 frames in menus); the static-RAM
  `_uvMediaCopy` sources in `save.c`/`task.c`; audio (M6); widescreen will
  need `XGFX_REJECT_RATIO` and `WINGS64_CULL_WIDEN` to follow the aspect.

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
