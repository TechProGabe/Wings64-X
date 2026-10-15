# Documentation index

Start with `CLAUDE.md` at the repository root (the rules and the build commands)
and `PLAN.md` (the design, the requirements and the milestones). Then:

| document | what it is |
|---|---|
| [`handoff.md`](handoff.md) | where the port is: one entry per milestone, newest first, with what was verified and how |
| [`architecture.md`](architecture.md) | the whole system: the two compiler triples, the decomp and its patches, the libultra shim, the Fast3D interpreter, the NV2A back end, input, audio, video, the settings menu, boot, memory |
| [`platform.md`](platform.md) | the Xbox platform layer (`xbox/src/hw`): boot flow, the ROM check, the clock, the RAM lock, video modes, the NV2A and pbkit, audio, pads, saves, settings and logs, exits and restarts |
| [`testing.md`](testing.md) | the test ladder, every compile-time switch with its default, the test-build tools (autopad, the xemu runner, screenshots, the console tools, symbols, the profiler, `debug.txt`) and the log lines |
| [`toolchain.md`](toolchain.md) | the build: MSYS2 and Docker, nxdk's pin, the game's flag set, `compile_game.py`, ThinLTO, the link |
| [`renderer.md`](renderer.md) | textures: TMEM conversion and padding (`texconv.c`) |
| [`rom.md`](rom.md) | the user's ROM: what is accepted, the validation procedure, why it is never committed, the tests |
| [`decisions.md`](decisions.md) | decisions and their reasons; the table of every edit to imported code (the decomp patches); the memory map and address model; the linker symbols; the libultra surface |

Outside `docs/`:

- `PLAN.md`: the plan of record. §2 is the architecture decisions (D1-D13),
  §3 the requirements (ROM validation, video modes, the splash, the settings
  menu), §4 the milestones, §7 the maintainer's decisions.
- `CLAUDE.md`: the layout table, the build and test commands, the hard rules.
- `patches/README.md`: the rules for the patches to the decomp, and the table of
  what each patch changed on import.
- `README.md`: the repository's one-line description.

## Where to look for

| question | document |
|---|---|
| how do I build, run in xemu, deploy to the console | `CLAUDE.md`, then `testing.md` (sections 2 and 4) |
| what does a log line mean | `testing.md` section 5 |
| what does a switch do, and its default | `testing.md` section 3 |
| why is this code the way it is | `decisions.md`, `PLAN.md` section 2 |
| what the boot does, step by step | `architecture.md` section 11, `platform.md` section 1 |
| where the settings and saves live | `platform.md` section 7 |
| how a patch to the decomp is made | `patches/README.md`, `architecture.md` section 3 |
| how the N64 graphics become NV2A draws | `architecture.md` sections 5-6, `renderer.md` |
| how the audio gets to the AC97 | `architecture.md` section 9, `platform.md` section 5 |
| what is not built yet | `handoff.md` (open items), `platform.md` section 10 |
