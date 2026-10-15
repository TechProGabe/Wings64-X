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

#include "../wx_game.h"
#include "../../gfx/f3d_interp.h"

/* ---- patches/026, 028: frame-buffer persistence ---- */
/* patches/026: the fades draw over the last frame (f3d_interp.c hold_frame;
 * kill switch -DXGFX_HOLD_LAST=0). DIVERGES (028, until M8): the album draws
 * over whatever the colour buffer holds. */
void wings64_fb_freeze(int on) {
    f3d_hold_last(on);
}

void wings64_fb_persist(int on) {
    (void)on;
}

/* ---- patches/022: the audio command list (M6: the RSP audio HLE, PLAN.md D6) ---- */
/* audio.c: the RSP audio HLE (M6); until then the list is counted and dropped */
void wings64_audio_task(OSTask* task) { wx_audio_task(task); }
