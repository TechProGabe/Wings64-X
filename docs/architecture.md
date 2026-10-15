# Architecture

Wings64-X is a native Xbox port of Pilotwings 64 (US `NPWE`). It is not an
emulator. The game is the N64 decompilation (n64decomp/pw64, pinned at
`1a8558f`) compiled for the Pentium III. Its Fast3D display lists are
interpreted on the CPU, the resulting triangles are drawn on the NV2A, and the
RSP audio microcode is reimplemented in C. The user's own ROM is loaded at boot
and checked before anything runs. Target: a stock 64 MB retail Xbox.

The design is `PLAN.md` (D1-D13 and §3 for the requirements); this file is the
map of what exists in the tree. `docs/platform.md` describes the platform layer
in detail, `docs/renderer.md` the textures, `docs/testing.md` the switches and
logs, `docs/toolchain.md` the build, `docs/rom.md` the ROM, `docs/decisions.md`
the table of every edit to imported code.

## 1. Two compiler triples

The tree has two kinds of code, compiled with different target triples because
they must link together and agree on struct layouts.

| part | path | triple and flags | built by |
|---|---|---|---|
| game | `external/pw64-decomp/` (decomp + patches), `xbox/src/game/`, `xbox/src/gfx/` | `i686-pc-windows-gnu -mno-ms-bitfields -march=pentium3 -mfpmath=sse -ffp-contract=off -fno-strict-aliasing -fwrapv` (the GCC bit-field layout the decomp's patches 001-003 need) | `tools/xbox/compile_game.py` (parallel, `-MMD`, one object per unit), with ThinLTO bitcode when `XBOX_LTO` is on (`tools/xbox/thinlto_link.py`) |
| platform | `xbox/src/hw/` | nxdk's `i386-pc-win32`, `-O2 -march=pentium3 -Wall` | CMake, nxdk's toolchain file (`xbox/CMakeLists.txt`, `xbox/build.sh`) |
| shared | `xbox/include/xhw.h`, `xgfx.h`, `xrom.h` | the only structs crossing the triples: 32-bit scalars, floats, byte arrays, no bit-fields, no 64-bit members | both |

Both triples are linked into one `default.xbe` by nxdk's `lld-link`; ThinLTO
runs over the game objects. Melee-X proved the two triples link and call each
other. `docs/toolchain.md` has the flag sets and the reasons.

## 2. The layers

```
 user's ROM  (D:\*.z64|.n64|.v64, next to default.xbe)
     |  xhw_rom.c: find, load, byte order, header, SHA-1          (platform)
     v
 +--------------------------------------------------------------------------+
 | GAME (game triple)                                                       |
 |   external/pw64-decomp  + patches/   the N64 game, unchanged where it    |
 |     (src/kernel, src/app, src/libultra/audio and sp), patched for the Xbox           |
 |   xbox/src/game/ultra/  libultra shim: os_shim.c (threads, queues, VI,   |
 |     SP, AI, PI, EEPROM, controllers), mio0.c, fatal.c, hooks.c           |
 |   xbox/src/game/        boot.c (wx_boot), rom/ (arena, byte swap),       |
 |     settings.c, settings_menu.c, pad.c, audio.c, eeprom.c, wx_log.c      |
 |   xbox/src/gfx/         f3d_interp.c (Fast3D on the CPU), texconv.c      |
 +--------------------------------------------------------------------------+
     |  xgfx.h (XgfxState, XgfxVtx, XgfxOutput)      |  xhw.h (log, time,
     v                                               |  threads, files, pad,
 +--------------------------------------------------+  |  audio ring, video)
 | PLATFORM (nxdk triple)                           |  |
 |   nv2a.c, nv2a_cc.c, nv2a_fault.c  NV2A back end |  v
 |   asp_hle.c            RSP audio HLE            |  xhw_audio.c (AC97, APU)
 |   xhw_video.c, xhw_splash.c, xhw_pad.c (SDL)    |  xhw_sys.c (log, files, threads)
 |   xhw_watchdog.c, xhw_crash.c, xhw_clock.c      |  xhw_ramlock.c, xhw_code_repair.c
 |   xhw_main.c           boot flow, exits        |  xhw_perf.c, xhw_prof.c, xhw_fbdump.c
 +--------------------------------------------------+
     |
 pbkit (nxdk, patched by tools/xbox/patch_pbkit.py), the kernel, XVideo, SDL2, USB
     |
 Xbox: Pentium III, 64 MB, NV2A, AC97/APU, HDD (E:), DVD (D:)
```

The game never touches the kernel or pbkit: it talks to the shim, the shim
talks to `xhw.h`, and only the platform layer calls the kernel.

## 3. The decomp and the patches

- **Fetch.** `tools/xbox/fetch_decomp.sh` clones n64decomp/pw64 at
  `1a8558f5ceb320cce254d6da0a49a1f2bbc23e21` into `external/pw64-decomp/`
  (gitignored), with `core.autocrlf=false`, then applies `patches/*.patch` in
  name order with `patch -p1 --fuzz=0`. A patch that does not apply exactly stops
  it, and the tree replaces the old one only when every patch applied. A stamp
  (pin plus patch hash) makes reruns no-ops.
- **Patches.** 36 patch files in `patches/` (001-037, with 025 not taken), each
  with a `.md` (why, origin, what changed). Every hunk is wrapped
  `#ifdef WINGS64_XBOX ... #else <original> #endif`, so the N64 build is the
  original. Most are imported from wings64-dc (`WINGS64_DC` renamed to
  `WINGS64_XBOX`); the rest are Wings64-X's own (`patches/README.md`, the table
  of what each changed on import).
- **Kinds of patch.** Bit-field and byte-order fixes for the GCC layout and the
  big-endian assets (001-003, 013-016), undefined-behaviour fixes (004-011,
  019-020, 033), the arena for the N64's RDRAM and addresses (017, 029), the
  scheduler and idle-thread changes (012, 018, 022, 023), the port's settings
  menu and title items (021, 031, 034-036), the render and frame-buffer hooks
  (026, 028), the culling widening for the wider view (030), a debug camera
  (027, off), and rumble on a crash (037).
- **Shadow headers.** `xbox/include/game/` holds the headers the decomp includes
  (`macros.h` with the empty `__attribute__`
  fixed, `segment_symbols.h` as constants, `pw64_endian.h`, the port's own
  `wings64_settings.h`, `wings64_fbfreeze.h`). `wings64_prelude.h` is force-included
  into every game unit: it defines `WINGS64_XBOX`, `PW64_SWAP` (the endian
  swap), `N64_K0` (the address-to-arena macro), and redirects the printf family
  to the port's logger.
- **Every departure from the game is marked** `DIVERGES: <reason>` in the port's
  code and in the patches, and recorded in `docs/decisions.md`.

## 4. The libultra shim (`xbox/src/game/ultra/os_shim.c`)

The game calls the libultra interface (`osCreateThread`, `osSendMesg`,
`osViSwapBuffer`, `osSpTaskStartGo`, `osPiStartDma`, `osAiSetNextBuffer`,
`osContGetReadData`, `osEepromLongWrite`, `osSetTimer`, `osGetTime`, ...), and
the shim implements each on the Xbox (D5). About 60 functions, each commented in
the file. The M2 stubs file (`ultra/stubs.c`) is gone.

- **Threads.** `osCreateThread` makes a kernel thread at a level chosen from the
  N64 priority (`level_of`): idle 2, app 8, kernel 9, the N64 priority band up to
  110 at 10, scheduler 11, PI manager 12, render and fault 13, VI manager 14. The
  audio manager (N64 priority 110) is moved to level 14 (`WX_AUDIO_THREAD_LEVEL`,
  a DIVERGES: a long frame of the scheduler must not starve the mix). The
  watchdog is 15. Kernel
  preemption is strictly by level, which is how libultra's order is kept.
- **Message queues and events.** `OSMesgQueue` is a ring with a recursive lock
  standing in for libultra's interrupt mask (`lock()`), and each blocked
  thread waits on its own event (`sleep_on`). `osSetEventMesg` is the table the
  VI and the timers post into.
- **VI.** The `vi_thread` waits on the retrace (`xhw_wait_vblank`: pbkit's vblank
  at 60 Hz, else the ACPI timer), latches the swapped frame buffer, counts the
  retrace, posts the VI message every `s_vi_every` retraces (skipped when the
  scheduler's queue is nearly full: a DIVERGES, `WX_VI_RESERVE`), and calls
  `wx_pad_retrace` (the combos, pad.c).
- **Graphics tasks.** `osSpTaskStartGo` with `M_GFXTASK` runs `f3d_run` on the
  calling thread (the Fast3D interpreter draws and presents the frame), then
  posts the SP and DP done events. One task is one frame.
- **Audio tasks.** `M_AUDTASK` goes to `wx_audio_task` (audio.c, the RSP HLE),
  then the SP event. Audio lists are counted and dropped with `WX_AUDIO=0`.
- **PI DMA.** `osPiStartDma` from the cart is a `memcpy` from the verified ROM
  image held in RAM (`wx_rom_ptr`); an address outside the ROM is read as zeros
  and logged (`[OS] PI DMA outside the ROM`). A write to the cart is a fatal.
- **AI.** `osAiSetNextBuffer` hands the buffer to `audio.c`; `osAiGetLength`
  comes from a virtual DAC that pays the game's audio clock in wall-clock time,
  counting the underruns.
- **Time.** `osGetCount`/`osGetTime` are the ACPI clock scaled to the N64's
  46.875 MHz count (D5). `osSetTimer` fills one of 8 slots; a timer thread
  sleeps until the next deadline and posts the message.
- **Controllers and EEPROM.** `osContGetReadData` calls `wx_pad_read` (pad.c: the
  N64 mapping, section 7). The EEPROM is `pilotwings64.eep` (eeprom.c).
- **Cache and misc.** The cache calls are no-ops (the DMA is a copy here),
  `osVirtualToPhysical` is the identity (patch 029), and the MIO0 decoder is
  in C (`ultra/mio0.c`).

## 5. The Fast3D interpreter (`xbox/src/gfx/f3d_interp.c`)

Phase A of D4: the RSP's work is done on the CPU, the RDP's work on the NV2A.

1. `f3d_run(dl)` walks the display list (F3D GBI, `f3d_gbi.h`, the opcodes of
   wings64-dc's `gfx_f3d.c`): the 16-entry vertex cache, the matrix stack and
   its loads, the segment table (`wx_addr` on the Xbox: addresses are host
   pointers), lighting, texgen, the fog factor, the viewport.
2. Clipping (near, the guard band, far) and culling on the CPU. A triangle is
   rejected beyond 1.2 w of the N64 viewport (`XGFX_REJECT_RATIO`): the port
   draws out to the screen edges, so the N64's exact limit would pop terrain.
3. Textures: the TMEM deswizzle and the format conversion (`texconv.c`, tested on
   the host), keyed by (RAM address, tile descriptor) in the back end's cache
   (`docs/renderer.md`).
4. Each triangle batch and its draw state goes to the back end through `xgfx.h`
   (`xgfx_draw`): an `XgfxState` (combiner words, cycle type, two texture units,
   blend, alpha test, depth function and write, stencil, polygon offset, fog,
   scissor, primitive and environment colours) and screen-space vertices
   (`x y z w`, RGBA8, `s t`, fog).
5. Rectangles, fills and clears (`TEXRECT`, `FILLRECT`) take the same path
   (`xgfx_clear`, `xgfx_read_rect`).
6. The end of the list is `xgfx_present` (`xgfx_frame_begin` at the start). Frame
   copies for the fades and the photo album (`xgfx_capture_last`,
   `xgfx_target_hold`) keep the last picture on the GPU.

Kept from the Dreamcast port (wings64-dc's `kb/traps.md`): the texrect encoding,
UVBT tile rows, far-plane clipping, shadow volumes, the SUBSCREEN overlay and
title fits, the LOADBLOCK `dxt` rule. Dropped where the NV2A does better (the
combiner per vertex, painter-depth layers, the baked sea product, modifier
volumes: stencil does the shadows).

## 6. The NV2A back end (`xbox/src/hw/nv2a*.c`, `nv2a_cc.c`)

`nv2a.c` implements `xgfx.h` on pbkit, and is Melee-X's back end with the GX parts
removed (its header says so).

- **Combiners.** `nv2a_cc.c` compiles the N64 colour combiner word pair
  (`(a - b) * c + d` per cycle) to register-combiner stages, cached by the
  combiner key (`[NV2A] approximated` counts the modes the compiler does not
  know). A host test (`tools/xbox/test_cc.py`) checks it against a software model.
- **State.** Each draw's state is diffed against the registers the GPU already
  has, so unchanged state is not sent again (the redundant-state filter of D13).
- **Vertices** go through a fixed pass-through vertex program into a
  write-combined ring of 1.5 MB, in windows of 32768 vertices, so a start index stays
  below 0x10000 (the console raises a PGRAPH data error past it).
- **Pushbuffer.** 1 MB, kicked every 8192 words, drawn in order; a
  `BREAK_VERTEX_BUFFER_CACHE` at each batch; the GPU is waited for at the next
  frame's first use (`XGFX_OVERLAP`), with the wait and the flip race
  counted in `[NV2A]`.
- **Textures.** A pool (8 MB at 480, 6 MB at HD) with first-fit small and big
  blocks and deferred frees; the cache is wiped on a level load.
- **Present and flip.** The frame is shown on the next vblank; a flip that pbkit
  loses is repaired by putting the newest finished frame on screen
  (`XGFX_FLIP_REPAIR`). `xhw_perf` times the present and the waits.
- **Frame buffers and output.** 640x480x32 at 480, R5G6B5 at 720p and 1080i
  (section 8, the video modes), with Z24S8 depth. Supersampled antialiasing
  (2x, 4x) only at 640x480 when memory allows.
- **Faults.** pbkit is patched so a GPU fault is recorded and logged rather than
  freezing the machine (`nv2a_fault.c`).

## 7. Input

`xhw_pad.c` polls four controller ports through nxdk's SDL2 GameController
layer. `pad.c` maps player 1 to the N64 pad: left stick with a radial dead zone
(settings, default 20%), A and X to A and B, the right stick and Y/B/White/Black
to the C buttons, the triggers to Z and R. BACK on the title opens the settings
panel. The combos L+R+BACK+BLACK (dashboard) and L+R+BACK+WHITE (restart) work in
every state: at each controller read, and from the VI thread when the game has
stopped reading. Rumble is requested by the crash path (patch 037) and sent at
the next read.

## 8. Video output

`xhw_video.c` chooses the mode once, before pbkit: the dashboard's encoder
settings allow, `settings.ini` chooses (`output = auto|480i|480p|720p|1080i`),
and a mode that cannot be set or lacks memory falls to 480. BACK at boot forces
480i 4:3 for the boot. The retrace is pbkit's vblank for 60 Hz output, else the
ACPI timer at 60.000 Hz. The 16:9 path (horizontal-plus and the HUD box, PLAN.md
3.2) is not implemented yet; the setting is stored and shown only.

## 9. Audio

```
N64 game (libultra's synthesizer, from the decomp)
   | audio task: an Acmd list (osSpTaskStartGo, M_AUDTASK)
   v
audio.c -> asp_hle.c   RSP audio microcode in C (ADPCM, RESAMPLE, ENVMIXER,
   |                   MIXER, ..., MMX inline asm where the bits match scalar)
   | AI buffer (osAiSetNextBuffer; virtual DAC paces it)
   v
xhw_audio_write -> lock-free ring (32768 int16 samples)
   |
   v  pump thread, kernel level 14
fill_48k (22050 -> 48000, linear, latency cap 200 ms)
   |
   v
AC97 descriptor ring (8 x 1024 frames, polled)      or   xemu's APU voice
```

Costs and the fallback threshold are PLAN.md D6; the console measurement
(`[PERF]` `audio`, `[AUDIO]`) is the one that counts, and nothing has been heard
on hardware yet.

## 10. Settings menu

Title item "Settings" (or, by default, "Port settings" in the game's Option menu,
patches 031, 034-036), drawn with the game's own menu code, backdrop, font 6,
cursor and sounds. `settings_menu.c` builds the rows: Resolution, Texture filter,
Anti-aliasing, Rumble, FPS counter, Screenshots (test builds), Stick dead zone
(with a live readout of the stick), Save and restart, Return. "Exit to dashboard"
is the title's last item (patch 031). Changes marked `*` apply after a restart
(`xhw_reboot_self`). `settings.c` reads and writes `E:\UDATA\57580001\settings.ini`
atomically. The FPS counter is the renderer's (`nv2a.c`, the Melee-X overlay).

## 11. Boot to the game

```
default.xbe
 -> main_body (xhw_main.c)
    RAM lock (64 MB), mount E:, open boot.log, log the previous exit,
    code repair, watchdog, ACPI clock, splash
 -> xhw_rom_load_verified (xhw_rom.c): find, read 8 MB, normalise, SHA-1
 -> wx_boot (game/boot.c)
    settings.ini (BACK at boot = 480i), video mode, xgfx_init (pbkit)
    wings64_rdram_init (4 MB arena), bootproc (decomp), then the boot
    thread watches the game's state ([GAME] lines)
```

A failure at any step shows a card (`xhw_error_card`) and goes to the dashboard
after its wait; the crash guard turns any fault into `crash.log`.

## 12. Memory

The design budget (PLAN.md D2, not measured): XBE about 3 MB; the ROM image
8 MB; the RDRAM arena 4 MB; frame buffers and depth 4.8 MB at 480, about 9 MB at
720p and 20 MB at 1080i; the texture pool 8 MB at 480, 6 MB at 720p, 4 MB at
1080i; pushbuffer 1 MB and vertex ring 1.5 MB; audio below 1 MB; threads, heap
and logs about 2 MB. `xhw_mem_free_kb()` gates the HD modes (32 MB free for 720p,
40 MB for 1080i). `xhw_ramlock.c` holds memory above 64 MB on a 128 MB console
(`[MEM]` lines). The live numbers are in `[MEM]` and `[CRASH] free` lines.

## 13. Logs and tests

Every subsystem logs with a tag (`[BOOT]`, `[ROM]`, `[VIDEO]`, `[NV2A]`,
`[AUDIO]`, `[GFX]`, `[PERF]`, `[PAD]`, `[EEP]`, `[SETTINGS]`, `[OS]`, `[WDOG]`,
`[CRASH]`, `[BEAT]`, `[CLOCK]`, ...) to COM1 (xemu) and `boot.log` on E:, see
`docs/testing.md`. Host tests (no ROM) cover the ROM checker, the combiner
compiler, the texture conversion and the audio HLE. xemu runs with autopad
scenarios check the boot and the pictures; the console is the truth for timing,
audio, memory and video modes.

## 14. Repository map

| path | what |
|---|---|
| `PLAN.md`, `CLAUDE.md`, `docs/` | the plan, the rules, the documentation |
| `external/pw64-decomp/` | the decomp at the pin, patched (gitignored) |
| `patches/` | the patches and their notes |
| `xbox/include/` | shared headers (`xhw.h`, `xgfx.h`, `xrom.h`, `xrom_known.h`, `xbuiltin.h`) and the decomp's shadow headers (`game/`) |
| `xbox/src/game/` | the game side: boot, the shim (`ultra/`), ROM glue (`rom/`), settings, pad, audio, EEPROM, logger |
| `xbox/src/gfx/` | the Fast3D interpreter and texture conversion |
| `xbox/src/hw/` | the platform layer: boot, video, log, files, pad, audio, NV2A back end, RSP HLE, diagnostics |
| `xbox/CMakeLists.txt`, `xbox/build.sh` | the link and the build driver |
| `tools/xbox/` | the build wrappers, the game compiler, ThinLTO link, fetch, xemu runner, console tools, host tests, leak check |
| `tests/xbox/` | the C sources of the host tests |
