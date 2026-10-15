/* wings64_widescreen.h - 16:9 hor+ (PLAN.md 3.2) as the decomp hooks
 * (patches/050-052), the settings (settings.c) and the Fast3D interpreter
 * (f3d_interp.c) see it. Plain prototypes and constants: decomp units and
 * port units both include it.
 *
 * Hor+ as Birdman64 does it (its native/src/pw64_widescreen.c and
 * docs/notes/renderer.md "Widescreen", MIT): the game's projection is left
 * as it is and the interpreter stops cropping the world view at the 4:3
 * edges (same scale, scissor opened to the 16:9 width), which is the picture
 * a projection widened by 3/4 on its x column gives, without touching the
 * matrices the game also uses for photo scoring. What the renderer can't do
 * is the game's CPU culling: patches/050 widens it for world views and tags
 * them in the display list, so only their perspective draws extend (a model
 * over a 2D menu stays in its 4:3 box). The rest of the 2D stays in the
 * centred 4:3 box, undistorted; patches/052 moves the flight HUD's gauges to
 * the 16:9 edges with anchor tags.
 *
 * Kill switches (XBOX_CFLAGS):
 *   -DXGFX_HORPLUS=0      16:9 is a plain stretch of the 4:3 picture (no hor+,
 *                         no box); the game's culling and borders stay the N64's
 *   -DXGFX_HUD_ANCHOR=0   the flight HUD stays in the centred 4:3 box */
#ifndef WINGS64_WIDESCREEN_H
#define WINGS64_WIDESCREEN_H

#ifndef XGFX_HORPLUS
#define XGFX_HORPLUS 1
#endif
#ifndef XGFX_HUD_ANCHOR
#define XGFX_HUD_ANCHOR 1
#endif

/* G_NOOP w1 tags (Birdman64's values): the world view's draws, and the
 * horizontal anchor of the 2D draws that follow ('L', 'C', 'R'). */
#define WINGS64_TAG_WORLD_ON 0x50575731u    /* "PWW1" */
#define WINGS64_TAG_WORLD_OFF 0x50575730u   /* "PWW0" */
#define WINGS64_TAG_ANCHOR 0x50574100u      /* "PWA" + 'L' / 'C' / 'R' */

/* The aspect the picture is shown at when hor+ is on (16:9), 0 when the
 * picture is 4:3 (Widescreen Off, or the kill switch). Set by settings.c at
 * boot and at once when the panel's row changes; the game reads it while it
 * builds a frame. */
extern float wings64_widescreen_aspect;

/* patches/050 (kernel/code_7150.c, kernel/chan.c): how much wider than its
 * own the culling frustum of channel `chan` (an UnkStruct_80204D94*) must be:
 * > 1 for a world view while hor+ is on, else 1. */
float wings64_widescreen_xscale(const void* chan);
/* patches/050: bracket a world view's draws (on = 1 before, 0 after) with
 * the WORLD tags; nothing when hor+ is off or `chan` is not a world view. */
void wings64_widescreen_tag(const void* chan, int on);

/* patches/052 (app/hud.c, kernel/font.c): the flight HUD's anchors. */
extern int wings64_hud_anchor;                   /* the anchor of text printed now */
void wings64_hud_emit_anchor(int anchor);        /* the tag only (font.c re-emits a message's) */
void wings64_hud_set_anchor(int anchor);         /* remember it and emit the tag */
void wings64_hud_anchor_xy(int x, int y);        /* a gauge at N64 x: x < 80 'L', x >= 200 'R', else 'C' */

/* settings.c: the Widescreen setting (off / on / auto) applied: aspect,
 * interpreter, log line. At boot after xgfx_init and on every change. */
void wx_settings_apply_widescreen(void);
/* f3d_interp.c: 1 = hor+ (16:9 picture), 0 = 4:3; read per frame. */
void f3d_set_widescreen(int on);

#endif
