# 030-port-cull-frustum-widen

**Why:** the camera's culling frustum (`func_802061A0`, kernel/code_7150.c: the far-plane corners `unk208`,
the side planes `unk298`, from which `func_80206318` derives the world planes `unk2E0`, the world corners
`unk250` and the ground view polygon `unk328`) is built from the same extents as the projection, for the
SUBSCREEN viewport (x 10..310, y 8..222 of 320x240; `camera.c`, `uv_graphics.h`). The N64 scissors the
picture to that viewport. The port draws the 3D view out to the screen edges (xbox/src/gfx/f3d_interp.c,
as wings64-dc), so the band outside the viewport shows geometry the game has already culled: objects and
terrain tiles pop in and out at the edges on fast turns (maintainer, xemu, 2026-10-15; wings64-dc run 4
saw the same).

**Fix:** under `WINGS64_XBOX`, the four extents are scaled by `WINGS64_CULL_WIDEN` (default 1.25) before
the corners and planes are built. Only culling uses these fields (dobj.c, fx.c, code_30EA0.c, terrain.c
via `func_80206F64` / `func_80207028` / `func_802070B8`); the projection matrices (`unk10`, `unk90`) are
built before and are unchanged. The bottom band needs 1.17 ((240 - 115) / 107); 1.25 leaves margin. The
cost is a few more objects and terrain tiles drawn per frame.

**Result:** on its own it did not stop the terrain pop-in the maintainer saw: that was the interpreter
rejecting triangles at the viewport edge (`XGFX_REJECT_RATIO`, xbox/src/gfx/f3d_interp.c), fixed
separately and confirmed in xemu. This patch stays for objects and terrain tiles the game culls in the
same band; its own effect has not been isolated.

**Kill switch:** `-DWINGS64_CULL_WIDEN=1.0f` (via `XBOX_CFLAGS`) is the N64's frustum.

**Later:** widescreen (M7) widens the visible area again; the factor must then follow the aspect.

**Origin:** Wings64-X, own.

**N64 build:** unchanged (`#ifdef WINGS64_XBOX`).
