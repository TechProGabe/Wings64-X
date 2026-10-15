# 051-port-widescreen-borders

**Why:** 16:9 hor+ (PLAN.md 3.2, M7; patches/050). The game frames its world views with four black bars
around the SUBSCREEN (10..310 x 8..222 of 320x240). With hor+ the world shows past the 4:3 edges, so the left
and right bars would sit inside the picture as two black columns. The top and bottom bars span the screen's
width and are stretched across the 16:9 width by the interpreter (`overlay_full`), as Birdman64's renderer
does.

**What (`DIVERGES`):** the two side bars are not drawn while `wings64_widescreen_aspect > 0`:
- `app/code_D2B10.c` `drawScreenBorder` (flight, title, results, class summary, briefing);
- `app/level_select.c` `func_8030F448` (the class summary map's own copy of the four bars).

**Kill switch:** `-DXGFX_HORPLUS=0` (the aspect stays 0); Widescreen Off at run time.

**Origin:** Birdman64 (MIT, 1.2.0): `crates/pw64-game/patches/src/app/code_D2B10.c.patch` and
`level_select.c.patch`, the widescreen condition only (its fill-screen condition is not taken).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX`).
