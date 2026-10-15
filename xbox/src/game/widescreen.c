/* widescreen.c - the game side of 16:9 hor+ (PLAN.md 3.2;
 * wings64_widescreen.h has the approach). From Birdman64's
 * native/src/pw64_widescreen.c (MIT), the hor+ and HUD anchor parts only
 * (no fill screen, no split screen), renamed wings64_*.
 * DIVERGES: hor+ widescreen (none on the N64)
 *
 * A game unit: it reads the camera channels (UnkStruct_80204D94) and
 * writes G_NOOP tags into the game's display list, which the interpreter
 * (f3d_interp.c) reads when the frame is drawn. */
#include <ultra64.h>
#include <uv_graphics.h>
#include <wings64_widescreen.h>

float wings64_widescreen_aspect;
int wings64_hud_anchor = 'C';

/* A channel that draws the 3D world: terrain (unk0 bit 1) or an environment
 * (unk2), over (almost) the full screen width: flight, the title, the class
 * summary, results. Pilot select's glider (no terrain, no environment) is
 * not one: it stays over its 4:3 background. */
static int world_view(const UnkStruct_80204D94* c) {
    return (c->unk0 & 2 || c->unk2 != 0xFFFF) && c->viewX0 <= 16 && c->viewX1 >= SCREEN_WIDTH - 16 &&
           c->viewX1 > c->viewX0;
}

/* The culling frustum of a world view, as wide as what is shown: the view
 * (300 px of the SUBSCREEN) shown 240 * aspect N64 px wide, +2 % for the
 * rounding of the output area (Birdman64's formula). */
float wings64_widescreen_xscale(const void* chan) {
    const UnkStruct_80204D94* c = chan;
    float k;
    if (wings64_widescreen_aspect <= 0.0f || !world_view(c)) return 1.0f;
    k = 240.0f * wings64_widescreen_aspect / (float)(c->viewX1 - c->viewX0) * 1.02f;
    return k > 1.0f ? k : 1.0f;
}

static void emit(u32 w1) {
    gGfxDisplayListHead->words.w0 = (u32)G_NOOP << 24;
    gGfxDisplayListHead->words.w1 = w1;
    gGfxDisplayListHead++;
}

void wings64_widescreen_tag(const void* chan, int on) {
    if (wings64_widescreen_xscale(chan) <= 1.0f) return;
    emit(on ? WINGS64_TAG_WORLD_ON : WINGS64_TAG_WORLD_OFF);
}

/* ---- the flight HUD at the 16:9 edges ----
 * The HUD can't be moved in the game's coordinates: its scissor is the 4:3
 * screen and a scissor left of x 0 can't be encoded. So the interpreter
 * moves whole 2D draws: an anchor tag 'L' / 'R' shifts the draws that
 * follow by the side margin, 'C' leaves them in the box. Text is queued
 * (uvFontPrintStr) and drawn at uvFontGenDlist, so font.c keeps the anchor
 * of each message and re-emits it (patches/052). */

void wings64_hud_emit_anchor(int anchor) {
    if (!XGFX_HUD_ANCHOR || wings64_widescreen_aspect <= 0.0f) return;
    emit(WINGS64_TAG_ANCHOR | (u32)(anchor & 0xFF));
}

void wings64_hud_set_anchor(int anchor) {
    wings64_hud_anchor = anchor;
    wings64_hud_emit_anchor(anchor);
}

/* the left gauge column (x 27) and the right one (radar 215, sea level 235,
 * altimeter 250) go to the edges; the rest stays centred */
void wings64_hud_anchor_xy(int x, int y) {
    (void)y;
    wings64_hud_set_anchor(x < 80 ? 'L' : x >= 200 ? 'R' : 'C');
}
