# 028-album-persistent-frame

**Why:** photos are stored as camera parameters, and the album/viewer (`func_8033E3A8`) re-renders each one
as a small 3D view (80x59 thumbnails, a 200x150 single view), three frames per photo, then idles redrawing
only the "view photo" text. It relies on the N64 frame buffer never being cleared. The PVR starts every
frame from a clear, so thumbnails vanished one frame after they were drawn.

**What:**
- `app/snap.c` `func_8033E3A8`: `wings64_fb_persist(1)` on entry; before the return it waits for the last
  gfx task and calls `wings64_fb_persist(0)`.
- While on, wings64-dc's `src/dc/gfx/gfx_f3d.c` draws a persistent picture as the frame's first layer and copies into it,
  from the VRAM front buffer (`gpvr_persist_capture`), only the rectangles the last two frames drew
  (`gpvr_list_rects`); text strips are copied once per layout. The thumbnails' own scissor is honoured by the
  interpreter (`WINGS64_SCISSOR`), so each thumbnail stays inside its box.
- Hardware runs 7-11: whole-screen re-captures compounded KOS's 480i render scaler (blur, 1/1024 shrink per
  capture), and writing SCALER_CFG at run time hung the PVR; the rectangle policy above is what works (run 11).

**Kill switch:** `WINGS64_FB_PERSIST` (default 1, `xbox/include/game/wings64_fbfreeze.h`).
`XBOX_CFLAGS=-DWINGS64_FB_PERSIST=0` compiles the hook out.

**Origin:** wings64-dc, own (design from the 2026-10-12 Fable consult).

**N64 build:** unchanged (the hooks are `#if defined(WINGS64_XBOX) && WINGS64_FB_PERSIST`; the include is port-only).

**Import:** wings64-dc `0c384bd` `patches/028-album-persistent-frame.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). The comment reworded as in 026. No-op until the renderer. Kill switch `XBOX_CFLAGS=-DWINGS64_FB_PERSIST=0`.
