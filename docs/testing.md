# Testing

Five tiers, cheapest first (PLAN.md D11): host tests without a ROM, a build
check, the xemu runner with autopad scenarios, the xemu screenshots, and the
real console. "xemu" results are emulator-only: no claim about timing, audio
or memory comes from them (CLAUDE.md). Console results are the only truth for
those.

Build and run commands are in CLAUDE.md. This file has the switch table, the
test-build tools and the log lines. For the platform code they read, see
`docs/platform.md`; for the build itself, `docs/toolchain.md`.

## 1. Host tests (no ROM, no Xbox)

| script | what it checks | sources |
|---|---|---|
| `tools/xbox/test_rom_check.py` | ROM verdicts and messages: size, byte order (z64/v64/n64 identical after normalising), header, game code, region, SHA-1 vectors | `tests/xbox/test_rom_check.c` + `xbox/src/hw/xhw_rom.c` (`-DXROM_HOST_TEST`) + `xhw_sha1.c` |
| `tools/xbox/test_asp_hle.py` | the RSP audio HLE against a reference decode; builds both the SSE path and `-DASP_SCALAR` and requires identical output | `tests/xbox/test_asp_hle.c`, `asp_ref.c` + `xbox/src/hw/asp_hle.c` |
| `tools/xbox/test_cc.py` | the N64 combiner compiled to register combiners (`nv2a_cc.c`) against a software model of the N64 combiner | `tests/xbox/test_cc.c`, `cc_ref.c` + `nv2a_cc.c` |
| `tools/xbox/test_texconv.py` | TMEM deswizzle and format conversion (`texconv.c` is pure C) | `tests/xbox/test_texconv.c` + `xbox/src/gfx/texconv.c` |
| `tools/xbox/rom_leak_check.py` | tree check: no ROM material in `git ls-files` (ROM header bytes, `.z64/.n64/.v64/.eep` files) | the tracked tree |

The host tests use the first of `$CC`, `clang`, `gcc`, `cc` on PATH (the MSYS2
copies under `/c/msys64/mingw64/bin` as a fallback). `rom_leak_check.py` must be
clean before any commit.

## 2. Build checks

- `tools/xbox/msys/build.sh` (Windows) or `tools/xbox/docker/build.sh` (Docker,
  image `wings64-x:sdk`) produce `build-xbox/xbe/default.xbe` and `default.tbn`.
- `XBOX_CFLAGS="-DXHW_AUTOPAD=1" tools/xbox/msys/build.sh` is a test build. Any
  switch in section 3 can go in `XBOX_CFLAGS`; it is reset on every run, so a
  debug knob never sticks in the CMake cache (`xbox/build.sh`).
- A release build has no test tooling: `XHW_TEST_BUILD` is 0 (section 3), so no
  autopad, no `[PAD] port 1 buttons` line, no screenshot button, no `debug.txt`.
- `XBOX_LTO=0` builds plain objects for diagnosis; LTO is the default.

## 3. Build switches

Every compile-time switch in `xbox/` and in the patch notes (`patches/*.md`),
found by `grep '#\s*ifndef\s+\(XGFX_\|WX_\|XHW_\|WINGS64_\|ASP_HLE_\)'` and the
`#if`/`#ifdef` uses of the same names. "Default" is the value when no `-D` is
given; "undefined" means the code is compiled only when `-D` names it.
`-DNAME=0` turns a default-on switch off; `-DNAME=<value>` sets a value.

### 3.1 Test-build and diagnostic switches (xbox/src/hw)

| switch | default | meaning | file |
|---|---|---|---|
| `XHW_TEST_BUILD` | derived: 1 if `XHW_AUTOPAD` or `XHW_PROF` is nonzero, else 0 | the test-build flag. Enables the screenshot button (BACK), `[PAD] port 1 buttons` lines, `debug.txt`, `[DRAW]`/`[GFX]` extras, `MX_*` env hooks, the `[OS] stall` watch, and makes `XHW_FPS_DEFAULT` 1 | `xbox/include/xhw.h` |
| `XHW_AUTOPAD` | 0 | scripted input from `D:\autopad.txt`, `env` lines, `SHOT`/`TSHOT`/`NEXT` events (section 4.1) | `xhw_autopad.c` (also read by `xhw_video.c`) |
| `XHW_PROF` | 0 | sampling profiler on the game's threads, `[PROF]`/`[PROFT]` lines (section 5.2) | `xhw_prof.c` |
| `XHW_PROF_SECS` | 10 | seconds between profiler reports | `xhw_prof.c` |
| `XHW_PERF` | 1 | the `[PERF]` frame-budget line (section 5.1); 0 removes the line, the adds stay | `xhw_perf.c` |
| `XHW_PERF_SECS` | 5 | seconds per `[PERF]` line | `xhw_perf.c` |
| `XHW_WATCHDOG` | 1 | hang dumper thread (`[WDOG]`, `hang.log`); 0 off | `xhw_watchdog.c` |
| `XHW_WATCHDOG_SECS` | 6 | frames stopped this long (after boot) → hang report | `xhw_watchdog.c` |
| `XHW_WATCHDOG_BOOT_SECS` | 45 | no frame this long after boot → hang report | `xhw_watchdog.c` |
| `XHW_WATCHDOG_PRESENT_SECS` | 10 | retraces run on but nothing presented this long → logged; after a minute also on screen | `xhw_watchdog.c` |
| `XHW_HEARTBEAT_SECS` | 5 | `[BEAT]` line period; 0 disables | `xhw_watchdog.c` |
| `XHW_CRASH_GUARD` | 1 | SEH guard on each thread: `crash.log` and screen on a fault; 0 off | `xhw_crash.c` |
| `XHW_CODE_REPAIR` | 1 | puts back `.text` bytes that differ from the file (`0f 31` → `cd 2e` rewrites); 2 checks and logs only; 0 not built | `xhw_code_repair.c` |
| `XHW_CODE_REPAIR_TEST` | undefined | test builds only: applies the rewrite ourselves at one site, to see the repair work | `xhw_code_repair.c` |
| `XHW_RAM_LOCK64` | 1 | hold RAM above 64 MB on a 128 MB console (PLAN.md D2); 0 for diagnosis only | `xhw_ramlock.c` |
| `XHW_CLOCK_TSC` | 1 | read the TSC once at boot against the ACPI timer for `[CLOCK] cpu N MHz`; 0 skips | `xhw_clock.c` |
| `XHW_VIDEO_1080I` | 0 | 1: offer 1080i (Auto picks it when the dashboard allows). Off since console run 15 (frame rate cost); 720p is the top mode | `xbox/include/xhw.h` |
| `XHW_VBLANK_PACE` | 1 | frame pacing from pbkit's vblank when the output is 60 Hz; 0 always the ACPI pacer at 60.000 Hz | `xhw_video.c` |
| `XHW_SPLASH_MS` | 2000 | splash hold time | `xhw_splash.c` |
| `XHW_NO_SPLASH` | undefined | defined: no splash at all | `xhw_splash.c` |
| `XHW_SPLASH_DUMP` | undefined | writes the splash frame to the log as `[FBDUMP]` | `xhw_splash.c` |
| `XHW_PAD_DRAIN` | 1 | empty SDL's joystick event queue every poll (the console memory leak fix); 0 restores the old growth | `xhw_pad.c` |
| `XHW_AUDIO_PUMP_LEVEL` | 14 | kernel level of the AC97 pump thread; 0 uses Melee-X's level (10) | `xhw_audio.c` |
| `XHW_AUDIO_MAX_MS` | 200 | latency cap: above it the oldest samples are skipped; 0 no cap | `xhw_audio.c` |
| `XHW_AUDIO_TARGET_MS` | 80 | where the skip stops | `xhw_audio.c` |
| `XHW_AUDIO_APU` | 1 | xemu's MCPX APU voice when the CPU has no VME (xemu); 0 AC97 only | `xhw_audio.c` |
| `XHW_AUDIO_TEST` | 0 | test bits: 1 leaves the engine running into the next boot; 2 reads CIV as 0 (an engine stuck from boot on) | `xhw_audio.c` |
| `XHW_FBDUMP_EVERY` | 0 | a `[FBDUMP]` frame every N presents; 0 off | `nv2a.c` |

`XHW_PERF_*` and `XHW_BTN_*`, `XHW_OUT_*`, `XHW_REPLACE_*` are enumerators and
constants in `xhw.h`, not switches. `XHW_UDATA_ROOT` (`E:\UDATA\57580001`) is
the save folder, a constant in `xhw_internal.h`.

### 3.2 Renderer switches (xbox/src/hw/nv2a.c, xbox/src/gfx/f3d_interp.c)

| switch | default | meaning | file |
|---|---|---|---|
| `XGFX_OVERLAP` | 1 | CPU/GPU overlap: the wait for the GPU moves to the next frame's first use; 0 waits in present | `nv2a.c` |
| `XGFX_SHADOW_MARK` | 1 | shadow-volume mark pass: 1 colour on, blended; 0 colour writes off. Also settable at run time through `debug.txt` `shadow_mark` | `nv2a.c` |
| `XGFX_POLY_SLOPE` | 1.0 | polygon offset slope factor for decal and tie-break draws; 0 bias only | `nv2a.c` |
| `XGFX_FLIP_REPAIR` | 1 | put the newest finished frame on screen when pbkit lost a flip; 0 leaves the old buffer up to the 1 s cap | `nv2a.c` |
| `XGFX_FLIP_WAIT` | 1 | the present waits for the flip; 0 the old guard (4 vblanks, then draws on the shown buffer) | `nv2a.c` |
| `XGFX_PB_KICK` | 8192 | pushbuffer words per kick | `nv2a.c` |
| `XGFX_VB_CACHE_BREAK` | 1 | `BREAK_VERTEX_BUFFER_CACHE` at each batch start | `nv2a.c` |
| `XGFX_TILE` | 12 | tile-region value: 4 enables tile region 0 (pbkit's base); 8 turns Z compression off. Default 12 = both (stencil shadows looked wrong on the console with compression) | `nv2a.c` |
| `XGFX_Z24_16BPP` | 1 | Z24S8 with 16-bit colour at 720p/1080i; 0 Z16 | `nv2a.c` |
| `XGFX_ZETA_GUARD` | 1 | resend the zeta pitch around depth clears where it differs from the colour's | `nv2a.c` |
| `XGFX_STATS_EVERY` | 600 | `[NV2A]` stats period in frames | `nv2a.c` |
| `XGFX_TEX_POOL_KB` | undefined (8 MB at 480, 6 MB at 720p/1080i) | texture pool size in KB for both modes | `nv2a.c` |
| `XGFX_FAR_CLIP` | 1 | clip at the far plane (z <= w) | `f3d_interp.c` |
| `XGFX_OVERLAY_FULL` | 1 | SUBSCREEN-sized untextured no-Z draws stretched to the screen edges | `f3d_interp.c` |
| `XGFX_TITLE_FIT` | 1 | the title's SUBSCREEN picture fitted with a black margin | `f3d_interp.c` |
| `XGFX_SCISSOR` | 1 | small scissors and viewports honoured (album thumbnails) | `f3d_interp.c` |
| `XGFX_Z_REL` | 1 | tie-break and decal bias as a fraction of distance; 0 polygon-offset units | `f3d_interp.c` |
| `XGFX_Z_TIEBREAK` | 1 | later coplanar draws win depth ties (the RDP's rule) | `f3d_interp.c` |
| `XGFX_SHADOW_VOL` | 1 | shadow volumes through the stencil; 0 both passes dropped | `f3d_interp.c` |
| `XGFX_TEX_MEMO` | 1 | per-frame (load address, tile key) texture memo | `f3d_interp.c` |
| `XGFX_DXT_EXACT` | 1 | LOADBLOCK `dxt` rule (the RDP's odd-line rule) | `f3d_interp.c` |
| `XGFX_REJECT_RATIO` | 1.2 | triangles are rejected beyond this many w of the N64 viewport | `f3d_interp.c` |
| `XGFX_HOLD_LAST` | 1 | fades draw over the last finished frame (patch 026's freeze); 0 over a clear | `f3d_interp.c` |
| `XGFX_FB_PERSIST` | 1 | the album's persistent frame (patch 028); 0 the colour buffer alone | `f3d_interp.c` |
| `XGFX_HOLD_GPU` | 1 | the held frame is captured on the GPU; 0 the CPU reads it back (about 244 ms a fade on the console) | `f3d_interp.c` |
| `XGFX_TRACE_FRAME` | 0 | frame n's draws as `[DRAW]` lines (`trace.log`); 0 off | `f3d_interp.c` |
| `XGFX_TRACE_DUMP_AT` | undefined | with `XGFX_TRACE_FRAME`: the picture after the n-th draw of the trace frame | `f3d_interp.c` |
| `XGFX_DEBUG_NOZ` | undefined | debug: depth test and write off | `f3d_interp.c` |
| `XGFX_DEBUG_ZHALF` | undefined | debug: depth values halved | `f3d_interp.c` |
| `XGFX_DEBUG_NOCULL` | undefined | debug: no face culling | `f3d_interp.c` |
| `XGFX_DEBUG_NOCLIPCULL` | undefined | debug: no clip-stage culling | `f3d_interp.c` |
| `XGFX_SELFTEST` | undefined | `=1`: the back end on its own (`xgfx_selftest.c`), instead of the game; `[SELFTEST]` lines, `[FBDUMP]` of frames 30 and 90 | `boot.c`, `xgfx_selftest.c` |
| `XBUILTIN_OFF` | undefined | not defined: `xbuiltin.h` makes constant-size `mem*` and `sqrtf` inline; `xhw_string.c` defines it | `xbox/include/xbuiltin.h` |

`XGFX_MIPS` appears only in a comment (the Phase B mip option); `XGX_*` appear
only in comments copied from Melee-X. Neither has code.

### 3.3 Audio switches (xbox/src/hw/asp_hle.c, xbox/src/game/audio.c, ultra/os_shim.c)

| switch | default | meaning | file |
|---|---|---|---|
| `ASP_HLE_SEGMENTS` | 0 on the Xbox (`XBOX` defined), 1 otherwise | address words resolve through the segment table; 0 they are host pointers (patch 029) | `asp_hle.c` |
| `ASP_HLE_ENV_FASTPATH` | 1 | ENVMIXER skips gain recomputation when no ramp runs; 0 always recomputes | `asp_hle.c` |
| `ASP_HLE_RESAMPLE_PER_TAP` | 0 | each tap rounded like a lone vmulf (sm64-port's reading); scalar only | `asp_hle.c` |
| `ASP_SCALAR` | undefined | plain C only, no inline MMX; the test builds both | `asp_hle.c` |
| `WX_AUDIO` | 1 | run audio lists through the HLE; 0 count and drop them (silent) | `xbox/src/game/audio.c` |
| `WX_AUDIO_THREAD_LEVEL` | 14 | kernel level of the audio manager thread; 0 keeps PW64's order (level 10) | `ultra/os_shim.c` |
| `WX_VI_RESERVE` | 2 | free scheduler slots the retrace message leaves; 0 always sends | `ultra/os_shim.c` |

`ASP_MMX` is derived (1 when GNU-style inline asm is available and `ASP_SCALAR`
is not defined); it is not a switch to set.

### 3.4 Port and game switches (xbox/src/game, xbox/include/game, patches)

| switch | default | meaning | file |
|---|---|---|---|
| `WINGS64_XBOX` | 1 (set by the prelude) | the guard every patch hunk tests: `#ifdef WINGS64_XBOX` = the port, `#else` = the original | `xbox/include/game/wings64_prelude.h`; patches |
| `WINGS64_SETTINGS_MENU` | 1 | the port's settings panel and the title hooks (patch 021, 031, 034); 0 compiles them out | `wings64_settings.h` |
| `WINGS64_TITLE_EXIT` | 1 | "Exit to dashboard" as the title's fourth item (patch 031) | `wings64_settings.h` |
| `WINGS64_SETTINGS_IN_OPTIONS` | 1 | the panel ("Port settings") and Exit are Option-menu items (patch 036); 0 the title's own "Settings" item (patch 031) | `wings64_settings.h` |
| `WINGS64_TITLE_COPYRIGHT` | 0 | keeps the title's copyright line and the box at its old place (patch 035) | `wings64_settings.h` |
| `WINGS64_TITLE_MENU_BOTTOM` | 40 | the title box's bottom edge, in pixels up from the screen's (patch 035) | `wings64_settings.h` |
| `WINGS64_FB_FREEZE` | 1 | patch 026's frozen frame under the fades (`#if` in the hunk) | `wings64_fbfreeze.h` |
| `WINGS64_FB_PERSIST` | 1 | patch 028's persistent frame for the album (`#if` in the hunk) | `wings64_fbfreeze.h` |
| `WINGS64_CULL_WIDEN` | 1.25f | the culling frustum's widening factor (patch 030); 1.0f is the N64's frustum | `patches/030-port-cull-frustum-widen.patch` (`#if !defined`) |
| `WINGS64_DBG_LOOK_AT_PAD` | undefined (off) | patch 027: the flight camera looks at the first landing pad from this many units (100 = LOD0, 300 = LOD1). Debug only | `patches/027-debug-look-at-pad.patch` |
| `WX_FPS_GAME_FONT` | 0 | wings64-dc's game-font FPS counter instead of the renderer's | `settings.c`, `settings_menu.c` |
| `WX_RUMBLE` | 1 | rumble on a crash (patch 037); 0 the hook does nothing | `ultra/hooks.c` |
| `WX_PAD_COMBO_WATCH` | 1 | the L+R+BACK combos also from the VI thread when the game has not read for 30 retraces; 0 only at the game's reads | `game/pad.c` |
| `WX_PAD_CBUTTONS_DPAD` | 1 | the D-pad (and the right stick) as the C buttons (0: C on the right stick and Y/B/White/Black, the D-pad as the N64 D-pad) | `game/pad.c` |
| `WX_PAD_COMBO_VI_ONLY` | 0 | test: the combos only from the VI thread, every 8 retraces | `game/pad.c` |
| `XHW_FPS_DEFAULT` | `XHW_TEST_BUILD` | the FPS counter's default (`[video] fps`) | `xbox/src/game/settings.c` |
| `XHW_LOG_BOOT_UNTIL_FRAME` | 1 | boot.log flushes every line only until the first presented frame (0: for the first 600 retraces, which reached into the title and hitched it) | `xbox/src/hw/xhw_sys.c` |
| `XGFX_TILE_MISMATCH_OFF` | 1 | a tile region whose pitch differs from its surface's (1080i: pbkit rounds the tile pitch, not the surface's) is switched off with its Z compression (0: left as pbkit set it; the console faulted on the first clear) | `xbox/src/hw/nv2a.c` |
| `XGFX_SIDE_CLEAR` | 1 | the pillarbox strips outside the 4:3 box cleared to black at the start of every frame (0: not; at 720p leftovers showed as a line in the right strip) | `xbox/src/gfx/f3d_interp.c` |
| `XGFX_CLEAR_FULL_CLIP` | 1 | every clear opens the window clip to the whole surface first (0: the last draw's scissor clipped clears; it cut the title margin's last column, a line at 720p) | `xbox/src/hw/nv2a.c` |
| `WX_SAVE_FILL` | 1 | Port settings > System > "Complete save": writes File 2 with every event at full points (0: the row is gone) | `xbox/include/game/wings64_settings.h`, `xbox/src/game/settings_menu.c` |
| `WINGS64_TITLE_TEXT_DROP` | 6 | the title menu rows moved down inside their box, centred (0: the game's margins) | `xbox/include/game/wings64_settings.h` |
| `WX_HAVE_GAME` | derived (CMake: decomp present) | links the game objects | `xbox/CMakeLists.txt` |
| `WX_MIO0_H`, `XGFX_H`, `XHW_H`, `ASP_HLE_H`, ... | header guards | not switches | |

`WINGS64_DC` and `WINGS64_SCISSOR` are named in patch notes (wings64-dc's
guards, renamed or not carried); the tree has no `#ifdef` on them.

### 3.5 Host-test and tool defines

| define | set by | meaning |
|---|---|---|
| `XROM_HOST_TEST` | `test_rom_check.py` | compiles the pure ROM functions of `xhw_rom.c` on the host; the Xbox half is excluded |
| `XHW_TEST_BUILD` | `xbox/include/xhw.h` (derived) | see 3.1 |

### 3.6 Build environment knobs (not C)

| variable | read by | meaning |
|---|---|---|
| `XBOX_CFLAGS` | `xbox/build.sh` (CMake `CMAKE_C_FLAGS`), the wrappers | extra C flags for every unit; reset each run |
| `XBOX_LTO` | `xbox/build.sh`, `CMakeLists.txt` | `0` plain objects; anything else (default) ThinLTO over game code |
| `XBOX_LTO_INDEX` | `thinlto_link.py` | index flags, default `-mllvm:-import-instr-limit=10` |
| `XBOX_LTO_BACKEND` | `thinlto_link.py` | extra backend flags |
| `XBOX_GAME_CFLAGS`, `XBOX_GAME_OUT`, `XBOX_FORCE` | `compile_game.py` | game flags (CMake passes `XBOX_CFLAGS` through), object folder, rebuild all |
| `XBOX_CMAKE_ARGS`, `XBOX_NINJA_ARGS` | `xbox/build.sh` | extra arguments for cmake and ninja |
| `XBOX_NO_ICON` | `msys/build.sh`, `docker/build.sh` | skip the title image step |
| `XBOX_KEEP_TEMPS`, `XBOX_PGO` | the wrappers pass them through | no reader in the tree yet (PGO is milestone M9) |
| `NXDK_DIR`, `LLVM` | the wrappers and `compile_game.py` | the nxdk checkout and LLVM 21 |

The `XBOX_CFLAGS` values of the test builds, e.g. `-DXHW_AUTOPAD=1`, go to both
triples (the game-side flags come from `XBOX_GAME_CFLAGS`).

## 4. Test-build tools

### 4.1 Autopad scenarios (`xbox/src/hw/xhw_autopad.c`)

Built only with `-DXHW_AUTOPAD=1`. At boot the port reads `D:\autopad.txt` (put
it in the folder `MX_STAGE_EXTRA` names; `xemu_run.sh` packs it onto the disc).
Port 1's input comes from the script, merged over the real controller. Shipped
example: `tools/xbox/scenarios/menus/autopad.txt`.

One event per line:

```
<frame> <token>... [for <frames>]     # comment
```

- `<frame>` counts **presented** frames (the `[BEAT]` "presented" count), not
  retraces: a screenshot stalls the game, not the presented count. Events are
  checked against the presented count, so a scenario replays at the same game frame.
- A press lasts 6 frames unless `for N` says otherwise.
- Button tokens (Duke names): `A B X Y START BACK WHITE BLACK UP DOWN LEFT RIGHT`.
- Triggers: `L R` (fully in). Left stick full tilt: `SL SR SU SD`. Right stick: `CL CR CU CD`.
- `SHOT`: a `[FBDUMP]` of the next frame (over COM1 when present, else a BMP to E:).
- `TSHOT`: a shot at the match tick (`xhw_autopad_tick`), for match-relative timing.
- `NEXT`: launch `MX_NEXT_XBE` now (a console round chains builds this way).
- Lines beginning `env NAME=VALUE` set what `getenv()` returns. Up to 16.
  Unknown tokens are logged as `[AUTOPAD] unknown token`. Up to 512 events.

Example: `600 A` presses A at presented frame 600; `900 SHOT` takes a screenshot.

The `getenv` keys the port reads (test builds only; a release build's `getenv`
returns NULL):

| key | read by | effect |
|---|---|---|
| `MX_VIDEO=480i\|480\|720\|1080` | `xhw_video.c`, over `settings.ini` | forces the video mode for the boot (`[VIDEO] MX_VIDEO=`) |
| `MX_ROM=<file>` | `xhw_rom.c` | the ROM file to load from D:\ by name (test builds), instead of the first one found |
| `MX_BIRDMAN_AT=<reads>` | `game/pad.c` | that many controller reads into a test, the switch a Birdman star makes (`func_802EE14C`), and the bird's position logged for the next frames (`[PAD] Birdman at`) |
| `MX_NEXT_XBE=F:\...\default.xbe` | `xhw_autopad.c`, `xhw_fbdump.c` | the XBE a `NEXT` event (or a hang) launches; shots get the folder's name |
| `MX_TEST_HANG=<frame>` | `xhw_autopad.c` | the game thread stops at that frame (checks the watchdog's report and recovery) |
| `MX_FILL_E=1` | `xhw_autopad.c` | fills E: to the last cluster with `E:\mx_fillN.bin` (disk-full tests) |
| `MX_PAD_NOISE=<n>` | `xhw_pad.c` | queues n axis events per poll, as SDL's joystick code does (reproduces the old queue growth) |

### 4.2 The xemu runner (`tools/xbox/xemu_run.sh`)

```sh
MX_RUN=/c/xemu/wx-run$slot MX_XEMU=/c/xemu/xemu/xemu.exe \
MX_XISO=/c/xdev/nxdk/tools/extract-xiso/build/extract-xiso.exe \
MX_XEMU_ARGS="-config_path C:/xemu/slots/$slot/xemu.toml" MX_ROM=<your rom or none> \
  tools/xbox/xemu_run.sh <secs> '<stop regex>'     # serial log: $MX_RUN/serial.log
```

| variable | default | meaning |
|---|---|---|
| `MX_ROM` | none | your ROM, packed next to `default.xbe`. `none` omits it, for the "No ROM found" card |
| `MX_XBE` | `build-xbox/xbe/default.xbe` | the XBE to stage |
| `MX_RUN` | `~/xemu/wx-run` | work folder: `stage/`, `game.xiso`, `serial.log`, `xemu.out` |
| `MX_STAGE_EXTRA` | none | folder whose files are also packed (`autopad.txt`, `debug.txt`) |
| `MX_XISO` | docker's `extract-xiso` | a native `extract-xiso` (Windows without Docker) |
| `MX_XEMU` | `/Applications/Xemu.app/...` | the xemu binary |
| `MX_XEMU_ARGS` | none | extra xemu arguments (e.g. `-config_path`) |
| `MX_GUI` | 0 | `1` leaves xemu running after the run |
| `MX_SHOTS` | number of `SHOT` lines in `autopad.txt` | stop once that many `[FBDUMP] END` lines are in; 0 off |

The runner stops at the timeout, when xemu exits, when the stop regex matches
the serial log, or when the screenshots are complete. Its output is the serial
log with CRs removed. xemu needs your own MCPX ROM, BIOS and HDD image; set it
to 64 MB (or 128 MB with a BIOS that honours it, to see `xhw_ramlock.c` hold the
upper half).

Lockstep (`MX_LOCKSTEP`) is named in PLAN.md D13 from Melee-X; it is not in this
tree.

### 4.3 Screenshots (`tools/xbox/fbdump_to_png.py`, `xhw_fbdump.c`)

```sh
tools/xbox/fbdump_to_png.py $MX_RUN/serial.log out   # -> out_000.png, out_001.png, ...
```

The serial log carries `[FBDUMP] BEGIN w h bpp pitch`, base64 lines of zlib-deflated
pixels, and `[FBDUMP] END`. The script decodes each complete frame (a mangled
line is reported and skipped). Pixels are X8R8G8B8 (or R5G6B5 at 16 bpp).
Stdlib only. On the console (no COM1), a screenshot is a BMP written to
`E:\UDATA\57580001\shotNN.bmp` (or `shot_<folder>_NN.bmp` in a chained round);
`console.py pull` fetches the first kind.

Game-frame images stay out of the tree (CLAUDE.md): the PNGs and BMPs belong
in the work folder.

### 4.4 The console (`tools/xbox/console.py`)

```sh
tools/xbox/console.py stage v33     # build-xbox -> $MX_HW/stage-v33 + $MX_HW/wings64_x.v33.map
tools/xbox/console.py deploy v33    # upload, verify, delete old logs and shots first
MX_ROM=<your rom> tools/xbox/console.py deploy v33   # also put your ROM next to default.xbe
tools/xbox/console.py pull v33      # logs and shots -> $MX_HW/logs33
tools/xbox/console.py ls            # what is in the console's log folder
```

| variable | default | meaning |
|---|---|---|
| `MX_FTP_HOST` | none (required) | the Xbox's IP, shown on its dashboard |
| `MX_FTP_USER`, `MX_FTP_PASS` | `xbox`, `xbox` | the console's FTP login |
| `MX_HW` | `~/xemu/hw` | the local folder for staged builds, maps and pulled logs |
| `MX_APP` | `/F/Applications/Wings64-X` | the console folder for `default.xbe` and `default.tbn` (also where a ROM goes) |
| `MX_ROM` | none | your ROM, uploaded next to `default.xbe` if the size differs. Never into the tree |

`deploy` puts `default.xbe` and `default.tbn` in `MX_APP` and `TitleImage.xbx`
and `TitleMeta.xbx` in `E:\UDATA\57580001` (the dashboard's icon cache). It
deletes the old logs first: `boot*.log` (`boot.log`, `boot_prev.log`, `boot2.log`,
`boot3.log`), `lastexit.txt`, `trace.log`, `crash.log`, `hang.log`, `prof.bin`,
`shot*.bmp`. The chained-round screenshots `shot_<folder>_NN.bmp` (`xhw_fbdump.c`, set by `MX_NEXT_XBE`) match neither the deploy nor the pull pattern, so pull them by hand. **Pull before you deploy.** `settings.ini`, `pilotwings64.eep`
and `debug.txt` are not touched and not uploaded by the script: `debug.txt` is
put up by hand over FTP. Keep each deployed build's map (`wings64_x.<v>.map`):
`sym.py` needs the map of the build that wrote the log.

### 4.5 Symbols (`tools/xbox/sym.py`)

```sh
tools/xbox/sym.py crash.log                  # every [CRASH] eip and stack address
tools/xbox/sym.py 0036c4f0 0014b6c0 ...      # given addresses
tools/xbox/sym.py --map wings64_x.v33.map crash.log
tools/xbox/sym.py --prof boot.log            # fold [PROF] buckets into functions (top 60)
```

`--map` defaults to `build-xbox/wings64_x.map`. The map must come from the build
that crashed or profiled. `--prof` folds the `[PROF] addr:samples` buckets of
the given logs into functions with their share of the samples.

### 4.6 The profiler (`-DXHW_PROF=1`)

`xhw_prof.c` samples every game thread at 1 ms (the KTHREAD time the clock
interrupt advances, and the interrupted EIP). A report each `XHW_PROF_SECS`:
the hottest 64-byte buckets of the XBE image as `addr:samples`, samples per
thread level, and the samples outside the image (kernel, nxdk). It implies a
test build. Fold with `sym.py --prof`.

## 5. The log

Logs go to `E:\UDATA\57580001\` (`XHW_UDATA_ROOT`): `boot.log` (the first 4 MB;
then the log goes on in `boot2.log` and `boot3.log` in turn, so the newest 2-4 MB survive),
`boot_prev.log` (the previous boot's, kept at each start), `lastexit.txt`,
`crash.log`, `hang.log`, `trace.log`, `settings.ini`, `pilotwings64.eep`,
`shotNN.bmp`. If E: can't be written, `boot.log` goes to `D:\` and the card
says so. COM1 mirrors the log when present (xemu). Each line is flushed by the
watchdog tick; lines tagged `[BOOT]`, `[ROM]`, `[VIDEO]`, `[MEM]`, `[WDOG]`,
`[EEP]`, `[CRASH]`, `[GAME]`, `[FATAL]`, `[NV2A] GPU`, `[NV2A] flip` are written
before anything else so a hang right after them keeps them.

### 5.1 Frame and subsystem lines

**`[PERF]`** (every 5 s, `xhw_perf.c`; the frame budget). Example shape:
`[PERF] 59.9 fps, 16.7 ms a frame (longest 22 ms): display lists 3.1 (textures 0.4), present 1.2 (GPU and flip waits 0.8), audio 0.3 (59 tasks a second), the rest 12.1 (game logic, idle)`.
- `fps`, `ms a frame`: presented frames per second and their average time.
- `longest`: the worst frame gap in the period (hitches).
- `display lists`: the interpreter's time per frame; `(textures)` its texture conversions and uploads.
- `present`: the back end's present; `(GPU and flip waits)` the part spent waiting.
- `audio`: the audio tasks' time per frame and the tasks per second.
- `the rest`: game logic and idle, what is left of the frame.
On the single-core console the buckets add up. Times are the ACPI clock.

**`[NV2A] frame N: ...`** (every 600 frames, `XGFX_STATS_EVERY`): `draws` this
frame; `tex pool KB free`; over the window: `idle waits` (GPU waits at a frame
start), `flip waits (repaired)`, texture KB, `pool allocations failed`, vertices
and draws, `pushbuffer peak of KB (restarts)`, `combiners approximated` (N64
combiner modes the register-combiner compiler approximated).

**`[NV2A] vblank shows the buffer being drawn`**, `display went back`, `flip
stalled`, `the back buffer stayed on screen`: the flip-race diagnostics (test
builds). `GPU stalled: get put dma_state pgraph ...`:
the GPU's pusher state at a stall. `first GPU fault: kind ...`: the first
PGRAPH or DMA fault with the pushbuffer words around `get` (`nv2a_fault.c`).
`frames with fewer than 3 draws`: thin frames. (Supersampling and its
`anti-aliasing` line were removed.)
`tiles: XGFX_TILE ...`: the tile and zcomp registers at start.

**`[GFX] frame N state S: tris T, rejected R, culled C, clipped L | vtx loads V, mtx M`**
(every 600 frames, `f3d_interp.c`): triangles drawn, rejected beyond the
viewport ratio, culled by face, clipped at the guard band or far plane, and the
display list's vertex and matrix loads. `thin frame` / `odd frame` (test builds):
frames that drew next to nothing or far less than the one before. `texture miss
(...)`: a texture load the cache could not satisfy, with its tile and TMEM
state. `hold: ...`: the frozen frame under a fade (captured on the GPU or by the
CPU, and how long). `unhandled op`: an F3D opcode the interpreter does not know.

**`[DRAW]`** (trace frames only, `XGFX_TRACE_FRAME`): per draw, the state (`cc`
combiner words, cycle type, texture units, blend, alpha reference, depth,
bias, stencil, fog) and the triangle count.

**`[AUDIO] N tasks (U us each), B buffers, F frames in, D dropped (ring full), S skipped (latency cap), peak P, A AI underruns, X bad addresses, Y unknown commands`**
(every 10 s, `audio.c`). Frames are 22050 Hz stereo frames. `skipped` is the
latency cap at work (XHW_AUDIO_MAX_MS). `underruns` is the game's DAC running
dry. `bad addresses` and `unknown commands` count the RSP HLE's failures. The
other `[AUDIO]` lines are the AC97 driver's: `found` (controller and APU as
found at boot), `idle`, `restarting`, `cold reset`, `stuck since boot`,
`timeout`, `<engine>, N Hz in -> 48 kHz`.

**`[GAME] state S (name) at T ms, retrace R, frames F`** (`boot.c`, on each
change): the decomp's top-level state (0 title, 5 flight, ...), the time since
boot, and the retrace and task counts. The first line for a scene change.

**`[PAD]`**: `port 1 buttons XXXX at retrace R` (test builds: the N64 button
word handed to the game when it changes); `L + R + BACK + BLACK: to the
dashboard` / `WHITE: restart` (the combos); `port N: <name>` / `removed` (the
controller layer); `rumble failed`.

**`[OS] stall: no gfx task for 20 retraces`** (test builds): the game waits, and
each thread's wait object (queue address; `sym.py` names it). `[OS] message ...
dropped` when a queue is full. `[OS] PI DMA outside the ROM` when the game reads
past the image (read as zeros).

**`[WDOG] ...`** (hang report, `xhw_watchdog.c`): the reason, retrace and
presented counts, free KB, then per thread: state, wait reason, priority, EIP,
and the stack words in the image (symbolise with `sym.py`). Also to `hang.log`.
`[WDOG] ... again after N s` when a stall ends or repeats. **`[BEAT] Ns: retrace R,
presented P, free F KB`** (every 5 s): the heartbeat; the last one before a freeze
says how far it got.

**`[CLOCK]`**: `cpu N MHz (TSC against the ACPI timer, 3375000 Hz); nxdk's guess M MHz`
at boot (log only; the TSC is never used for time). `retrace R.RR Hz over the
last S s of ACPI time` every 30 s from the watchdog: a 60 Hz output shows
60.00; a timer or vblank off by a lot shows here.

**`[MEM]`**: `boot: 65536 KB of RAM, nothing to hold` (64 MB) or the 128 MB hold
result (`xhw_ramlock.c`); `<where> free X KB of Y KB (image, virt, pool)` at
the points `xhw_mem_log` is called (boot and the load paths); `rdram: N KB at
ADDR` (the RDRAM arena, `rdram.c`).

**`[VIDEO]`**: `dashboard: encoder ..., AV pack N, widescreen W, 480p P, 720p Q,
1080i R` (what the dashboard allows; PLAN.md 3.2); `WxH N-bit progressive|interlaced`
(the mode set); `WxH skipped: X KB free, Y KB needed`, `XVideoSetMode failed`,
`fell back to 640x480 interlaced`; `retrace from the vblank | the ACPI timer at 60.000 Hz`.

**`[ROM]`**: `file D:\...`, `title "..." code NPWE region E v0 crc ... order z64`,
`loaded N KB in M ms, SHA-1 in K ms`, `verified: US v1.0 SHA-1 ok`, `SHA-1 <hex>`
(a failed hash), `lastexit.txt will say: ROM check failed (...)`.

**`[BOOT]`**: start (`<title> (date time)`, `test build`), `E: N MB free`,
`save folder write test: ok|FAILED`, `image <base>-<end> <name>, stack <addr>`,
`previous exit: <reason>` (from `lastexit.txt`) or `not recorded (switched
off, reset or killed)`, `card: <title>`, `next: <path>` (a console round's
launch), `restart: <path>`, `starting the game (bootproc)`.

**`[SETTINGS]`**: `no settings.ini: defaults written`, `BACK held at boot: 480i
4:3 (saved)`, `output ..., progressive ..., widescreen ..., fps ..., screenshots
..., stick dead zone N%`, `saved`, `could not write`, `panel open: N rows`,
`panel closed`, `save and restart: output X`.

**`[EEP]`**: `no pilotwings64.eep: a fresh EEPROM (the game formats it)`, `loaded`,
`short or unreadable`, `saved (N bytes at 0xOFF, write W, M ms)`, `FAILED to save`.

**`[SHOT]` / `[FBDUMP]`**: `wrote <path>`, `could not create` (no space on E:);
`[FBDUMP] BEGIN/END` around a frame dump (section 4.3).

**`[PROF]` / `[PROFT]`**: profiler buckets and per-thread samples (section 4.6).

**`[CRASH]`**: `<exception> (code) at <addr>, frame F, thread T`, the fault
address, `eip/esp/ebp/eflags`, the registers, `free K KB`, `stack:` words in the
image. Also `crash.log`. Symbolise with `sym.py crash.log`.

**`[NOTICE]`**: a two-line message shown on screen (save and screenshot
problems: E: full). **`[FILE]`**: a replace was refused and the file renamed.
**`[FATAL]`**: a `wx`/`pw64` fatal (`WINGS64_FATAL` lines from `fatal.c`
also appear, for the xemu runner). **`[DEBUG]`**: a `debug.txt` value read
(`name = value (debug.txt; default D)`), test builds. **`[SELFTEST]`**: the
back-end self test. **`[MENU] Exit to dashboard`**: the title's exit item.
**`[SCENE]` and `[WARN]`** are tags that jump the log queue; nothing in the
tree writes them yet.

### 5.2 `debug.txt` (`xbox/src/hw/xhw_debug.c`)

Test builds only. `E:\UDATA\57580001\debug.txt` holds `name = value` lines
(integers, C `strtol` syntax so `0x` works; `#` starts a comment), read once at
the first lookup. A key the file does not name returns its built-in default; a
release build, or no file, returns the default for every key. Each value used is
logged once as `[DEBUG] name = value (debug.txt; default D)`.

The only key read in the tree is `shadow_mark` (`nv2a.c`, default
`XGFX_SHADOW_MARK`): 0 turns the shadow mark pass's colour writes off (the A/B
for the stencil shadows; `docs/renderer.md`), 1 leaves them on. The file is
read when the back end starts.

## 6. Checklist for a change

1. Host tests and `rom_leak_check.py` pass.
2. Release build (no `XBOX_CFLAGS`) and a test build both link.
3. Scenario in xemu (`xemu_run.sh` with an autopad scenario): the boot, the
   picture (`fbdump_to_png.py`), no `[CRASH]`, no `[WDOG]`, `[GAME]` moves.
4. Console run for timing, audio, memory and video modes (`console.py`); pull
   the logs before the next deploy.
5. Every new switch goes in section 3, with its default and its file; every new
   log line goes in section 5.
