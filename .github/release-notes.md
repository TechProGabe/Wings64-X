Pilotwings 64 (US) running natively on the original Xbox. You need your own ROM, see the README.

## Install

Unzip `Wings64-X-<version>.zip` and copy the `Wings64-X` folder to `E:\Games\` on your Xbox. Put your ROM (8 MB, US `NPWE`, `.z64`, `.n64`, `.v64`, `.rom` or `.bin`) in the folder next to `default.xbe`, then launch it from the dashboard. If you'd rather burn a disc, `tools/make-xiso` packs the folder and your ROM into an ISO.

## What's in it

- Tested on a real Xbox (stock 64 MB), not just in an emulator
- 60 fps, with the game's timing fixed so it plays like the N64 (30 fps is an option)
- 480i, 480p and 720p, within what your dashboard allows
- 16:9 widescreen in flight that shows more of the world, with the gauges moved to the edges
- Rumble when you crash
- N64 or Sharp texture filtering
- Port settings (Back on the title screen, or Option > Port settings), with a hint under every row
- A one-press complete save in File 2 if you want everything unlocked
- D-pad for the camera views
- L + R + Back + Black quits to the dashboard, L + R + Back + White restarts

## Known issues

- Widescreen only applies in flight. Menus stay 4:3.
- Taking a screenshot (off by default) stalls the game for a moment, longer at 720p.
- 1080i isn't offered. It ran, but the frame rate cost wasn't worth it.
- Only the US release of the game is supported.

Updating later: copy the new `default.xbe` and `default.tbn` over the old ones. Saves and settings stay in `E:\UDATA\57580001\`.
