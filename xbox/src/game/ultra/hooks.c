/* hooks.c - the port-side functions the decomp patches call that belong to
 * later milestones: the Settings menu (patches/021, M7), the frame-buffer
 * persistence hooks (patches/026, 028; the renderer, M3-M4) and the audio task
 * (patches/022; the RSP audio HLE, M6). Each is replaced by the real one in
 * its milestone and removed from here.
 *
 * Where a do-nothing version is the game's own behaviour minus a port
 * feature (no frame-rate monitor, no frame kept, no audio), it does nothing;
 * where the game would need a value the port does not have yet, it stops
 * with pw64_fatal, as stubs.c does for libultra. */
#include <ultra64.h>
#include <wings64_settings.h>
#include <wings64_fbfreeze.h>

/* ---- patches/021: the Settings menu (M7: xbox/src/game/settings.c) ---- */
int wings64_settings_title_init(void) {
    pw64_fatal("TODO: wings64_settings_title_init (M7; or build with -DWINGS64_SETTINGS_MENU=0)", 0);
}

short* wings64_settings_title_label(void) {
    pw64_fatal("TODO: wings64_settings_title_label (M7)", 0);
}

int wings64_settings_open(void) {
    pw64_fatal("TODO: wings64_settings_open (M7)", 0);
}

/* No panel was asked for: the game's own Option screen runs. */
int wings64_settings_take_request(void) {
    return 0;
}

int wings64_settings_screen(void) {
    pw64_fatal("TODO: wings64_settings_screen (M7)", 0);
}

/* The frame-rate monitor is off until M7 draws it: the game's own frame. */
void wings64_settings_frame(void) {
}

/* ---- patches/026, 028: frame-buffer persistence (the renderer, M3-M4) ---- */
/* DIVERGES: until the renderer decides, fades and the album draw over
 * whatever the colour buffer holds. */
void wings64_fb_freeze(int on) {
    (void)on;
}

void wings64_fb_persist(int on) {
    (void)on;
}

/* ---- patches/022: the audio command list (M6: the RSP audio HLE, PLAN.md D6) ---- */
/* DIVERGES: until M6 the command list is not run: silence. */
void wings64_audio_task(OSTask* task) {
    (void)task;
}
