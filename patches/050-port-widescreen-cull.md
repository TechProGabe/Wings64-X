# 050-port-widescreen-cull

**Why:** 16:9 hor+ (PLAN.md 3.2, M7). The interpreter (`xbox/src/gfx/f3d_interp.c`) keeps the game's
projection and 4:3 mapping and draws a world view's perspective geometry past the 4:3 edges, out to the 16:9
width. Two things only the game can do: cull with a frustum as wide as what is shown (else objects and terrain
pop in and out at the 16:9 edges), and say which draws are the world view (else a 3D model over a 2D menu,
pilot select's glider, would poke out of its 4:3 background).

**What (`DIVERGES`):**
- `kernel/code_7150.c` `func_802061A0`: after patches/030's `WINGS64_CULL_WIDEN` (1.25, every channel), a
  world view's x extents are widened to `wings64_widescreen_xscale(chan)` when that is larger:
  `240 * aspect / (viewX1 - viewX0) * 1.02` (1.45 for the 300 px flight view at 16:9). The projection
  matrices (`unk10`, `unk90`) are built before and are unchanged.
- `kernel/chan.c` `uvChan_80204FE4`: with hor+ on, the planes are rebuilt for the current viewport (it may be
  set after the frustum), and the channel's draws are bracketed with `G_NOOP` tags (`WINGS64_TAG_WORLD_ON` /
  `_OFF`, `xbox/include/game/wings64_widescreen.h`) when it is a world view: terrain (`unk0 & 2`) or an
  environment (`unk2 != 0xFFFF`) over the full width. The interpreter extends only perspective draws inside
  the tags; RDP no-ops are harmless anywhere else.

`wings64_widescreen_aspect` (0 = off) is set by `xbox/src/game/settings.c` from the Widescreen setting at once
when it changes; `xbox/src/game/widescreen.c` has the helpers.

**Kill switch:** `-DXGFX_HORPLUS=0` (the aspect stays 0: no tags, no wider culling; 16:9 is then a plain
stretch). Widescreen Off in the Port settings panel does the same at run time.

**Origin:** Birdman64 (MIT, github.com/NoahVl/Birdman64, 1.2.0): `crates/pw64-game/patches/src/kernel/code_7150.c.patch`
and `chan.c.patch` (the widescreen parts; its fill-screen y scale, infinite view distance and split-screen
parts are not taken), `native/src/pw64_widescreen.c` (`pw64_widescreen_xscale`, `pw64_widescreen_tag`,
renamed `wings64_*`).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX`).
