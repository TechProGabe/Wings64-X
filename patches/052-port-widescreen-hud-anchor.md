# 052-port-widescreen-hud-anchor

**Why:** PLAN.md 3.2 and maintainer decision 2026-10-15: with 16:9 hor+ the flight HUD is anchored to the
16:9 edges, not left in the centred 4:3 box. The HUD can't be moved in the game's coordinates (its scissor is
the 4:3 screen and a scissor left of x 0 can't be encoded), so the game tags the HUD's draws with an anchor
and the interpreter (`xbox/src/gfx/f3d_interp.c`) moves whole 2D draws by the side margin.

**What (`DIVERGES`):**
- `app/hud.c`: each gauge helper (`hudDrawThrottle`, `hudDrawRadar`, `hudDrawSpeed`, `hudDrawFuel`,
  `hudDrawAltimeter`, `hudSeaLevel`, `hudDrawTimer`) starts with `wings64_hud_anchor_xy(x, y)`: the left
  column (x < 80) to the left edge, the right one (x >= 200) to the right edge, the rest centred. The
  cannonball's power gauge (left), heading bar (centre) and elevation gauge (right), the demo's controller
  picture (left), the reticle, photo count and messages (centre) are set by hand. The camera shutter's two
  bars are anchored to their edges and their width is scaled by `aspect * 3/4`, so they still meet in the
  middle of the 16:9 picture. `hudMainRender` ends with the anchor back at centre, so menus, briefing and
  results are untouched.
- `kernel/font.c`: text is queued (`uvFontPrintStr*`) and drawn at `uvFontGenDlist`, after the HUD has moved
  on, so each message keeps the anchor of its print (`sWx64MsgAnchor`) and `uvFontGenDlist` re-emits it.

The tags are `G_NOOP` w1 = `WINGS64_TAG_ANCHOR | 'L' / 'C' / 'R'` (`xbox/include/game/wings64_widescreen.h`),
emitted only while hor+ is on (`xbox/src/game/widescreen.c`).

**Kill switch:** `-DXGFX_HUD_ANCHOR=0`: no tags, the HUD stays in the centred 4:3 box (the shutter keeps its
N64 widths). `-DXGFX_HORPLUS=0` turns hor+ off altogether.

**Origin:** Birdman64 (MIT, 1.2.0): `crates/pw64-game/patches/src/app/hud.c.patch` and `kernel/font.c.patch`,
the horizontal anchors only (its vertical anchors for fill screen, split-screen, frame-rate and free-flight
hunks are not taken); `native/src/pw64_widescreen.c` (`pw64_hud_*`, renamed `wings64_hud_*`).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX`).
