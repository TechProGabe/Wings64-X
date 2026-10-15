/* hooks.c - the port-side functions the decomp patches call that belong to
 * later milestones: the frame-buffer persistence hooks (patches/026, 028;
 * the renderer, M3-M4) and the audio task (patches/022; the RSP audio HLE,
 * M6). Each is replaced by the real one in its milestone and removed from
 * here (the Settings menu's, patches/021, are in ../settings_menu.c).
 *
 * Where a do-nothing version is the game's own behaviour minus a port
 * feature (no frame kept, no audio), it does nothing;
 * where the game would need a value the port does not have yet, it stops
 * with pw64_fatal, as stubs.c does for libultra. */
#include <ultra64.h>
#include <wings64_fbfreeze.h>
#include <wings64_settings.h>

#include "../wx_game.h"
#include "../../gfx/f3d_interp.h"

/* ---- patches/026, 028: frame-buffer persistence ---- */
/* patches/026: the fades draw over the last frame (f3d_interp.c hold_frame;
 * kill switch -DXGFX_HOLD_LAST=0); patches/028: the album's persistent frame
 * (f3d_interp.c, -DXGFX_FB_PERSIST=0). */
void wings64_fb_freeze(int on) {
    f3d_hold_last(on);
}

void wings64_fb_persist(int on) {
    f3d_fb_persist(on);
}

/* ---- patches/037: rumble on a crash ---- */
#ifndef WX_RUMBLE
#define WX_RUMBLE 1
#endif
void wings64_rumble_crash(void) {
    if (WX_RUMBLE) wx_pad_rumble(52000, 650);   /* both motors, ~2/3 s: the jingle's opening hit */
}

/* ---- patches/022: the audio command list (M6: the RSP audio HLE, PLAN.md D6) ---- */
/* audio.c: the RSP audio HLE (M6); until then the list is counted and dropped */
void wings64_audio_task(OSTask* task) { wx_audio_task(task); }
