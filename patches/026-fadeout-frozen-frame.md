# 026-fadeout-frozen-frame

**Why:** `func_8032D51C` (the game's `screen_fadeout`: black fade before menu and level changes, white fade
when the Birdman star is hit) calls `uvCopyFrameBuf(0)` and then draws only a translucent quad per frame,
relying on the N64 frame buffer to keep the last scene underneath. The PVR renders every frame from a clear
background, so on the port the scene vanished at once and the fade ran over black (the white box of the
Birdman star on black).

**What:**
- `app/replay_screen.c` `func_8032D51C`: after `uvCopyFrameBuf(0)` it calls `wings64_fb_freeze(1)`; at the
  end it waits for the fade's last gfx task and calls `wings64_fb_freeze(0)`. While the freeze is on,
  wings64-dc's `src/dc/gfx/gfx_f3d.c` replays the last finished frame's PVR polygon lists (copied once) under each frame.

**Kill switch:** `WINGS64_FB_FREEZE` (default 1, `xbox/include/game/wings64_fbfreeze.h`).
`XBOX_CFLAGS=-DWINGS64_FB_FREEZE=0` compiles the hook out.

**Origin:** wings64-dc, own.

**N64 build:** unchanged (the hooks are `#if defined(WINGS64_XBOX) && WINGS64_FB_FREEZE`; the include is port-only).

**Import:** wings64-dc `0c384bd` `patches/026-fadeout-frozen-frame.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). The comment reworded from the PVR to "a renderer that clears every frame". The hooks are no-ops until the renderer (M3-M4) decides whether it needs them (`xbox/src/game/ultra/hooks.c`, `xbox/include/game/wings64_fbfreeze.h`). Kill switch `XBOX_CFLAGS=-DWINGS64_FB_FREEZE=0`.
