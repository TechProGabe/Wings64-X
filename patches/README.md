# patches/ - changes to the fetched decomp (external/pw64-decomp @ 1a8558f)

`tools/xbox/fetch_decomp.sh` fetches n64decomp/pw64 at
`1a8558f5ceb320cce254d6da0a49a1f2bbc23e21` and applies these in name order
with `patch -p1 --fuzz=0`; a patch that does not apply exactly stops it.
Rules (CLAUDE.md):

- One patch per reason, `NNN-<reason>.patch` plus `NNN-<reason>.md` (why,
  origin, what changed on import).
- Every change is wrapped in `#ifdef WINGS64_XBOX ... #else <original> #endif`,
  so the N64 build is byte-identical. (Additions with no original, such as
  021's font helper, are `#ifdef WINGS64_XBOX` only.)
- Never edit `external/pw64-decomp/` by hand: change a patch and run
  `tools/xbox/fetch_decomp.sh` (it re-fetches when a patch changes;
  `--refresh` forces it).
- The patches are LF (`.gitattributes`: `*.patch -text`) and the fetch checks
  the decomp out with `core.autocrlf=false`, so they apply on every host.
- Nothing here is sent upstream (the decomp's CONTRIBUTING.md bans
  AI-generated contributions).

## Import from wings64-dc (2026-10-15)

All but 025 are wings64-dc's patches at its commit `0c384bd`, with the guard
`WINGS64_DC` renamed `WINGS64_XBOX`. Each `.md` ends with an **Import** line
saying what else changed. 025 (wings64-dc's in-game "TECHPROGABE presents"
card) is not taken: the Xbox splash is Melee-X's `xhw_splash.c` (PLAN.md
1.1). The Xbox is ILP32 and little-endian like the Dreamcast's SH-4, so the
DC patches' pointer-width and byte-order assumptions hold unchanged; the
bit-field ones hold because the game triple uses GCC's layout
(`-mno-ms-bitfields`, checked in `xbox/src/game/rom/layout_checks.c`).

| patch | reason | changed on import |
|---|---|---|
| 001 | bit-fields: `uv_filesystem.h` flag word declared LSB-first | guard only |
| 002 | bit-fields: `setimg.cmd` read as `w0 >> 24` | guard, comment |
| 003 | bit-fields: EEPROM photo album to/from the IDO byte image | guard only |
| 004 | UB: block-scope `customFxParams` made static | guard only |
| 005 | UB: `test_summary.c` `sp5C[3]` overrun | guard only |
| 006 | UB: `gSiContStatus[4]` | guard only |
| 007 | UB: `app_entrypoint` prototype | guard only |
| 008 | UB: reverb negated unsigned index | guard only |
| 009 | UB: synth param pool stride | guard only |
| 010 | UB: debug vertex colour loop over four arrays | guard, comment |
| 011 | UB: `_uvMediaCopy`'s deliberate fault store -> `pw64_fatal` | guard, comment |
| 012 | busy-wait spins yield (`osYieldThread`) | guard, comments |
| 013 | endianness: ROM structs swapped in place after the copy (`PW64_SWAP`) | guard only |
| 014 | endianness: texel tint loops | guard only |
| 015 | endianness: audio bank / sequence file deserialisers | guard, comment paths |
| 016 | endianness: font `FRMT` words and `BITM` records | guard only |
| 017 | memory map: RDRAM arena (`N64_K0`) instead of fixed KSEG0 literals | guard, comments (and see its `.md`: bit 31) |
| 018 | scheduler RSP status register write dropped | guard, comment |
| 019 | UB: `uvMat4CopyF2L` debug fault store dropped | guard, comments |
| 020 | UB: camera arrays declared shorter than used | guard, comments |
| 021 | port feature: title-screen Settings item, frame hook, overlay font helper (`DIVERGES`) | guard, `uvFontOverlayStr_dc` -> `_port` |
| 022 | audio task run on the audio thread, not queued on the scheduler (`DIVERGES`) | **runs the command list** (`wings64_audio_task`) |
| 023 | kernel thread's idle spin blocks instead | guard, comment |
| 024 | `_uvDebugPrintf` forwarded to the port log, rate-limited | guard, comment |
| 026 | screen fade over the frozen last frame (`DIVERGES`, `WINGS64_FB_FREEZE`) | guard, comment |
| 027 | debug camera aimed at the landing pad (off unless `WINGS64_DBG_LOOK_AT_PAD`) | guard only |
| 028 | photo album keeps a persistent frame (`DIVERGES`, `WINGS64_FB_PERSIST`) | guard, comment |
| 029 | host pointers: the K0/physical conversions are the identity | Wings64-X own |
| 030 | culling frustum widened to the full screen (`DIVERGES`, `WINGS64_CULL_WIDEN`) | Wings64-X own |
| 031 | title menu: fourth item "Exit to dashboard" (`DIVERGES`, `WINGS64_TITLE_EXIT`) | Wings64-X own |
| 032 | static-data sources of `_uvMediaCopy` copied as RAM (save mirror, Birdman take-off pad) | Wings64-X own |
| 033 | UB: `uvLevelInit` COMM copy clamped to `UVBlockCounts` (it overwrote `alGlobals`) | Wings64-X own |
| 034 | title menu: BACK opens the Settings panel (`DIVERGES`) | Wings64-X own |
| 035 | title menu: the game's two items at the bottom, no copyright line (`DIVERGES`, `WINGS64_SETTINGS_IN_OPTIONS`, `WINGS64_TITLE_COPYRIGHT`) | Wings64-X own |
| 036 | Option menu: "Port settings" and "Exit to dashboard" (`DIVERGES`, `WINGS64_SETTINGS_IN_OPTIONS`) | Wings64-X own |
| 037 | rumble on a crash (the crash jingle starts; `DIVERGES`, `WX_RUMBLE`, the Rumble row) | Wings64-X own |
| 038 | frame rate: `func_80313D74` sets the N64-frame scale (`wings64_rate.h`), frozen-time screens per N64 frame (`DIVERGES`, `WX_RATE`) | from Birdman64 (MIT) |
| 039 | frame rate: `func_80313AF4`'s approach step covers the N64 frame's fraction (`DIVERGES`, `WX_RATE`) | from Birdman64 (MIT) |
| 040 | frame rate: the flight recorder samples by recorder time, not every 2nd frame (`DIVERGES`, `WX_RATE`) | from Birdman64 (MIT) |
| 041 | frame rate: frame counters used as durations count N64 frames (HG contact crash, gyrocopter trim, shutter; `DIVERGES`, `WX_RATE`) | from Birdman64 (MIT) |
| 042 | frame rate: physics steps without `dt` per N64 frame (weathervane, birdman landing nudge, JH crouch turn; `DIVERGES`, `WX_RATE`) | from Birdman64 (MIT) |
| 043 | frame rate: the birdman's flap impulse and decay skip are an N64 frame's (`DIVERGES`, `WX_RATE`) | from Birdman64 (MIT) |
| 044 | frame rate: per-frame random draws at the N64's spread (wind walk, skydiver drift, wing flutter; `DIVERGES`, `WX_RATE`) | from Birdman64 (MIT) |
| 045 | frame rate: per-frame visual steps and decays per N64 frame (JH legs and camera, fly-by camera, ski lift, shuttle, splash, hawk, sound fade, flame, radar blink; `DIVERGES`, `WX_RATE`) | from Birdman64 (MIT) |

038-045 (PLAN.md M5, real 60 fps) are adapted from Birdman64's rate-decoupling patches
(https://github.com/NoahVl/Birdman64, MIT, `crates/pw64-game/patches`, its `docs/notes/framerate.md`
audit): only the hunks that change anything at the port's 60 fps cap, re-guarded, one reason each, on
the helpers in `xbox/include/game/wings64_rate.h` / `xbox/src/game/rate.c`; each `.md` says what it
scales and what of Birdman64's was left out.

The port-side functions these call: `xbox/src/game/rom/` (`pw64_swap`, the
loaders, the arena, `N64_K0`'s base), `xbox/src/game/ultra/fatal.c`
(`pw64_fatal`), `xbox/src/game/wx_log.c` (`wings64_uv_debug`),
`xbox/src/game/settings_menu.c` (021, 031, 034: the title items, the
Settings panel, the frame hook) and, until their milestones,
`xbox/src/game/ultra/hooks.c` (frame persistence: M3-M4; audio task: M6).
docs/decisions.md has the full table.
