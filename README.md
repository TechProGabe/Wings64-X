# Wings64-X

Pilotwings 64 running natively on an original Xbox.

This isn't an emulator. The [n64decomp/pw64](https://github.com/n64decomp/pw64) project turned the US Pilotwings 64 back into C source, and Wings64-X compiles that code for the Xbox's Pentium III. The game's graphics are interpreted on the CPU and drawn by the Xbox's own NV2A GPU, and the N64's sound microcode is rewritten in C so the Xbox's audio chip plays it. The game data is read at runtime from a ROM you supply, the same way the N64 read its cartridge.

**Status:** in development. The title screen, the menus and flight have run in the [xemu](https://xemu.app) emulator. It has not yet been played end to end on real hardware, frame rate and full game parity are still being worked on, and a crash can happen. The status and the open items are in [docs/handoff.md](docs/handoff.md).

- Your own US copy of Pilotwings 64 (cartridge ID `NPWE`), checked by SHA-1 before anything runs
- Video at 480i, 480p, 720p or 1080i, within what your dashboard's video settings allow
- A settings panel: **Back** on the title screen (or Option > Port settings)
- Saves in the N64 cartridge's EEPROM format, the one Project64 and Mupen64Plus use
- Hold **L + R + Back + Black** on any controller to quit to the dashboard

You need your own copy of the game. Nothing from Nintendo ships with this.

## What you need

- A modded original Xbox (softmod or modchip) that runs homebrew. A stock 64 MB console is fine; a 128 MB console runs in its first 64 MB.
- A way to copy files to it, usually FTP from your dashboard (UnleashX, XBMC4Gamers, EvolutionX and friends all have a server built in).
- A **Pilotwings 64 (US)** ROM, dumped from your own cartridge: 8,388,608 bytes (8 MB), game code `NPWE`, header title "Pilot Wings64"; the port checks its SHA-1 against the US v1.0 release at boot. Any byte order works (`.z64`, `.n64`, `.v64`), and the file name doesn't matter. The accepted extensions are `.z64`, `.n64`, `.v64`, `.rom` and `.bin`. The Japanese and European releases are refused with a message saying so: the port is built from the US release only.
- A controller.

## Install

Grab the newest `Wings64-X-<version>.zip` from the [Releases](../../releases) page and unzip it. It has `Wings64-X/default.xbe` (the game), `Wings64-X/default.tbn` (the dashboard icon), `tools/make-xiso`, `README.md` and `LICENSE.md`.

**Hard drive** (easiest, loads fastest):

1. Put your ROM in the `Wings64-X` folder, next to `default.xbe`. Any name works, for example `Pilotwings 64 (U).z64`.
2. Connect to your Xbox with an FTP client like FileZilla. Your dashboard shows the Xbox's IP address; the login on most dashboards is `xbox` / `xbox`.
3. Copy the whole `Wings64-X` folder to `E:\Games\` (or anywhere your dashboard looks for games). The result must be `E:\Games\Wings64-X\default.xbe` next to the ROM.
4. Launch Wings64-X from the dashboard.

If the ROM isn't found, the port shows a "No ROM found" card with the folder it looked in and the extensions it accepts, and goes back to the dashboard after 30 seconds.

**Burned disc:** `tools/make-xiso` packs `default.xbe`, the icon and your ROM into one Xbox disc image:

```sh
tools/make-xiso Wings64-X "Pilotwings 64 (U).z64" Wings64-X.iso
```

It needs [xdvdfs](https://github.com/antangelo/xdvdfs/releases) or extract-xiso on your PATH, and runs in Git Bash, WSL, Linux or macOS. Burn `Wings64-X.iso` as an image at a low speed (ImgBurn or similar) and start it from the dashboard's disc option. The ROM is read from the disc (`D:\`), so the disc works the same as the hard drive copy. Not every Xbox DVD drive likes burned discs; if yours won't read it, use the hard drive.

**xemu:** load the ISO with Machine > Load Disc and set xemu's memory to 64 MB. It runs slower than a real Xbox.

**Updating:** copy the new `default.xbe` and `default.tbn` over the old ones. Your save and settings stay where they are.

## Saves

Saves go to the hard drive, in `E:\UDATA\57580001\pilotwings64.eep`. It holds the cartridge's 16 Kbit EEPROM in the byte order Project64 and Mupen64Plus use, so copying a save to and from an emulator is meant to work as it is. Neither that nor saves across a restart has been tested on hardware yet. A missing file starts as a blank chip, and the game formats its save files the first time you play. Each save is written to a temporary file and then swapped in, so a failed write leaves your previous save in place.

If the port can't write to `E:` (usually because it's full), it says so on screen and `boot.log` says why. Free some space and start again.

## Video

Your dashboard decides what your TV and cables can handle, and Wings64-X never goes past it.

- **480i** works on everything, composite included. It is the safe mode.
- **480p** needs an HDTV or VGA pack (component, or a VGA/HDMI adapter) and 480p turned on in the dashboard.
- **720p** needs an HDTV or VGA pack and 720p turned on in the dashboard. It's always 16:9.
- **1080i** needs an HDTV or VGA pack, 1080i turned on in the dashboard, and a 60 Hz (NTSC) TV.
- **Auto** (the default) takes the highest of these the dashboard allows. If a mode can't be set, the port drops to the next one down.

**Black screen?** Some TVs don't show the higher modes. Hold **Back** on any controller while Wings64-X starts (the splash says so). That boot comes up in 480i, 4:3, and saves 480i as your setting. Change it back in the settings panel whenever you like.

## Settings

Press **Back** on the title screen and the settings panel opens. It's also in the game's **Option** menu as "Port settings". The panel has three pages: **Video**, **Controls** and **System**. Up and down pick a row; left and right (or A) change it; B or Return goes back a page, and the settings are saved when you leave the panel. The line under the rows says what the row under the cursor does.

| page | row | what it does |
|---|---|---|
| Video | Resolution `*` | Auto, 480i, 480p, 720p or 1080i, within what your dashboard and cable allow; it shows what you'll actually get, like `720p > 480i` |
| Video | Widescreen | Off, On or Auto (the default). Auto is 16:9 at 720p and 1080i, and follows the dashboard's widescreen setting at 480. 16:9 shows more of the world at the sides, and the flight gauges move to the screen edges. Applies at once |
| Video | Texture filter | N64 (the game's own look, the default) or Sharp (crisp square pixels). Applies at once |
| Video | FPS counter | Frames per second in the top left corner. Applies at once |
| Video | Frame rate | 60 (the default) or 30, the N64's pace. Applies at once |
| Video | Apply and restart | Restarts the port to apply the rows marked `*`; it does nothing when none changed |
| Controls | Stick dead zone | 0 to 60 %, in steps of 5 (default 20 %). A live readout shows how far the stick is tilted, so you can pick a value for your controller |
| Controls | Rumble | On or off: the controller rumbles when you crash |
| System | Screenshots | Off by default. On: **Back** during play saves a picture to `E:\UDATA\57580001\` |
| System | Complete save | Writes a save with every event at full points (all gold, everything unlocked) into **File 2**. If File 2 already holds a save, press A a second time to overwrite it. File 1 is never touched |

Everything the panel changes lives in `E:\UDATA\57580001\settings.ini`, which you can also edit over FTP. It's written with the defaults on the first start. Delete it to get the defaults back.

## Controls

| Xbox | N64 |
|---|---|
| Left stick | control stick (radial dead zone, see Settings) |
| A | A |
| X | B |
| D-pad | C-buttons (up, down, left, right): the camera views |
| Right stick, pushed past half | C-buttons too |
| Left trigger | Z |
| Right trigger | R |
| Start | Start |
| Back, on the title screen | opens the settings panel |
| L + R + Back + Black | quit to the dashboard |
| L + R + Back + White | restart the port |

The L and R in the combos are the two triggers, pressed fully. Both combos work in every screen, even one that has stopped responding. Where a press went, and the reason for a quit, go to the log (see below).

## If something breaks

Logs go to `E:\UDATA\57580001\`: `boot.log` for the current start, `boot_prev.log` for the one before it, and `crash.log` if the game crashed. `lastexit.txt` says how the last run ended (a restart, a quit, a failed ROM check). Attach those, and a photo of the screen if it showed an error, when you open an issue.

**ROM refused?** The card says why: wrong size, not an N64 ROM, a different game, or a bad dump. A bad dump shows its SHA-1, so you can compare it with your own tools. Re-dump the cartridge if it doesn't match.

**No sound?** The audio has not been checked on hardware yet. If you hear nothing, attach `boot.log` and say which dashboard and TV you use.

## Building it yourself

nxdk and LLVM 21, built natively on Windows with MSYS2 or in Docker:

```sh
tools/xbox/fetch_decomp.sh        # the decomp at its pin, with the port's patches
tools/xbox/msys/build.sh          # Windows, MSYS2
tools/xbox/docker/build.sh        # Docker (Linux, macOS, Windows)
```

Both write `build-xbox/xbe/default.xbe` and `default.tbn`. Set `XBOX_LTO=0` for a plain build when diagnosing a problem; the default is ThinLTO. The setup, the tests and how the port works are in [docs/](docs/README.md), starting with [docs/toolchain.md](docs/toolchain.md) and [docs/rom.md](docs/rom.md).

## Legal

This repo has no game assets, no ROM data and no Nintendo binaries. It's the decompiled C source plus the port's code. You need your own legally dumped copy of Pilotwings 64, so please don't open issues asking for ROMs.

Not affiliated with or endorsed by Nintendo or Microsoft. Pilotwings is a trademark of Nintendo, and Xbox is a trademark of Microsoft. Licensing details are in [LICENSE.md](LICENSE.md).

## Credits

- [n64decomp/pw64](https://github.com/n64decomp/pw64), the decompilation this is all built on
- [Birdman64](https://github.com/NoahVl/Birdman64) (MIT), whose N64 structures, byte swapping and audio loader the port builds on
- [wings64-dc](https://github.com/TechProGabe/wings64-dc), the Dreamcast port whose patches, interpreter and settings menu this one starts from
- [OpenCrossing-Xbox](https://github.com/GabeConway/OpenCrossing-Xbox) (MIT), for the Xbox audio, crash reporting, CPU and timer code
- [Melee-X](https://github.com/TechProGabe/Melee-X), the Xbox port of Melee: the splash, the video modes, the settings conventions and the release tooling
- [nxdk](https://github.com/XboxDev/nxdk), [xemu](https://xemu.app) and [xdvdfs](https://github.com/antangelo/xdvdfs), the open Xbox toolchain, emulator and ISO packer

> AI tools (Claude) were used in developing this port.
