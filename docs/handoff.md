# Handoff

Newest first. Each milestone entry says what was verified and how; "xemu"
results are emulator-only (no timing, audio or memory claims from them).

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
