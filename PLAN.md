# Wings64-X — implementation plan

Native original-Xbox port of **Pilotwings 64** (N64, 1996), built from the
`gcsmith/Pilotwings64Decomp` matching decompilation with nxdk and LLVM 21. It is
not an emulator: the decompiled game is compiled for the Pentium III and draws on
the NV2A, the way Melee-X does for Melee and OpenCrossing-Xbox for Animal Crossing.
Target: retail Xbox, stock 64 MB. Deliverable: a `default.xbe` that the user puts
next to their own Pilotwings 64 ROM.

This file is the plan for an executing LLM. It decides the architecture, orders the
work into milestones with exit checks, and names the hard problems and the open
questions. It was written 2026-10-15 from the three sibling repos and the survey in
section 1. It contains no code; the repo has no code yet (`README.md` only).

Read in this order: this file, then the sibling repos it points at
(`Melee-X`, `OpenCrossing-Xbox`, `wings64-dc`, all under `github.com/TechProGabe`).

---

## 0. Ground truth

| fact | value | source |
|---|---|---|
| Decomp | `gcsmith/Pilotwings64Decomp` @ `1a8558f` (2026-09-28; the repo now redirects to `n64decomp/pw64`), MIT; US build 100 % C, no game-logic asm; CONTRIBUTING bans AI-generated contributions, so **never send PRs upstream**, consume at a pin by commit SHA | wings64-dc `PLAN.md` §0, `docs/reference/decomp-portability.md` |
| ROM | US `NPWE`, 8,388,608 bytes, SHA-1 `ec771aedf54ee1b214c25404fb4ec51cfd43191a`, MD5 `8b346182730ceaffe5e2ccf6d223c5ef` (big-endian `.z64` order) | wings64-dc `docs/rom-provenance.md` |
| Microcode | stock **Fast3D** (`graphics.c:606-607`), `G_TRI1` only, 32 opcodes, 13 combiner modes, always 2-cycle | wings64-dc `PLAN.md` §0, Birdman64 `docs/notes/gfx.md` |
| Audio | stock libultra alSyn + aspMain, 22050 Hz, 32 PVoices, VADPCM waves (57 music + 107 SFX), 31 sequences, 368-sample frames | Birdman64 `docs/notes/audio.md` |
| Video (N64) | 320x240 16-bit, Z-buffer, LAN1; 4 MB fixed memory map, 39 `0x80xxxxxx` literals | decomp brief §5 |
| Per-frame load (flight) | 2.3–3.8 k tris, 8–13 k Gfx words, 63–312 draws, ~200 texture loads | Birdman64 `renderer.md:118,164` |
| Textures | RGBA16 / I4 / IA8 / I8 / IA4 / IA16 (+RGBA32 in UVBT), **no CI/TLUT**, ≤ 0x1000 B each, TMEM-swizzled rows, up to 6 mips, non-power-of-two heights | Birdman64 formats brief |
| Assets | IFF `FORM` filesystem, 1272 files, index at ROM 0xDE720, MIO0 blocks; parsers in `src/kernel/texture.c` | decomp brief §4 |
| Cart I/O choke point | `_uvMediaCopy` (`memory.c:136`) + `_uvDMA` (`system.c:419-438`) | decomp brief §5 |
| Save | EEPROM 16 Kbit (0x800 bytes), game uses 0x208 | `system.c:84` |
| Sibling Xbox ports | Melee-X (v4.1, 480i/480p/720p, settings menu, splash, AC97, crash/hang reporters, console tooling); OpenCrossing-Xbox (v1.1, origin of the splash, video rules, GL shim) | their `docs/` |
| Same game, other console | wings64-dc (M4 done, hardware runs 1–11): Fast3D interpreter, libultra shim, 28 decomp patches, in-game settings menu, ROM slicer, audio transcoder | `kb/STATE.md`, `patches/README.md` |
| Xbox | Pentium III 733 MHz (SSE1, 128 KB L2), 64 MB shared, NV2A (vertex programs, 8 general + 1 final register combiner, 4 texture units, Z24S8 with stencil), AC97 | Melee-X `docs/architecture.md` |

---

## 1. Research

### 1.1 What is reused from the maintainer's ports

The Xbox side is **Melee-X's platform layer**, taken as the model for structure,
build, boot and video; game-side mechanics come from **wings64-dc**, which already
runs this game from this decomp on a 16 MB console.

| area | take from | what |
|---|---|---|
| Layout and triples | Melee-X `CLAUDE.md`, `docs/toolchain.md`, `xbox/CMakeLists.txt` | `xbox/src/hw` (nxdk triple: kernel, pbkit, AC97, USB) vs game-side code; the two talk only through plain-scalar headers (`xhw.h`-style) |
| Build | Melee-X `xbox/build.sh`, `tools/xbox/docker/{Dockerfile,build.sh}`, `tools/xbox/msys/build.sh`, `tools/xbox/setup.sh` | nxdk `58427c07` built with LLVM 21.1.8, CMake + Ninja with nxdk's toolchain file, `XBOX_CFLAGS` reset every run, Docker and native-MSYS2 builds that lay code out identically, `-ffile-prefix-map` |
| Boot flow | Melee-X `xbox/src/hw/xhw_main.c`, `xbox/src/sdk/boot.c` | mount E:, make `E:\UDATA\<title id>\`, open `boot.log`, log `[BOOT] previous exit` (`lastexit.txt`), watchdog, splash, find the user's image next to `default.xbe` (D:\, any filename), validate it, `xsdk_early` (BACK held = safe video) then video mode; `xhw_crash_guard` around every thread |
| **Splash** | Melee-X `xbox/src/hw/xhw_splash.c` (from OpenCrossing `xbox_splash.c`) | "TechProGabe Presents..." on the navy gradient, CPU-drawn into the `XVideoSetMode(640,480,32)` framebuffer before pbkit, unscii-16 font, fade-in, 2 s hold (any button skips), load bar, "Hold BACK for 480i (safe video)" line, `-DXHW_NO_SPLASH`, `-DXHW_SPLASH_MS`. **Copied as is.** The maintainer's directive: not wings64-dc's in-game "TECHPROGABE presents" card (patches/025) |
| Video modes | Melee-X `xbox/src/hw/xhw_video.c`, `docs/platform.md` "VI"; OpenCrossing `docs/architecture.md` "Video output", "Safe video" | the dashboard allows, the settings choose; 720p 16-bit; `XVideoInit` for 480i on an HDTV pack; `xhw_video_fallback_480` when pbkit can't start; BACK at boot = 480i saved; Widescreen Auto/On/Off (OpenCrossing) |
| Error screens | Melee-X `xhw_error_screen`, `xhw_fatal`, `fatal_no_disc` | pbkit debug screen, title + lines, "Press any button or wait to return to the dashboard", 15–30 s, `XLaunchXBE(NULL)` |
| Settings file | Melee-X `xbox/src/sdk/settings.c`, `docs/platform.md` "settings.ini" | `E:\UDATA\<id>\settings.ini`, written with defaults on first boot, atomic replace (`settings.tmp` → read back → rename), unknown keys dropped |
| Settings menu behaviour | Melee-X `xbox/src/sdk/menu.c`; OpenCrossing `xbox_settings_menu.c` | restart-only rows marked `*`, "480p -> 480i" when the dashboard disallows, Save and restart via `xhw_reboot_self` (`XeImageFileName`), raw-button navigation, held-direction repeat by retrace count. **The drawing, however, is wings64-dc's in-game style (1.1 below)** |
| Renderer back end | Melee-X `xbox/src/hw/nv2a.c`, `nv2a_rc.c`, `nv2a_fog.c`, `nv2a_vp.c`, `tools/xbox/patch_pbkit.py`, `docs/renderer.md` | pushbuffer kicks, texture pool (first fit, deferred frees), vertex ring, state diffing, first-fault PGRAPH dump, GPU-fault-tolerant pbkit, tile regions, Z24S8 depth, `fb_cpu` aperture for CPU overlays, EFB-copy readback, TEV → register-combiner compiler as the template for the N64 combiner compiler |
| Audio output | Melee-X `xbox/src/hw/xhw_audio.c`, `docs/platform.md` "Audio" | polled AC97 (nxdk's IRQ path froze hardware), 48 kHz pump thread, APU voice under xemu, idle-at-boot, stuck-engine recovery and give-up notice |
| Input | Melee-X `xbox/src/hw/xhw_pad.c`, `xbox/src/sdk/pad.c` | SDL2 GameController, player N = physical port N, radial dead zone, event-queue drain (the v39–v52 leak), per-port remap in `settings.ini`, L+R+BACK+BLACK quits |
| Diagnostics | Melee-X `xhw_watchdog.c`, `xhw_crash.c`, `xhw_prof.c`, `xhw_fbdump.c`, `xhw_autopad.c`, `tools/xbox/{console.py,sym.py,prof_report.py,xemu_run.sh,fbdump_to_png.py}` | `boot.log`/`boot_prev.log`, `crash.log`, `hang.log`, `[BEAT]` heartbeat, sampling profiler, `[FBDUMP]` screenshots over serial in xemu, BACK screenshots on the console (test builds), scripted pads, FTP stage/deploy/pull |
| Icon and title ID | Melee-X `tools/xbox/xbe_title_image.py`, `make_logo.py` | `$$XTIMAGE` section, `default.tbn`, `TitleImage.xbx`/`TitleMeta.xbx`; a unique title ID (Melee-X `4D580001`, OpenCrossing `4F430001`) |
| Release | Melee-X `.github/workflows/build.yml`, `tools/xbox/package_release.py`, `README.md` | manual workflow, release tag input, zip with `default.xbe` + `default.tbn` + `tools/make-xiso`, link map kept for symbolizing |
| Decomp handling | wings64-dc `patches/`, `scripts/fetch_decomp.sh`, `src/dc/decomp/wings64_prelude.h`, `src/game/Makefile` | decomp fetched to `external/` (gitignored) at the pin and patched; one patch per reason with a `.md`; `#ifdef` guard so the N64 build is byte-identical; `DIVERGES:` marks; shadow headers; the flag set (`-D_LANGUAGE_C -DVERSION_US -DBUILD_VERSION=VERSION_D -D_FINALROM -DNDEBUG -DTARGET_N64 -DNON_MATCHING -DAVOID_UB -DRECOMP_BUILD`, `-funsigned-char -fwrapv -fno-strict-aliasing -ffp-contract=off -ftrivial-auto-var-init=zero -fno-aggressive-loop-optimizations`) |
| The 28 patches | wings64-dc `patches/001`–`028` | bit-field order (001–003), decomp UB (004–011, 019, 020), yields (012), endianness (013–016), RDRAM arena (017), scheduler status write (018), **settings menu hooks (021)**, audio task off the scheduler (022), kernel idle spin blocks (023), debug printf to the log (024), fade over frozen frame (026), debug camera (027), album persistent frame (028). 025 (in-game splash) is **not** taken |
| Fast3D interpreter | wings64-dc `src/dc/gfx/gfx_f3d.c` (1802 lines), `f3d_gbi.h`, `gfx_texconv.c` (+ host test) | DL walk, 16-slot vertex cache, matrix stack, segment table, state tracking, TMEM deswizzle, format conversion, the texture-cache keying, and every quirk already paid for (`kb/traps.md`: texrect encoding, UVBT tile rows, rect shade, Mtx halfword order, far-plane clip, shadow volumes, layer order, sea product texture, tie-break bias, SUBSCREEN fades, album frame) |
| libultra shim | wings64-dc `src/dc/ultra/os_shim.c` (511 lines), `rdram.c`, `mio0.c`, `kos_bridge.c` | the exact surface the game calls, the thread/priority mapping, virtual AI DAC, EEPROM, controller mapping, VI event from vblank |
| **In-game settings menu** | wings64-dc `src/dc/settings.c` (407 lines) + patch 021 | a third title item "Settings"; the Option screen's backdrop, fonts, cursor, chime and confirm sounds (`menuCreateItems`/`menuSetItem`/`menuCheckInputs`/`menuRender`); glyph strings built from font 6's bold set; value column via the `0xFD` x-position code; `uvFontOverlayStr_dc` for the FPS counter in the HUD font, drawn last in every frame's display list |
| Audio design facts | wings64-dc `docs/audio-design.md`, `kb/traps.md` "M4 audio" | `alAudioFrame` must report a non-empty command list; `osAiGetLength` = playing buffer only; the audio task must not wait for the scheduler; the N64 resampler clamps pitch at 1.99996 |
| ROM provenance and leak checks | wings64-dc `tools/rom/slice_fs.py`, `tools/check/rom_leak_check.py` | SHA-1 gate, filesystem walk (`uvMemInitBlockHdr` re-implemented on the host), no ROM bytes or images in the tree |

What is **not** taken from wings64-dc: sh4zam (SH-4 only), KOS, the ROM slice +
AICA transcode pipeline (the Xbox holds the whole ROM in RAM, see D2), the VMU
save, the in-game splash (025), the SD-card log, the PVR back end.

### 1.2 Survey of N64 port approaches

Checked 2026-10-15 (GitHub, project READMEs, the sibling repos' reference
notes). Four ways to put an N64 game on another machine exist; the fit for a
733 MHz Pentium III with 64 MB and an NV2A is judged for each.

| approach | representative repos | what it needs from the host | fit for Pilotwings 64 on the Xbox |
|---|---|---|---|
| **Full emulation** (interpreter/dynarec CPU + RSP + RDP) | Surreal64 / Surreal64 CE (1964, Project64 and UltraHLE on the Xbox; https://surreal64ce.sourceforge.net) | the whole N64 emulated on the P3 | **No.** Surreal64 is the existence proof that the P3 cannot emulate the VR4300 + RSP at full speed for most titles, and it needs HLE plugins anyway. Nothing to reuse but the lesson |
| **Static recompilation** (MIPS → C → native, RDP via a modern renderer) | N64Recomp (https://github.com/N64Recomp/N64Recomp, originally Mr-Wiseguy), Zelda64Recomp (https://github.com/Zelda64Recomp/Zelda64Recomp), RT64 renderer (https://github.com/rt64/rt64) | Zelda64Recomp's README: a GPU with Direct3D 12.0 Shader Model 6 or Vulkan 1.2, a CPU with AVX; RT64 is a shader-based, high-precision RDP | **No.** The NV2A has no shaders in that sense and the P3 has no AVX; recompiled MIPS also keeps the RSP work (the recomp runs the microcode through RT64's HLE). Where the game is already 100 % C, recompilation only loses information |
| **Decomp-based port with HLE graphics and audio** (compile the decompiled C, replace libultra with a platform layer, interpret Fast3D display lists, implement the RSP audio ABI in C) | sm64-port (https://github.com/sm64-port/sm64-port: `src/pc/gfx/gfx_pc.c`, Emill's n64-fast3d-engine https://github.com/Emill/n64-fast3d-engine; `src/pc/mixer.c`, a C/SSE4.1 implementation of the aspMain audio ABI, ~1,250 lines), sm64ex (https://github.com/sm64pc/sm64ex), Ship of Harkinian and libultraship (https://github.com/HarbourMasters/Shipwright, https://github.com/Kenix3/libultraship), Starship (Star Fox 64, https://github.com/HarbourMasters/Starship), SpaghettiKart (Mario Kart 64, https://github.com/HarbourMasters/SpaghettiKart), the Perfect Dark port (https://github.com/fgsfdsfgs/perfect_dark), **Birdman64** (Pilotwings 64 on PC, https://github.com/NoahVl/Birdman64, MIT) | a C compiler for the target and a renderer able to do the RDP's combiner/blender; the display lists are walked on the CPU | **Yes.** This is what every working N64 port does, what Birdman64 does for this very game, and what wings64-dc already did on a 200 MHz SH-4. The NV2A's register combiners map onto the N64 colour combiner better than any GL 1.x fixed-function path these ports started with |
| **Decomp-based console ports on weak hardware** (the same approach, with the interpreter tuned for the console's GPU) | sm64-dc, sf64-dc, mk64-dc (https://github.com/jnmartin84/sm64-dc, /sf64-dc, /mk64-dc), ssb64-dc (https://github.com/beigeceleron/ssb64-dc), DCA3 (GTA III on Dreamcast, https://github.com/emtee40/dca3-game mirror), **wings64-dc** (https://github.com/TechProGabe/wings64-dc) | the display-list interpreter emits native geometry; combiners approximated per vertex where the GPU has one texture unit | the closest relatives. wings64-dc's interpreter, shim and patches are reused directly (1.1); the Xbox needs none of their PVR workarounds (one texture unit, no stencil, modifier volumes, the 480i render scaler) |

Decomp status: the matching decompilation lives at
https://github.com/n64decomp/pw64 (the `gcsmith/Pilotwings64Decomp` URL redirects
there), MIT, US release only in the README, "work-in-progress matching
decompilation"; the US build is 100 % code at the pinned revision (wings64-dc
§0). Its `CONTRIBUTING.md` says "AI must not be used to generate code for
contributions to this project" and asks agents to discourage AI analysis of the
project. The plan's position is wings64-dc's: this port is a downstream consumer
under the MIT licence at a pinned revision, keeps its changes as its own patches,
and **never sends anything upstream**; the maintainer should be aware of the
policy (§7, item 13).

Pilotwings 64 specifics that decide the approach: stock Fast3D with `G_TRI1`
only, 32 opcodes and 13 combiner modes (small interpreter); stock alSyn + aspMain
audio (the same microcode family sm64-port's `mixer.c` and Birdman64's `abi.rs`
implement, so the RSP audio ABI can be reimplemented rather than the game's audio
rewritten); 2.3–3.8 k triangles a frame (far under what the NV2A or the P3 care
about); an 8 MB ROM whose assets are parsed by the game itself at load, so no
asset extraction pipeline is needed; 4 MB of RDRAM to emulate as an arena.

ROM handling in these projects: sm64-port and the decomp build systems check the
user's `baserom.*.z64` by SHA-1 and refuse anything else; N64Recomp takes a
`.z64` and checks its header; SpaghettiKart and Starship take a `.z64` and
generate an archive (`.o2r`) from it; Birdman64 and wings64-dc gate on the US
SHA-1 `ec771aedf54ee1b214c25404fb4ec51cfd43191a`. The N64 ROM header layout used
in §3.1 (0x10/0x14 CRCs, 0x20 title, 0x3B game code, 0x3E region, 0x3F version;
first word `80 37 12 40` big-endian) is the standard one libultra's `makemask`
writes and every emulator reads. The byte-order detection rule (first four bytes)
is what Mupen64Plus and Project64 use.

Xbox facts checked: nxdk's `lib/hal/video.c` mode table has **1920x1080i for the
HDTV pack at 60 Hz** (`0x880E0C03`, NTSC-M and NTSC-J, no PAL), and
`XVideoListModes` filters it out unless the EEPROM's `VIDEO_MODE_1080I` flag is
set, so `XVideoSetMode(1920, 1080, 16, 60)` works as the plan assumes when the
dashboard allows 1080i; `XVideoInit` allocates the framebuffer itself as
write-combined contiguous memory (so pbkit's own buffers come on top). Melee-X
and OpenCrossing already read `VIDEO_MODE_1080I` from the encoder settings
(OpenCrossing logs it) but never set the mode.

Unverified (noted, not relied on): any existing decomp port to the original Xbox
(none found; Surreal64 is emulation); the licence of sm64-port's `mixer.c` (no
header, repo without a licence file: treat as a reference to read, not code to
copy); Birdman64's current patch count (76 or 80 in the two reference notes).

---

## 2. Architecture decisions

### D1. Layering

```
user's ROM (.z64/.n64/.v64 next to default.xbe)          read at boot, validated, held in RAM
        |
external/pw64-decomp/   decomp @ 1a8558f, fetched + patched from patches/ (gitignored)
xbox/include/game/      shadow headers (PR/os.h, ultratypes, macros.h, segment_symbols.h), wings64_prelude.h (force-included)
xbox/src/game/          game-side port code compiled with the decomp's flags: ultra/ (OS shim), rom/ (swap helpers,
                        arena), settings.c (in-game menu), audio glue
xbox/src/gfx/           Fast3D interpreter (f3d_interp.c), texture import (texconv.c, tmem.c), combiner description
xbox/include/xgfx.h, xhw.h   the only structs crossing to the hw side: 32-bit scalars, floats, byte arrays
xbox/src/hw/            nxdk triple: xhw_main/video/splash/pad/audio/log/watchdog/crash/prof/fbdump/autopad,
                        nv2a.c back end, nv2a_cc.c (N64 combiner -> register combiners), asp_hle.c (RSP audio)
tools/xbox/             build, docker, msys, xemu runner, console.py, host tests, icon, release packaging
```

Every departure from the decomp's behaviour in port code is marked `DIVERGES: <reason>`
(wings64-dc discipline). Every edit to the decomp is a patch in `patches/` with a
`.md` (why, origin), wrapped `#ifdef WINGS64_XBOX ... #else <original> #endif`.
The decomp is never edited in place.

### D2. Memory model: the whole ROM in RAM, byte-identical

Load the user's ROM (8 MB) into one allocation, normalised to `.z64` byte order
(D9). `_uvMediaCopy` / `_uvDMA` / `osPiStartDma` become `memcpy` from
`rom_base + rom_addr`; the audio sample DMAs (`audio_manager.c`, 0x800-byte
chunks from `audio_tbl`) read the same buffer. No slicing tool, no offline
transcode, no disc layout: the Xbox has 64 MB where the Dreamcast had 16.

Assets stay big-endian and are swapped at the enumerated sites (wings64-dc D3,
patches 013–016, Birdman64's map): the game reads most fields byte-wise; the bulk
copies (Vtx, Mtx halves, Mtx4F, UVEN, UVTX Gfx words, task/spath/level tables,
fonts, audio banks) are swapped in place right after the copy with `PW64_SWAP`.

The N64's fixed 4 MB map is a 4 MB arena (patch 017, `rdram.c`): `N64_K0(a)` =
`arena + (a - 0x80000000)`. On the Xbox the XBE loads at 0x10000 and the arena is
an ordinary allocation, so every `0x80xxxxxx` literal goes through the arena macro
(no address trick is available as on SH-4; the DC patch already does this).

Budget (64 MB, worst case 1080i):

| item | size |
|---|---|
| XBE (game ~1.5 MB + port) | ~3 MB |
| ROM image | 8 MB |
| RDRAM arena | 4 MB |
| framebuffers + depth: 480 32-bit + Z24S8 / 720p R5G6B5 ×3 + Z24S8 / 1080i R5G6B5 ×3 + Z24S8 | 4.8 / 9.1 / ~20 MB |
| texture pool | 8 MB (480), 6 MB (720p), 4 MB (1080i) |
| pushbuffer 1 MB + vertex ring 1.5 MB | 2.5 MB |
| audio HLE buffers, mixer ring | < 1 MB |
| threads, heap, logs | ~2 MB |

Even 1080i leaves > 15 MB free; 720p > 25 MB. pbkit needs the framebuffers
contiguous, so the mode decision checks `xhw_mem_free_kb()` before `pb_init`, as
Melee-X does for 720p (32 MB free rule).

**64 MB is enforced** (maintainer decision 2026-10-15). A 128 MB console runs as a
stock one: `xhw_ramlock.c` (OpenCrossing's `xbox_ramlock.c` / Melee-X's
`xhw_mem_hold_upper`) holds every page above 64 MB first thing in `main_body`,
before anything allocates, and cxbe marks the XBE "limit to 64 MB". Reasons as in
those ports: pbkit and the back end mask GPU addresses with `0x03FFFFFF`, and
allocations that would fail on 64 MB (and fall back) must fail the same way on
every console. `[MEM] boot` logs the held amount; kill switch `-DXHW_RAM_LOCK64=0`
for diagnosis only.

### D3. Toolchain: nxdk at Melee-X's pin, two compile setups

- nxdk `58427c078b4ccb0121f359b6b6c536b3b2914976` with LLVM 21.1.8, exactly
  Melee-X's `setup.sh`/`Dockerfile` (drop the `disc_lower` build; Wings64-X needs no
  lowering pass). Docker image `wings64-x:sdk` shares layers with `melee-x:sdk`;
  the MSYS2 path (`tools/xbox/msys/build.sh`) is kept for Windows without
  virtualization.
- **Game-side code (decomp + `xbox/src/game` + `xbox/src/gfx`) uses Melee-X's game
  triple**: `--target=i686-pc-windows-gnu -mno-ms-bitfields -march=pentium3 -msse
  -mfpmath=sse -ffp-contract=off -fno-strict-aliasing -fwrapv -funsigned-char
  -ftrivial-auto-var-init=zero -fno-aggressive-loop-optimizations -fno-auto-import`.
  Why not nxdk's `i386-pc-win32`: MS bit-field layout does not merge adjacent
  fields of different types, so the decomp's bit-field structs (`uv_filesystem.h`,
  `gbi.h`, reversed for LSB-first targets by patches 001–003) would lay out
  differently from what the DC-proven patches assume; `-mno-ms-bitfields` gives
  the GCC layout those patches were written for. `-mfpmath=sse` keeps floats
  single precision as on the VR4300 (no x87 excess precision), which the flight
  model and `fdr.c` replays depend on. Melee-X proved the two triples link and
  call each other.
- Platform code (`xbox/src/hw`) uses nxdk's triple, `-O2 -march=pentium3 -Wall`.
- Decomp TUs get `-Wno-*` for the IDO-isms (implicit int, int/pointer
  conversions, multichar) and `-Werror=implicit-function-declaration` (an
  undeclared float function would return in EAX, Melee-X's rule).
- Shadow headers: `macros.h` with `#include_next` and `#undef __attribute__`
  (the decomp defines it empty, dropping `aligned(8)`), `_LANGUAGE_C`, libc shims
  over nxdk's pdclib (`stdint.h`, `stddef.h`, `math.h`; pdclib's printf has no
  `%f`: route the game's few printfs through a float-aware `xsdk_vsnprintf` as
  Melee-X's `log.c` does).
- **ThinLTO is required** (maintainer decision 2026-10-15): the game-side code
  (decomp + `xbox/src/game` + `xbox/src/gfx`) is compiled to bitcode with
  `-flto=thin` and linked through Melee-X's `tools/xbox/thinlto_link.py`
  (lld-link `-thinlto-index-only`, clang's ThinLTO backend per module with the
  game triple's codegen flags, native objects linked as usual, so an order file
  still applies). Knob `XBOX_LTO=1|0`: **on by default** for every build and
  required for releases (`package_release.py` refuses a non-LTO build); `0` is
  the kill switch for diagnosis. Platform code (nxdk triple) and nxdk's libraries
  stay native. Import limit as Melee-X (`-import-instr-limit=10`, `XBOX_LTO_INDEX`
  to change it). Rules that keep LTO honest: `-opt:noicf` (the game compares
  function pointers), no `-ffast-math`, the same `-ffp-contract=off -mfpmath=sse`
  in the backend as at compile time (float determinism, H6: the fdr hash oracle
  must match LTO and non-LTO builds), and the replay hash and the host tests run
  against the LTO build in CI. Introduced in M3 (once the game runs, so a
  miscompile is visible), mandatory from M5 on.
- `xbox/order.txt` (hot-function layout) and PGO are Melee-X machinery to adopt
  in M9 only if the profile asks for it; the P3 has a 128 KB L2 and this game is
  ~1/10 of Melee's code, so probably not.

### D4. Rendering: the Fast3D interpreter on the CPU, the RDP on the NV2A

Phase A (M3–M4): port wings64-dc's `gfx_f3d.c` as `xbox/src/gfx/f3d_interp.c`,
keeping its DL walk, vertex cache, matrix stack, segment table, lighting, texgen,
fog coefficient, clipping and all the PW64 quirks, replacing sh4zam math with
plain C/SSE and the PVR emission with a canonical draw interface
(`xgfx.h`: a `XgfxState` of combiner, othermode, blender, tile/TMEM descriptors,
fog, scissor, Z mode; vertices as screen-space `x y z w` + RGBA8 + `s t` +
per-vertex fog). The CPU does the RSP's work: 2.3–3.8 k triangles a frame is
nothing for a 733 MHz P3 (wings64-dc's SH-4 at 200 MHz manages 45–51 fps), and
it keeps the N64's exact clip, lighting and texgen semantics. The NV2A runs a
fixed pass-through vertex program (screen-space positions in, no transform;
Melee-X's `nv2a_vp.c` encoder generates it) and does everything the RDP did:

- **Combiner** (`nv2a_cc.c`, from Melee-X `nv2a_rc.c` / OpenCrossing
  `xbox_tev_rc.c`): the N64's `(a - b) * c + d` per cycle maps onto the register
  combiners' `A*B + C*D` with signed inputs; two cycles → up to four general
  stages, TEXEL0/TEXEL1, SHADE, PRIM, ENV, LOD_FRAC, COMBINED, K4/K5, NOISE
  (constant) as inputs. Compiled per `SETCOMBINE` word pair, cached by the 64-bit
  key; a mode outside the 13 known ones is approximated and counted
  (`[NV2A] approximated`). A host test (`test_cc.py`) checks every combiner the
  game uses against a software model of the N64 combiner, the way `test_rc.py`
  checks TEV.
- **Blender**: `G_RM_*` → NV2A blend factors + alpha test (`CVG_X_ALPHA`,
  `ALPHA_CVG_SEL`, `AA_EN` cutouts become alpha test at 0.5), `FORCE_BL` → blend
  on, `Z_CMP`/`Z_UPD` → depth test/write, `ZMODE_DEC` → depth bias.
- **Fog**: per-vertex fog factor from the interpreter (Fast3D computes it in the
  RSP from `gSPFogPosition`) into `oFog`, the fog unit in LINEAR mode and the
  final combiner `lerp(fog, PREV, fog colour)` as Melee-X does.
- **Depth**: N64 16-bit Z (0..0x3FFF with dz) → Z24S8; the interpreter's `z/w`
  mapped to the 24-bit range. Decals (`ZMODE_DEC`) via polygon offset.
- **Shadow volumes** (`cimg == zimg` pass, wings64-dc's "column under the pilot"):
  the Xbox has stencil. Pass 1 (volume back faces into the z image) → stencil
  increment with colour writes off; pass 2 (front faces as dark XLU shade) →
  stencil test. Exact, where the Dreamcast needed modifier volumes.
- **Z clear** (`FILLRECT` with `cimg == zimg`) → `pb_fill` depth clear.
- **Textures**: import = TMEM deswizzle (odd rows 32-bit swapped, from
  `gfx_texconv.c`) then native NV2A formats: RGBA16 → A1R5G5B5, IA16/IA8/IA4 →
  A8Y8, I8/I4 → AY8 (alpha = intensity, as the RDP's I formats), RGBA32 →
  A8R8G8B8. Non-power-of-two sizes padded to the next power of two with the
  coordinates rescaled (OpenCrossing), or linear-pitch textures where no wrap is
  needed. Cache keyed by RAM address + tile descriptor (1024 entries), wiped on
  level load (the game reuses its heap). Mip levels: Phase A samples the level
  the DL loads (TEXEL1 = TEXEL0, as Birdman64 and wings64-dc do); Phase B option
  behind `XGFX_MIPS`: upload all six UVTX levels and let the NV2A trilinear.
- **Rects**: `TEXRECT`/`FILLRECT` → screen-space quads through the same path
  (PW64's texrect encoding is E4 + B3 + B2, `kb/traps.md`).
- **Framebuffer tricks**: `uvCopyFrameBuf` (photo album) → GPU/CPU readback of
  the content rect into a texture (Melee-X's EFB-copy machinery, `read_rect_cpu`);
  snow's CPU framebuffer writes → the N64 colour image lives in the arena, so the
  interpreter uploads the dirty 320x240 RGBA16 region as a texture over the frame
  when the game wrote to it (dirty flag set by the arena's write path), else
  `DIVERGES`. The album's "persistent frame buffer" (patch 028) is simpler here:
  no render scaler, readback is plain.
- **Output geometry**: the N64's 320x240 maps onto a *content rect* (Melee-X): all
  of 640x480 (4:3), all of 1280x720 / 1920x1080 (16:9), or a pillarboxed 4:3 rect
  inside a 16:9 framebuffer when widescreen is Off. See §3.2 for 16:9.

Phase B (M9, by profile only): move transform and lighting into a vertex program
(the N64's lights and texgen fit the NV2A fixed-function model); native geometry
baking at load (wings64-dc D4 phase B) is **not** planned: the NV2A is not the
bottleneck for this game.

### D5. OS shim: real kernel threads, libultra semantics preserved

PW64 has few threads (kernel 0xC, render 0xFA, app 0xA, scheduler, audio, VI/PI
managers) and a message-driven scheduler. Map `osCreateThread` → `xhw_thread_start`
(nxdk/`PsCreateSystemThreadEx`) with distinct priorities in libultra order (render
highest), `OSMesgQueue` → ring + kernel event with real blocking, `osSetEventMesg(VI)`
posted by the frame pacer (`xhw_wait_vblank`, 60.000 Hz as Melee-X's `vi.c`), the
SP graphics task run synchronously inside `osSpTaskStartGo` on the calling thread
(interpret → pushbuffer → present) then SP/DP done posted, `osPiStartDma` =
`memcpy` + post `OSIoMesg`, `osGetTime`/`osGetCount` from rdtsc scaled to the N64's
46.875 MHz count, `osSetIntMask`/`__osDisableInt` → a recursive spinlock (Melee-X's
interrupt lock), MIO0 in C, `osMemSize = 0x400000`, `osTvType = NTSC`, `osViModeTable`.
**Time comes from the vblank and the ACPI timer, never from an assumed CPU
clock** (maintainer decision 2026-10-15: upgraded and overclocked CPUs must work).
Frame pacing is the retrace (`xhw_wait_vblank`, 60.000 Hz), audio pacing is the
AC97's consumption, and `osGetTime`/`osGetCount`/`osSetTimer` are derived from
rdtsc **calibrated at boot** against the ACPI power-management timer (3.375 MHz
from a crystal, independent of the CPU; OpenCrossing `docs/traps.md` "clock":
nxdk's `KeQueryPerformanceFrequency` guesses the CPU clock from multiplier and
FSB with a 733 MHz fallback and is wrong on a CPU upgrade). The calibration is
re-checked every few seconds against the retrace count and logged as `[CLOCK] cpu
N MHz` (a 1.0/1.4 GHz Tualatin or a modchip's overclock shows up here; the game
must run identically). Two more hazards from those ports: some BIOS/modchip
firmware rewrites `rdtsc` (`0f 31`) into `int 2Eh` in titles on upgraded CPUs
(OpenCrossing issue #3, `xbox_code_repair.c`): the clock code tolerates it by
reading time through one function the repair step can verify, and nothing in a
hot loop depends on rdtsc; and a faster CPU finishes a game tick early,
which only means more idle waiting at the retrace, never a faster game.

The Xbox kernel is strictly priority-preemptive (unlike KOS v2.2.2's tick
scheduling that cost wings64-dc 44 % of every frame), but patches 022 and 023 stay:
a spinning kernel idle thread still burns a core the renderer wants. Fallback if
libultra priority semantics prove load-bearing: a cooperative coroutine shim
(ssb64-dc `sysshim.c` model, one host thread), same API.

### D6. Audio: libultra's own synthesizer, the RSP audio microcode in C

The decomp's `src/libultra/audio` (sequence player, sound player, **synthesizer**,
envelopes, resampler bookkeeping) compiles unchanged and produces Acmd lists for
`aspMain`. Write `asp_hle.c`: a C interpreter of the audio ABI (`include/libultra/PR/abi.h`,
MIT): `A_ADPCM`, `A_RESAMPLE`, `A_ENVMIXER`, `A_MIXER`, `A_LOADBUFF`/`A_SAVEBUFF`,
`A_CLEARBUFF`, `A_SETBUFF`, `A_SETVOL`, `A_LOADADPCM`, `A_SETLOOP`, `A_DMEMMOVE`,
`A_INTERLEAVE`, `A_POLEF`, `A_SEGMENT`, over a 4 KB DMEM model, run at `osSpTaskStartGo`
time for audio tasks. Output: 22050 Hz 16-bit stereo into the AC97 pump (Melee-X's
`xhw_audio.c`, resampling to 48 kHz; APU voice under xemu). Birdman64's
`crates/pw64-audio/src/abi.rs` (MIT, 611 lines) is the cross-reference for each
command's exact arithmetic (reverb included), with sm64-port's `src/pc/mixer.c`
(portable + SSE4.1 paths, unlicensed: read, don't copy) as a second reading;
mupen64plus-rsp-hle is **not** a source (GPL). CPU cost estimate: 32 voices × 22050 Hz ADPCM + resample + envelope
mix ≈ 3–5 % of the P3; measure in M6 (`[PERF] audio`).

The AI model: `osAiSetNextBuffer`/`osAiGetLength` as wings64-dc's virtual DAC
(the playing buffer only, deficit capped), `osAiSetFrequency(22050)` honoured by
the pump's resampler. The DMA of samples from `audio_tbl` is a `memcpy` from the
ROM image.

Fallback (kill switch, M6 risk): wings64-dc's cut point (replace the alSyn voice
API, `aica_synth.c` lineage) with a software voice mixer on the Xbox. Less exact
(no reverb), but proven for this game.

### D7. Saves and settings on E:

- EEPROM: the 0x800-byte image as `E:\UDATA\<title id>\pilotwings64.eep`, the
  byte order emulators use, so a Project64/Mupen save moves both ways (as Melee-X's
  `.gci` interchange). Written through `xhw_replace_file` (temp, read back, rename);
  a dirty flag and a writer that runs after `osEepromLongWrite` returns, as the
  N64's asynchronous write did. Missing or short file: the game formats it
  ("file_load: checksum failure" once is normal, `kb/traps.md`).
- Port settings: `settings.ini` in the same folder (Melee-X `settings.c`), not the
  EEPROM tail the Dreamcast used (no VMU constraint here). Keys in §3.4.

### D8. Input

N64 pad → Xbox (Duke and S), player 1 only (the game is single-player; ports 2–4
are read but ignored), remappable per `settings.ini` as Melee-X. Default layout
(maintainer 2026-10-15: "whatever makes sense"; this is it, by position and by
Pilotwings' use of the buttons):

| N64 | Xbox |
|---|---|
| control stick | left stick: a **radial dead zone** (Settings row, 0–60 % in steps of 5, default 20 %, `stick_deadzone` in `settings.ini`), then the remaining range rescaled so full tilt reaches ±80, the N64's usable rim, so a large dead zone costs no top speed; Melee-X's `pad.c` code. No automatic calibration (OpenCrossing tried it: a worn stick's rest point moves after every release, so a learned value walks the vehicle on its own); the live readout in the menu is how a user picks the value for their pad |
| A | A |
| B | X (where the N64 B sits relative to A) |
| C-up / C-down / C-left / C-right | right stick past ~50 %, and Y / B / White / Black |
| Z | left trigger |
| R | right trigger |
| L | (none by default; remappable) |
| Start | Start |
| D-pad | D-pad |
| BACK (title screen) | opens the port Settings panel, same as choosing "Settings" |
| L + R + BACK + BLACK | quit to the dashboard (Melee-X) |

### D9. ROM handling (§3.1 has the full procedure)

Any file next to `default.xbe` whose extension is `.z64`, `.n64`, `.v64`, `.rom`
or `.bin` (the decomp's `baserom.us.z64` included) is a candidate; byte order is
detected from the first word, the image normalised in RAM, the header parsed, and
the SHA-1 of the normalised image compared with the known-good table. Nothing runs
on an unvalidated ROM.

### D10. Video (§3.2–3.3 have the rules)

480i/480p at 640x480x32 + Z24S8; 720p at 1280x720 R5G6B5 + Z24S8; 1080i at
1920x1080 R5G6B5 + Z24S8 (Z16 retry, then 720p, then 480 if contiguous memory
runs out). The dashboard allows, the settings choose, and a mode that cannot be
confirmed falls back to 480i 4:3 (§3.3). 16:9 is hor+ at the projection with the
HUD kept in its 4:3 box (§3.2).

### D11. Testing ladder

Host tests without a ROM (`tools/xbox/test_*.py`, CI): ROM checker on synthetic
images, texture import vs the N64 formats, TMEM deswizzle, combiner compiler vs a
software combiner model, MIO0 vs reference, `PW64_SWAP` layouts vs `sizeof`, Acmd
HLE commands vs reference decodes (ADPCM from the decomp's codebook math), settings
parser round trip. xemu with autopad scenarios for smoke, screenshots and
`[FBDUMP]` comparisons (Melee-X's lockstep idea: `env MX_LOCKSTEP=1` so frame N is
tick N). **Hardware is truth** for timing, audio, memory and the video modes: the
maintainer's two consoles (red: HDTV pack; gold: 480i only) over FTP with
`console.py`; logs pulled before every relaunch. Tree checks: no ROM material
(`rom_leak_check.py`), every `DIVERGES` has a reason, every patch has a `.md`.

### D12. Documentation set

`CLAUDE.md` (index + hard rules), `docs/README.md`, `docs/handoff.md`,
`docs/decisions.md` (with an "Edits to imported code" table that lists every
patch), `docs/renderer.md`, `docs/platform.md`, `docs/testing.md` (switch table),
`docs/toolchain.md`, `docs/architecture.md` (memory budget), `docs/rom.md`
(validation, provenance, never commit). Same shape as Melee-X so an agent moving
between the ports finds things where it expects them.

---

## 3. The requirements, as designed

### 3.1 ROM validation (critical)

Files: `xbox/src/hw/xhw_rom.c` (find, load, normalise, hash, report),
`xbox/include/xrom.h` (result struct: order, title, game code, region, version,
CRCs, SHA-1, verdict), `tools/xbox/test_rom_check.py` + `tests/xbox/test_rom_check.c`
(compiles `xhw_rom.c`'s pure functions on the host).

1. **Find**: first file in D:\ (the XBE's folder) with an accepted extension, any
   name. None → the "No ROM found" card (modelled on Melee-X's `fatal_no_disc`):
   what is needed, where to put it, accepted extensions. 30 s, then the dashboard.
2. **Size**: must be exactly 8,388,608 bytes. Anything else → "This is not a
   Pilotwings 64 ROM (size N bytes; expected 8 MB)". No partial reads.
3. **Load** the file into the ROM buffer with the splash progress bar advancing
   (`xhw_splash_progress`), committing the destination first (`xhw_commit`: the
   kernel's file system can't take a demand-commit fault, Melee-X).
4. **Byte order** from the first four bytes: `80 37 12 40` → `.z64` (big-endian,
   as is); `37 80 40 12` → `.v64` (16-bit byte-swapped: swap every pair);
   `40 12 37 80` → `.n64` (32-bit little-endian words: reverse every 4 bytes).
   Anything else → "Not an N64 ROM (bad header)". The extension is only a hint;
   the bytes decide. The normalised image is what everything else reads.
5. **Header**: title at 0x20 (20 bytes, "Pilot Wings64"), game code at 0x3B–0x3E
   (`NPWE`), region = byte 0x3E (`E` USA, `J` Japan, `P` Europe), version at 0x3F,
   CRC1/CRC2 at 0x10/0x14. Logged as `[ROM] title "..." code NPWE region E v0
   crc 0x... 0x... order z64`. A different game code → "This ROM is not
   Pilotwings 64 (game code XXXX)". `NPWJ`/`NPWP` → "Japanese/European
   Pilotwings 64 is not supported; the port is built from the US release (NPWE)".
6. **Hash**: SHA-1 over all 8 MB of the normalised image (a straightforward C
   SHA-1 in `xhw_sha1.c`; ~0.2 s on the P3, under the splash). Compare with the
   table in `xrom_known.h`: `{ "NPWE", 0, "ec771aed...191a", "8b346182...c5ef", "US v1.0" }`.
   Match → `[ROM] verified: US v1.0 SHA-1 ok`, proceed. Right header, wrong hash
   → "Bad or modified dump: SHA-1 is <40 hex>, expected <40 hex>. Re-dump your
   cartridge." The hash is shown so a user can compare it against their own tools.
   Overdumps, trimmed ROMs, byteswapped-then-truncated files all fail at step 2 or 6.
7. **Never proceed** without step 6 passing. The error card lists the reason,
   the file name, the detected order and header, "Press any button or wait to
   return to the dashboard"; `boot.log` carries the same lines; `lastexit.txt`
   says "ROM check failed (<reason>)".
8. **Test**: `test_rom_check.py` builds a random 8 MB image with a fake header,
   writes it as z64/v64/n64, checks the normalised bytes are identical, then
   corrupts size, header, game code, region and one byte of content and asserts
   each verdict and message. The known-good SHA-1 lives only as a constant: no ROM
   bytes in the repo; `rom_leak_check.py` rejects any file containing the ROM's
   header bytes or any `.z64/.n64/.v64/.eep` file.

### 3.2 Video modes, aspect ratio and HUD scaling

Rules (Melee-X `xhw_video.c` extended):

| mode | framebuffer | when |
|---|---|---|
| 480i | 640x480x32 + Z24S8, 4:3 or 16:9 anamorphic | always available; the safe mode |
| 480p | same | HDTV/VGA pack, dashboard 480p flag, `progressive = 1`, NTSC (nxdk has no PAL progressive) |
| 720p | 1280x720 R5G6B5 + Z24S8, always 16:9 | HDTV/VGA pack, dashboard 720p flag, `output = 720p` or Auto, ≥ 32 MB free |
| 1080i | 1920x1080 R5G6B5 + Z24S8 (Z16 retry), always 16:9 | HDTV/VGA pack, dashboard 1080i flag (EEPROM video flags bit 0x00040000, next to 720p's 0x00020000 and 480p's 0x00080000), `output = 1080i`, ≥ 40 MB free |

Auto picks the highest the dashboard allows, **1080i included** (maintainer
decision 2026-10-15). The confirmation net of §3.3 covers a TV that takes the
dashboard's 1080i flag but shows nothing, and the Video output row lets a user pin
720p if 1080i flickers on their set. The settings only allow a mode; the dashboard has to allow it
too; `[VIDEO] dashboard: encoder %08x, AV pack %u, widescreen %d, 480p %d, 720p
%d, 1080i %d` is logged at every boot.

Aspect and scaling: the game draws in a 320x240 space. The interpreter maps it
onto the content rect; at 4:3 that is the whole framebuffer. For 16:9:

- **3D** is hor+: the projection matrix the game loads is widened by 3/4 on its x
  column (the standard sm64ex/Birdman64 trick), so the vertical field of view and
  the HUD's size are unchanged and more world is visible left and right. The
  game's own culling is for a 4:3 frustum (the camera arrays of patch 020), so
  objects may pop at the edges; Birdman64's widescreen patches widen the culling
  too and come in with the HUD patches below.
- **2D**: the flight HUD is **anchored to the 16:9 edges** (maintainer decision
  2026-10-15) with Birdman64's HUD patches (`hud.c`, `chan.c`, `code_7150.c`,
  `code_D2B10.c`, `font.c`, MIT) imported into `patches/` and re-guarded: the game
  itself places HUD elements relative to a wider logical screen, so the
  interpreter's 2D mapping is a plain scale of the logical width onto the
  framebuffer. Menus, text screens and other 2D that the patches do not touch
  keep their 4:3 proportions inside a centred box (960x720 in 1280x720; 1440x1080
  in 1920x1080; 480x480 of a 640x480 anamorphic frame). Full-screen 2D (title
  backdrops, SUBSCREEN fades, the sky strip, the results letterbox) is stretched
  to the full framebuffer: the interpreter knows these by size (a rect covering
  ≥ 300x220 of the 320x240 space) and by the DC port's list
  (`WINGS64_OVERLAY_FULL`, `WINGS64_TITLE_FIT` cases). Pillarbox bars are black.
  First step in M7 is the centred box alone (no game-code change); the HUD
  patches land in the same milestone once the box is right.
- **Widescreen is a user toggle** (maintainer 2026-10-15), never only the
  dashboard's choice: the Settings row "Widescreen" has **On**, **Off** and
  **Auto** (the dashboard's flag at 480; 16:9 at 720p/1080i), stored as
  `widescreen = on|off|auto`. On at 480 renders 16:9 hor+ into the 640x480 frame
  (anamorphic; a 16:9 TV set to "full" undoes the squeeze) even when the
  dashboard says 4:3, as OpenCrossing offers; Off at 720p/1080i renders a 4:3
  content rect centred in the 16:9 framebuffer with black bars, no hor+. Hold
  BACK at boot forces Off for that boot and saves it.
- **It applies at once, no restart**: the framebuffer and video mode do not
  change, only the content rect, the projection widening and the 2D mapping,
  which the interpreter reads per frame. So the toggle can be tried on the
  title screen and seen immediately when the panel closes, and `[VIDEO]
  widescreen on|off (auto: dashboard 16:9|4:3)` is logged on each change.
- The menu shows the result ("Auto, 16:9") as OpenCrossing's does.

Kill switches: `-DXGFX_HORPLUS=0` (16:9 as a plain stretch), `-DXGFX_HUD_BOX=0`
(2D stretched with the frame), `-DXHW_VIDEO_1080I=0`.

### 3.3 Splash and the safe display fallback

The splash is Melee-X's `xhw_splash.c` unchanged in look and behaviour: it runs
in the dashboard's own mode (`XVideoSetMode(640,480,32)`), so it shows on any TV,
and it is where the ROM load bar lives. Three independent safety nets, all
logged:

1. **Hold BACK at boot** (Melee-X `xsdk_early`): this boot runs 480i 4:3 and
   saves `output = 480i`, `progressive = 0`, `widescreen = off`; the splash's
   bottom line says so. The controllers are opened before the splash
   for it (OpenCrossing's finding: wait ~1.5 s for enumeration without a splash).
2. **Start-up failure** (Melee-X `xhw_video_fallback_480`): if `pb_init` or the
   contiguous allocations for 1080i/720p fail, retry with Z16, then the next mode
   down, then 640x480; never a black screen because of memory.
3. **Unconfirmed mode** (new): when the mode about to be set differs from the last
   mode that was *confirmed*, write `E:\UDATA\<id>\video_pending.txt` before
   `XVideoSetMode`, and draw a prompt over the first frames: "New video mode
   <mode>. Press A to keep it. Reverting to 480i in 15 s." A press confirms
   (`video_confirmed = <mode>` in `settings.ini`, the file deleted); no press →
   `xhw_video_fallback_480` at once (the mode switches live: the renderer tears
   down and re-inits pbkit at 640x480, as the failure path does) and the
   settings are rewritten to 480i. If the console is reset or powered off with
   the pending file present (a TV that showed nothing and a user who gave up),
   the next boot sees the file, boots 480i and saves 480i. 480i 4:3 is never
   put on probation. The prompt is CPU text over the finished frame
   (Melee-X `xhw_overlay.c`), so it needs no game state.

### 3.4 In-game settings menu (the game's look; Melee-X's semantics)

As wings64-dc's patch 021 and `settings.c`, re-targeted: a third title item
"Settings" below "Start game" and "Option" (the title's dark box grows a row,
`menuCreateItems` with 3 items, `menuSetItem` for the glyph string built from
font 6's bold set), `gameUpdateStateOptions` runs the port panel instead of
`optionsTopRender` when the title asked for it, `uvGfxEnd` calls the port's frame
hook (FPS counter), `uvFontOverlayStr_x` prints one string in a game font without
disturbing the game's pending messages. The panel uses the Option screen's level
and backdrop (`options_80316B80`, `optionsDrawBorder`), the yellow cursor, chime,
confirm/cancel sounds and the 0.75 stick threshold of `menuCheckInputs`, values in
a column via the font's `0xFD` x code, flipping with sound `0x54` like the Sound
panel's Stereo/Mono row. BACK on the title opens the same panel. Rows:

| row | values | applies |
|---|---|---|
| Video output | Auto, 480i, 480p, 720p, 1080i; shown as "Auto, 720p" / "480p -> 480i" when the dashboard disallows | restart (`*`) |
| Widescreen | On, Off, Auto; shown as "Auto, 16:9" / "Auto, 4:3" | **at once** (content rect, hor+ and 2D mapping are per-frame state; §3.2) |
| FPS counter | On, Off | at once (drawn in the HUD number font at the top, as the DC's) |
| Screenshots (BACK) | On, Off | at once; row present in test builds only |
| Stick dead zone | 0–60 %, steps of 5, default 20 %, with a live stick readout next to the value (the raw stick's current tilt as a percentage, as Melee-X's and OpenCrossing's rows show) | at once |
| Buttons | Default, (file) | read-only hint that `settings.ini` has the map |
| Save and restart | relaunches the XBE (`xhw_reboot_self`) when a `*` row changed | |
| Return | saves `settings.ini` (B too) | |

`settings.ini` keys: `[video] output = auto|480i|480p|720p|1080i`, `progressive`,
`widescreen = auto|on|off`, `video_confirmed`, `fps`; `[system] screenshots`;
`[input] stick_deadzone`, `[port1] a = A ...` (N64 names `A B Z L R START CUP CDOWN
CLEFT CRIGHT UP DOWN LEFT RIGHT NONE`). Written on first boot, atomic replace.

Kill switch `WINGS64_SETTINGS_MENU=0` compiles the four hooks out; the FPS counter
has a `-DXHW_FPS_DEFAULT=1` for test builds (Melee-X `XHW_TEST_BUILD`).

---

## 4. Milestones

Each milestone ends with a `default.xbe` that boots on the console and a
`docs/handoff.md` entry. Verification lists what the maintainer sees; "xemu" means
also run the smoke script, "console" means a real Xbox (480i on gold, HD on red).

### M0 — Toolchain, skeleton, splash, first XBE
- **Goal**: an XBE that boots to the "TechProGabe Presents..." splash and the
  "No ROM found" card, with logs, watchdog and crash reporter wired.
- **Files**: `xbox/CMakeLists.txt`, `xbox/build.sh`, `tools/xbox/setup.sh`,
  `tools/xbox/docker/{Dockerfile,build.sh}`, `tools/xbox/msys/build.sh`
  (Melee-X's minus `disc_lower`); `xbox/src/hw/`: `xhw_main.c`, `xhw_splash.c`,
  `xhw_sys.c`, `xhw_video.c` (480 only for now), `xhw_pad.c`, `xhw_watchdog.c`,
  `xhw_crash.c`, `xhw_string.c`, `xhw_fbdump.c`, `xhw_autopad.c` (copied from
  Melee-X with the Melee-specific lines removed), `xhw_ramlock.c` (64 MB
  enforced, D2), `xhw_clock.c` (ACPI-calibrated time, `[CLOCK]` line, D5);
  `xbox/include/xhw.h`;
  `xbox/assets/logo.png` + `tools/xbox/{make_logo.py,xbe_title_image.py}` with a
  new title ID (`57580001`, "WX"; §7); `tools/xbox/xemu_run.sh`,
  `fbdump_to_png.py`, `console.py`, `sym.py`; `.gitignore` (ROMs, `build-xbox/`,
  `external/`); `CLAUDE.md`, `docs/` skeleton; `.github/workflows/build.yml`
  (build + host tests, manual).
- **Depends on**: nothing.
- **Risks / open**: nxdk pin builds on the CI runner (Melee-X's cache keyed on
  `setup.sh` works); the title ID must not collide.
- **Verify**: xemu: splash, then the card, then quit (`[BOOT]` lines, `[FBDUMP]`
  shot of the splash via `-DXHW_SPLASH_DUMP`); xemu at 128 MB (Cerbios) logs the
  upper 64 MB held. Console: the same, `boot.log` in `E:\UDATA\57580001\`,
  `[CLOCK]` within 1 % of the console's known CPU clock, icon in the dashboard.

### M1 — ROM validation
- **Goal**: §3.1 complete; the game is not linked yet, so a valid ROM ends on a
  debug card "ROM verified: US v1.0" with the header fields, then the dashboard.
- **Files**: `xbox/src/hw/xhw_rom.c`, `xhw_sha1.c`, `xbox/include/xrom.h`,
  `xrom_known.h`; `tests/xbox/test_rom_check.c`, `tools/xbox/test_rom_check.py`;
  `tools/xbox/rom_leak_check.py`; `docs/rom.md`.
- **Depends on**: M0.
- **Risks / open**: load time of 8 MB from a FATX HDD over `ReadFile` (~1–2 s;
  fine) and from DVD (~5 s; the progress bar covers it); SHA-1 speed on the P3
  (expect < 0.3 s).
- **Verify**: host test green in CI (every verdict). xemu with `MX_ROM=` set to a
  z64, a v64 and an n64 conversion of the same image (`tools/xbox/rom_order.py`
  makes them; it is a converter, not a ROM): all three verify with identical
  SHA-1; a truncated copy, a renamed other game and a one-byte-corrupted copy
  each show their card. Console: the same three files by FTP.

### M2 — The decomp compiles and links
- **Goal**: `external/pw64-decomp` fetched at the pin and patched; `src/app`,
  `src/kernel`, `src/libultra/{audio,sp}` compile with the game triple; every
  libultra OS/IO symbol the game needs is stubbed; the XBE links (bootproc
  reachable, not called).
- **Files**: `tools/xbox/fetch_decomp.sh` (pin, patches in name order,
  `--refresh`); `patches/` imported from wings64-dc 001–024 and 026–028 with the
  guard renamed `WINGS64_XBOX` (one `sed`, recorded in each `.md`; 025 and the
  SD-log parts of 021 dropped); `xbox/include/game/` shadow headers and
  `wings64_prelude.h` (`PW64_SWAP`, `N64_K0`, `pw64_fatal`); `xbox/src/game/rom/`
  `pw64_swap.c`, `pw64_rom_structs.c`, `layout_checks.c`, `pw64_audio_load.c`,
  `rdram.c`; `xbox/src/game/ultra/stubs.c` (every `os*`/`__os*` symbol as
  `pw64_fatal("TODO")`); CMake: a `game_objs` target compiling the decomp list
  (`tools/xbox/compile_game.py` from Melee-X, without the lowering steps);
  `docs/decisions.md` "Edits to imported code" table.
- **Depends on**: M0.
- **Risks / open**: clang vs GCC differences on the patches (wings64-dc built them
  with GCC 14; expect `-Wno-*` churn, not logic); `va_start` on promoted types
  (`chan.c:360`, `audio_emitter.c:174`, `code_1050.c:43`) must compile under
  clang; `_uvMediaCopy`'s deliberate fault store (patch 011); struct layouts: a
  host `layout_checks.c` asserts the sizes the patches assume (Birdman64's audit)
  under the game triple.
- **Verify**: `xbox/build.sh` links; `layout_checks` passes at compile time;
  `nm` shows no undefined symbols; the M1 XBE still boots (the decomp is linked
  but idle).

### M3 — Boots to the title and menus
- **Goal**: the game runs: title, file menu, pilot select, level select, Option
  screen, at 640x480 4:3 (480i/480p), textures right, text right, saves persist.
- **Files**: `xbox/src/game/ultra/os_shim.c` (D5: threads, mesg queues, events,
  timers, PI, VI, SP dispatch, AI stubs, SI, EEPROM → `pilotwings64.eep`,
  `osViModeTable`), `mio0.c`; `xbox/src/gfx/f3d_interp.c`, `f3d_gbi.h`,
  `texconv.c`, `tmem.c`, `xbox/include/xgfx.h`; `xbox/src/hw/nv2a.c` (Melee-X's
  back end cut down: pass-through vertex program, texture pool, vertex ring,
  present, GPU-fault dump), `nv2a_cc.c`, `nv2a_fog.c`; `xbox/src/sdk/`-style
  glue `vi.c` (frame pacing, retrace event) and `pad.c` (N64 mapping); host tests
  `test_texconv.py`, `test_cc.py`, `test_mio0.py`, `test_swap.py`; `settings.c`
  (file only, no menu yet); `-DXGFX_BOOT_LEVEL` knob to skip menus;
  ThinLTO (D3): `tools/xbox/thinlto_link.py` from Melee-X, `XBOX_LTO` in
  `compile_game.py` and `xbox/CMakeLists.txt`, the title reached with LTO on and
  off (same screenshots).
- **Depends on**: M2.
- **Risks / open**: the thread model (D5 fallback); combiner coverage (the 13
  modes enumerated from Birdman64's `gfx.md`; an unknown one logs and draws
  flat); depth mapping for the title's models; Mtx halfword order on a
  little-endian host (traps: element e is halfword e; wings64-dc paid for it);
  the EEPROM first-boot format path.
- **Verify**: xemu autopad script `title`: boot → title → file menu → level
  select, `[FBDUMP]` shots compared with N64 reference captures by eye (and kept
  as the baseline for lockstep comparisons later). Console: the same by hand,
  `boot.log` clean (no `[NV2A] fault`, no approximated combiners), a save file
  appears and survives a reboot.

### M4 — One mission playable
- **Goal**: Rocket Belt Beginner Class A from level select to the results screen,
  30 fps minimum on the console, correct terrain, sea, sky, fog, shadows, HUD.
- **Files**: `f3d_interp.c` (terrain UVTR/UVCT, fog, texgen env-map, far-plane
  clip, `cimg==zimg` paths), `nv2a.c` (stencil shadow volumes, depth bias
  tie-break, Z clear), `nv2a_cc.c` (all 13 modes exact, `TEXEL0*TEXEL1` sea
  product without the DC's baked texture: two texture units do it natively),
  `pad.c` (per-vehicle feel), `xhw_perf.c` (`[PERF]`: interpreter, pushbuffer,
  present, audio per frame), the endian sites outside the kernel (patches 013–016
  already cover them; verify each on x86).
- **Depends on**: M3.
- **Risks / open**: pop-in from the game's own culling (patch 020); z-fighting
  on coplanar pad layers (the DC's `WINGS64_Z_TIEBREAK` draw-order bias ported
  as a polygon offset); the snow framebuffer writes; `uvCopyFrameBuf`.
- **Verify**: xemu scenario `rb_a` (scripted flight to the results, shots at
  take-off, mid-flight, landing, results). Console: the maintainer flies it;
  `[PERF]` shows the frame budget; the FPS counter (test build) reads ≥ 30; BACK
  shots of the sea, horizon, landing target, shadows match the DC's confirmed
  hardware runs 8–11 (`kb/STATE.md`'s graphics-fix table is the checklist).

### M5 — Real 60 fps
- **Goal**: flight, menus and replays at a locked 60 fps with the game's physics,
  timers and sound cues unchanged (maintainer decision 2026-10-15: visible
  60 fps, not a doubled 30). The N64 game steps its simulation once per frame it
  draws, so drawing twice as often needs the simulation told so.
- **Files**: `patches/` + Birdman64's ~20 rate-decoupling patches (`pw64_rate.h`,
  the per-vehicle files `birdman*`, `gyrocopter`, `hang_glider*`, `rocket_belt`,
  `cannonball`, `skydiving`, `jumble_hopper`, camera, HUD timers; MIT) imported
  one reason per patch, re-guarded, each with a `.md` that names what it scales;
  `vi.c` (one gfx task per retrace instead of every other), `settings.c` (a
  `fps_mode = 30|60` key and a Settings row "Frame rate", default 60, as the
  escape hatch while the patches are validated), `-DXGFX_FDR_HASH` oracle wired
  so the ROM's demo replays can be hashed at 30 and at 60.
- **Depends on**: M4 (a mission to measure), M3.
- **Risks / open**: first measure what the game already does: wings64-dc runs
  flight at 45–51 fps with no rate patches and the maintainer did not report a
  fast game, so part of the logic may already be time-based; the patches needed
  may be fewer than Birdman64's. Replays recorded by the game (`fdr.c`) and the
  ROM's demos assume the N64's step; at 60 they may diverge (Birdman64's notes
  say how it handles them: read before porting). Audio cues and timers tied to
  frame counts (`uvaSeqStop`, HUD countdowns) must be checked. Any drift →
  `fps_mode = 30` is one row away, and the port still presents at 60 Hz.
- **Verify**: xemu `rb_a` scenario at 30 and at 60: the same mission takes the
  same wall time to the results screen (the in-game timer agrees with a stopwatch
  within a frame), `[PERF]` shows one new display list per retrace, the demo
  replays complete and their state hashes are stable run to run. Console: the
  maintainer flies each vehicle at 60 and judges feel and speed against the
  N64 or an emulator at 30; FPS counter reads 60 in flight.

### M6 — Audio
- **Goal**: music and SFX in menus and flight, no dropouts over 10 minutes, the
  AC97 healthy (`[AUDIO] AC97 polled` once per boot).
- **Files**: `xbox/src/hw/asp_hle.c` (D6), `asp_hle.h`; `os_shim.c` AI (virtual
  DAC); `xhw_audio.c` (Melee-X's driver, 22050 → 48 kHz resampler in the pump);
  host test `test_asp_hle.py` (ADPCM decode vs the decomp's codebook math on
  synthetic frames, resampler vs a reference, envelope mixer ramps,
  `A_INTERLEAVE`); `tests/xbox/asp_ref.c`.
- **Depends on**: M3 (the audio thread and task dispatch).
- **Risks / open**: exactness of `A_RESAMPLE` and `A_ENVMIXER` (Birdman64's
  `abi.rs` is the oracle; the three lowpass reverb sections are silent on real
  hardware too, per Birdman64); the audio thread's priority vs the renderer;
  `alAudioFrame` and `osAiGetLength` traps from wings64-dc.
- **Verify**: xemu: `[AUDIO]` counters (key-ons, frames mixed, buffer
  underruns = 0) over a 10-minute attract-mode soak (`-DXHW_SOAK`); the APU
  voice under xemu plays. Console: the maintainer listens (menus, flight, SFX
  pitch) and a 10-minute soak logs no underrun or AC97 recovery. Fallback
  decision point: if `[PERF] audio` exceeds 8 % of a frame, enable the D6
  fallback mixer behind its switch and compare.

### M7 — Video modes, widescreen, settings menu, safe fallback
- **Goal**: §3.2–3.4 complete: 480i/480p/720p/1080i within the dashboard's
  allowance, 16:9 hor+ with the HUD box, the in-game Settings panel, the FPS
  counter, the three fallbacks.
- **Files**: `xhw_video.c` (1080i, Z16 retry chain, pending/confirm), `nv2a.c`
  (content rect, 16-bit paths at 1080i, pool sizes per mode, re-init for the live
  fallback), `f3d_interp.c` (hor+, 2D box, full-screen 2D detection),
  `patches/` + Birdman64's widescreen/HUD patches (re-guarded, each with a `.md`),
  `xhw_overlay.c` (confirm prompt, notices), `xbox/src/game/settings.c` (the
  panel), patch 021 (re-guarded; the hooks), `settings.c` (keys), `xsdk_early`
  BACK handling, `docs/platform.md`, `README.md` "Video".
- **Depends on**: M4 (a scene to look at), M3.
- **Risks / open**: **1080i in nxdk**: `XVideoSetMode(1920,1080,...)` must have
  a mode in nxdk's table for the HDTV pack; if not, the mode is added by a small
  pbkit/nxdk patch (`patch_pbkit.py` style) using the kernel's `AvSetDisplayMode`
  flags, and 1080i ships as "experimental" until red confirms it. Fill rate at
  1920x1080 16-bit with fog and a few overdraw layers: expect fine for ~3 k tris;
  measure `[PERF] gpu`. Interlace flicker on thin HUD lines at 1080i. The live
  fallback re-init of pbkit (Melee-X never re-inits; its fallback runs before
  the first `pb_init`): if a second `pb_init` proves unreliable on hardware, the
  unconfirmed-mode fallback becomes "reboot into 480i" via `xhw_reboot_self`
  with the pending file, which is still never a black screen for more than the
  timeout.
- **Verify**: xemu: EEPROM copies with the 480p/720p/1080i/widescreen flags
  (`eeprom_video.py` extended with 1080i), `env MX_VIDEO=` per scenario, shots
  at each mode; the panel scenario (`settings`: open, change rows, Save and
  restart, reopened values; a `widescreen` scenario toggles On/Off/Auto in the
  panel and shots show 16:9 hor+ vs the 4:3 box without a restart). Console red
  (HDTV pack): each mode photographed; hor+ shows more world with an undistorted
  HUD; Widescreen Off at 720p gives centred 4:3 with black bars, and On at 480i
  on gold (4:3 dashboard) gives the anamorphic frame; set 1080i then press nothing →
  480i after 15 s and `settings.ini` reads 480i; set 720p on gold (480i-only
  pack) → "720p -> 480i" shown in the menu and 480i at boot. BACK at boot on
  either console → 480i saved.

### M8 — Full game parity
- **Goal**: every vehicle and class (Hang Glider, Rocket Belt, Gyrocopter,
  Cannonball, Skydiving, Jumble Hopper, Birdman), every level, the photo album,
  replays, the credits; saves hold; fades, title fit and album frame right.
- **Files**: `f3d_interp.c`/`nv2a.c` burn-down (combiner edge cases, alpha
  compare, dither off, coverage AA edges skipped, canopy fringes), the
  framebuffer readback for `uvCopyFrameBuf` and the album (patch 028's hooks),
  patch 026 (fade over frozen frame) — on the NV2A the previous frame is simply
  not cleared under a fade, so 026 may become a one-line DIVERGES; EEPROM
  interchange test with an emulator save.
- **Depends on**: M4, M5, M6.
- **Risks / open**: the Class A Gyrocopter overview hang the DC saw once (watch
  `hang.log`); `uv: RSP timeout` watchdog lines on long loads (harmless on DC);
  float determinism of replays on SSE vs the VR4300 (the `fdr.c` hash oracle:
  `-DXGFX_FDR_HASH` logs a per-frame state hash while the ROM's demos play; it
  must be stable across builds, and ideally match the DC's).
- **Verify**: a completion checklist in `docs/testing.md` ticked on the console;
  the demo replays run through without divergence; a Project64 `.eep` loads and
  shows the same pilots.

### M9 — Performance and hardening
- **Goal**: 60 fps held at every mode, 1080i included, with no missed retrace in
  flight (M5 made 60 the game's rate; this milestone makes the console keep it);
  a 2-hour burn-in at 720p and at 480i with no crash, hang, leak or audio fault;
  the same behaviour on a 733 MHz console and on an upgraded CPU.
- **Files**: `xhw_prof.c` + `prof_report.py` (sampling profiler), `[MEMB]`
  memory breakdown, `[BEAT]`; LTO vs non-LTO `[PERF]` pair recorded (the LTO
  build is the default; D3); `order.txt`/PGO only if the profile shows
  the interpreter or the game thread bound; `nv2a.c` state-diff and texture
  memo; Phase B vertex program if the CPU is the limit (unlikely).
- **Depends on**: M8.
- **Risks / open**: 1080i fill rate; the AC97 pump under load; long-uptime
  leaks (Melee-X's SDL event queue lesson is already in `xhw_pad.c`).
- **Verify**: `[PERF]` on the console per scene (no missed retrace in flight);
  two burn-ins (`console_round.py` style) with logs pulled and read; `[MEMB]` flat
  over two hours; if an upgraded-CPU console is available, one run there with
  `[CLOCK]` and `[PERF]` compared.

### M10 — Release
- **Goal**: `Wings64-X-<tag>.zip` from the GitHub `build` workflow with a release
  tag: `Wings64-X/default.xbe`, `default.tbn`, `tools/make-xiso`, `README.md`,
  `LICENSE.md`; the link map attached.
- **Files**: `.github/workflows/build.yml` (release step, `XBOX_LTO=1`), `tools/xbox/package_release.py`
  (refuses `XBOX_CFLAGS` builds and non-LTO builds), `.github/release-notes.md`, `README.md`
  (install, ROM requirements, video, settings, controls, troubleshooting, in
  Melee-X's voice), `LICENSE.md`: the port's own code is **MIT** (maintainer decision
  2026-10-15); it lists the decomp (MIT), Birdman64-derived patches (MIT),
  wings64-dc-derived interpreter and shim (the maintainer's), OpenCrossing-Xbox
  (MIT) and the Melee-X-derived files, which the maintainer relicenses as their
  author (Melee-X's `xbox/` is GPL-3.0-or-later only because of melee-pc, none of
  which is used here; `LICENSE.md` must say so), `docs/` complete.
- **Depends on**: M9.
- **Verify**: the workflow run's zip installs on both consoles from scratch and
  plays a mission; the README's steps were followed literally by the maintainer.

---

## 5. Hard problems and risks

| # | problem | why hard | plan |
|---|---|---|---|
| H1 | Bit-field and struct layout of the decomp under clang | MS vs GCC layout, IDO MSB-first fields; a wrong layout is silent corruption | game triple with `-mno-ms-bitfields`; patches 001–003; `layout_checks.c` under the same triple; host tests on parsed structs |
| H2 | Fast3D → NV2A fidelity: 2-cycle combiner, blender modes, `cimg==zimg` tricks, fog, decals | the RDP is a different machine | register-combiner compiler with a host model; stencil for shadow volumes; Melee-X's depth/fog machinery; the DC's confirmed-on-hardware fix list as the acceptance checklist |
| H3 | RSP audio HLE exactness | 15 Acmd commands with fixed-point semantics; audible if wrong | Birdman64 `abi.rs` as the oracle, host tests per command, fallback voice mixer behind a switch |
| H4 | libultra scheduler semantics on Xbox kernel threads | priority-preemptive but not identical; spin loops | distinct priorities, patches 022/023, coroutine shim fallback |
| H5 | IDO-era UB on clang | crashes IDO hid | Birdman64/wings64-dc UB patch list, `-ftrivial-auto-var-init=zero`, `AVOID_UB`, `-fno-aggressive-loop-optimizations` |
| H6 | Float determinism (replays, demos), now also at 60 fps | SSE single vs VR4300 single: same IEEE results if no FMA and no excess precision; the rate patches change the step | `-mfpmath=sse -ffp-contract=off`, no `-ffast-math` in game TUs, the fdr hash oracle at 30 and 60, `fps_mode = 30` fallback |
| H7 | 1080i | nxdk mode table, memory, fill rate, flicker | opt-in, Z16 retry chain, experimental until confirmed on red |
| H8 | Safe fallback without TV feedback | the console can't see the TV | three nets: BACK at boot, init failure, 15-s confirmation with a pending file |
| H9 | 16:9 correctness | 4:3 culling, 2D vs 3D split, full-screen 2D detection | hor+ at the projection, HUD box, Birdman64's patches as step 2, kill switches |

| risk | likelihood | impact | mitigation |
|---|---|---|---|
| clang miscompiles a decomp TU at `-O2` | low | medium | per-TU `-O0` quarantine list with evidence (OpenCrossing pattern) |
| a second `pb_init` (live fallback) misbehaves on hardware | medium | medium | reboot-into-480i variant of the fallback |
| audio HLE too slow | low | medium | D6 fallback mixer |
| nxdk has no 1080i mode | medium | low | add it (kernel `AvSetDisplayMode`), ship experimental |
| title ID collides with another homebrew | low | low | check xboxdevwiki's list before M0 |
| upgraded/overclocked CPU: wrong clock guess, patched `rdtsc`, early ticks | medium | high | ACPI-calibrated time, vblank pacing, `[CLOCK]` check, OpenCrossing's code-repair lesson (D5) |
| 128 MB console behaves differently | medium | medium | `xhw_ramlock.c` holds the upper 64 MB at boot; XBE limited to 64 MB (D2) |
| J/E ROM users | n/a | low | refused with a clear card (§3.1); no plan for a JP build |
| xemu hides hardware-only rendering order | high | medium | console shots on every milestone, BACK screenshots |

---

## 6. Repository layout (to create)

```
CLAUDE.md  README.md  LICENSE.md  PLAN.md (this)  .gitignore  .github/workflows/build.yml
docs/      README.md handoff.md decisions.md architecture.md renderer.md platform.md testing.md toolchain.md rom.md
patches/   NNN-<reason>.patch + .md (imported from wings64-dc, guard WINGS64_XBOX), README.md
external/  pw64-decomp/ (gitignored; tools/xbox/fetch_decomp.sh)
xbox/      CMakeLists.txt build.sh order.txt (later) assets/logo.png
  include/ xhw.h xgfx.h xrom.h xrom_known.h xsdk.h xsdk_settings.h game/ (shadow headers, wings64_prelude.h)
  src/hw/  xhw_main.c xhw_splash.c xhw_video.c xhw_overlay.c xhw_rom.c xhw_sha1.c xhw_pad.c xhw_audio.c
           xhw_sys.c xhw_log.c xhw_watchdog.c xhw_crash.c xhw_prof.c xhw_perf.c xhw_fbdump.c xhw_autopad.c
           xhw_string.c nv2a.c nv2a_cc.c nv2a_fog.c nv2a_vp.c asp_hle.c
  src/gfx/ f3d_interp.c f3d_gbi.h texconv.c tmem.c
  src/game/ ultra/{os_shim.c,mio0.c,stubs.c,ultra_data.c} rom/{pw64_swap.c,pw64_rom_structs.c,layout_checks.c,
            pw64_audio_load.c,rdram.c} settings.c vi.c pad.c eeprom.c
tests/xbox/ test_rom_check.c test_cc.c cc_ref.c test_texconv.c test_asp_hle.c asp_ref.c
tools/xbox/ setup.sh docker/ msys/ compile_game.py fetch_decomp.sh xemu_run.sh fbdump_to_png.py console.py
            console_round.py sym.py prof_report.py eeprom_video.py rom_order.py rom_leak_check.py
            xbe_title_image.py make_logo.py package_release.py test_*.py scenarios/
```

---

## 7. Maintainer decisions

### Taken (2026-10-15)

| question | decision | where it lands |
|---|---|---|
| Frame rate | **real 60 fps** with Birdman64's rate-decoupling patches, validated against the replay oracle; a 30/60 Settings row as the escape hatch (clarified 2026-10-15 after "present at 60, logic at 30" turned out to mean a doubled 30 fps picture) | M5, M9, D5 |
| Button layout | the D8 table ("whatever makes sense") | D8, M3 |
| 1080i in Auto | **yes**, Auto tops out at 1080i where the dashboard allows it | §3.2, M7 |
| Widescreen | **a Settings toggle** (On / Off / Auto) at every output, applied at once without a restart; not left to the dashboard | §3.2, §3.4, M7 |
| Stick dead zone | **a Settings row** (0–60 %, default 20 %, live readout), applied at once; radial, with the range rescaled to the N64 rim | D8, §3.4, M3 |
| J/E ROMs | **refused** with a clear card; no JP build planned | §3.1, M1 |
| Memory | **64 MB enforced**: 128 MB consoles run as 64 MB, held at boot | D2, M0 |
| CPU | **upgraded/overclocked CPUs must work**: time from the ACPI timer and the vblank, never an assumed 733 MHz; `[CLOCK]` logged | D5, M0, M9 |
| Audio | **RSP audio HLE first**, the voice-mixer cut as the fallback switch | D6, M6 |
| 16:9 HUD | **anchored to the 16:9 edges** with Birdman64's HUD patches, after the centred box works | §3.2, M7 |
| Licence | **MIT** for the port's own code; Melee-X-derived files relicensed by their author | M10 |
| LTO | **ThinLTO required**: on by default (`XBOX_LTO=1`), releases refuse a non-LTO build, `XBOX_LTO=0` only as the diagnosis kill switch (added 2026-10-15) | D3, M3, M5, M9, M10 |

### Defaults applied unless the maintainer objects

1. **Title ID** `57580001` ("WX") and a dashboard icon in the Melee-X/OpenCrossing
   style (a glider silhouette): check xboxdevwiki's title-ID list first.
2. **Save interchange**: the EEPROM image as `pilotwings64.eep` in emulator byte
   order (Project64/Mupen compatible).
3. **Patch guard macro** `WINGS64_XBOX` (wings64-dc's `WINGS64_DC` renamed on
   import, recorded in each patch's `.md`).
4. **Video-mode confirmation timeout** 15 s (§3.3).
5. **No platform-drawn emergency menu** (Melee-X's BACK panel); BACK on the title
   opens the in-game Settings panel instead.
6. **Upstream policy**: the decomp is consumed downstream under MIT at a pinned
   commit and nothing is contributed upstream, as in wings64-dc; its
   `CONTRIBUTING.md` forbids AI-generated contributions and asks agents to
   discourage AI analysis of the project, which `CLAUDE.md` will record.

---

## 8. Seed prompt for the executing model

Paste this to start the build (and after every context flush), as wings64-dc's
`kb/RESUME.md` does:

> You are executing Wings64-X, a native original-Xbox port of Pilotwings 64 built
> from the n64decomp/pw64 decompilation (pinned at `1a8558f`) with nxdk and
> LLVM 21. Read `PLAN.md` first: it is binding on architecture (section 2), the
> requirements as designed (section 3), the milestone order and exit checks
> (section 4), and the maintainer's decisions (section 7). Then read, in the
> sibling checkouts, Melee-X's `CLAUDE.md`, `docs/platform.md`, `docs/toolchain.md`
> and `xbox/src/hw/{xhw_main.c,xhw_video.c,xhw_splash.c}`, and wings64-dc's
> `CLAUDE.md`, `kb/traps.md`, `patches/README.md`, `src/dc/gfx/gfx_f3d.c`,
> `src/dc/ultra/os_shim.c` and `src/dc/settings.c`. Work milestone by milestone
> starting at M0; each ends with a `default.xbe` that boots, a `docs/handoff.md`
> entry and a commit (ask before committing; never push to `main` without the
> maintainer's go-ahead). Hard rules: never commit ROM material or anything
> derived from it (images of game frames included); never edit
> `external/pw64-decomp/` in place, change `patches/` and re-fetch; every patch
> is `#ifdef WINGS64_XBOX`-guarded with a `.md`; every port-side departure from
> the game is marked `DIVERGES: <reason>`; every optimisation and every risky
> change has a kill switch and the default is the good build; the splash is
> Melee-X's `xhw_splash.c` as is; the settings menu is in the game's own style
> (wings64-dc patch 021); 64 MB is enforced; time comes from the ACPI timer and
> the vblank, never an assumed CPU clock; nothing runs on a ROM that has not
> passed the SHA-1 check; the decomp is consumed downstream under MIT and
> nothing is sent upstream. xemu is for crashes and pictures, not timing or
> audio: say when a result is emulator-only. Report what you verified and how,
> not what you intended.
