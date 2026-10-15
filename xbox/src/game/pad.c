/* pad.c - Xbox controllers as N64 pads (PLAN.md D8).
 *
 * Player 1 is the controller in physical port 1 (xhw_pad.c); the game is
 * single-player, ports 2-4 are read and reported but carry no input it
 * uses. Default layout, by position and by Pilotwings' use of the buttons:
 *
 *   N64          Xbox
 *   stick        left stick: radial dead zone (settings.ini [input]
 *                stick_deadzone, 0-60 %, default 20), the rest rescaled so
 *                full tilt reaches +-80, the N64's usable rim (Melee-X pad.c)
 *   A / B        A / X
 *   C buttons    right stick past half way, and Y (up) B (down) White (left)
 *                Black (right)
 *   Z / R        left / right trigger
 *   Start, D-pad Start, D-pad
 *   L            none (remappable later, M7)
 *   BACK         on the title: the Settings panel (patches/034)
 *
 * L + R + BACK + BLACK on any port quits to the dashboard (Melee-X);
 * L + R + BACK + WHITE restarts the game (relaunches this XBE). Both fire as
 * soon as the four are down together (both triggers past 200 of 255, as
 * Melee-X's), are logged and go to lastexit.txt (xhw_exit_reason). They are
 * checked at every controller read the game makes and, so that they work in
 * every game state (a level load, a stalled game thread, a screen that reads
 * no controller), also from the VI thread whenever the game has not read the
 * controllers for COMBO_WATCH retraces (half a second). A busy flag keeps the
 * two threads out of SDL at the same time: the game's read waits (yielding)
 * for the VI thread's short look; the VI thread skips its look while the game
 * is reading, so it never blocks, not even on a game thread stuck in a read.
 * Kill switch -DWX_PAD_COMBO_WATCH=0: the combos only at the game's reads.
 * Test switch -DWX_PAD_COMBO_VI_ONLY=1: the combos only from the VI thread,
 * which then looks every 8 retraces (checks that path in xemu). */
#include <ultra64.h>
#include <math.h>
#include <string.h>

#include "wx_game.h"
#include "xhw.h"

#define TRIG_ON 128
#define CSTICK_ON 16384   /* half tilt */
#define COMBO_TRIG 200    /* both triggers past this for the combos (Melee-X) */

#ifndef WX_PAD_COMBO_WATCH
#define WX_PAD_COMBO_WATCH 1
#endif
#ifndef WX_PAD_COMBO_VI_ONLY
#define WX_PAD_COMBO_VI_ONLY 0
#endif
#define COMBO_WATCH (WX_PAD_COMBO_VI_ONLY ? 0 : 30)   /* retraces without a game read before the VI thread looks */

static int s_raw_pct;
static volatile uint32_t s_retraces;
static volatile uint32_t s_read_at;    /* s_retraces at the game's latest read */
static volatile long s_busy;           /* 1 while a thread is in SDL (xhw_pad_poll/get) */
static volatile int s_started;         /* the game has read the controllers once (SDL is up) */
static int s_back_now;                 /* BACK on port 1 (no trigger held) at the latest read */
static unsigned s_back_presses, s_back_seen;

/* L + R + BACK + BLACK / WHITE (PLAN.md D8): any port, any game state */
static void combos(int port, const xhw_pad* p) {
    if (!(p->buttons & XHW_BTN_BACK) || p->lt <= COMBO_TRIG || p->rt <= COMBO_TRIG) return;
    if (p->buttons & XHW_BTN_BLACK) {
        xhw_logf("[PAD] port %d: L + R + BACK + BLACK: to the dashboard", port + 1);
        xhw_exit_reason("quit by the controller (L+R+BACK+BLACK)");
        xhw_quit_to_dashboard();
    }
    if (p->buttons & XHW_BTN_WHITE) {
        xhw_logf("[PAD] port %d: L + R + BACK + WHITE: restart", port + 1);
        xhw_exit_reason("restart by the controller (L+R+BACK+WHITE)");
        xhw_reboot_self();
    }
}

/* VI thread, every retrace (os_shim.c) */
void wx_pad_retrace(void) {
    uint32_t r = ++s_retraces;
    if (WX_PAD_COMBO_WATCH && s_started && r - s_read_at >= COMBO_WATCH && (r & 7) == 0 &&
        __sync_bool_compare_and_swap(&s_busy, 0, 1)) {
        int i;
        if (s_retraces - s_read_at >= COMBO_WATCH) {   /* the game did not read meanwhile */
            xhw_pad_poll();
            for (i = 0; i < MAXCONTROLLERS; i++) {
                xhw_pad p;
                if (xhw_pad_get(i, &p)) combos(i, &p);
            }
        }
        __sync_lock_release(&s_busy);
    }
}

uint32_t wx_pad_retraces(void) { return s_retraces; }
int wx_pad_stick_raw_pct(void) { return s_raw_pct; }
/* a press not yet seen, with BACK still down (one left over from another
 * screen is dropped) */
int wx_pad_back_pressed(void) {
    int r = s_back_presses != s_back_seen && s_back_now;
    s_back_seen = s_back_presses;
    return r;
}

/* radial dead zone, then the remaining range rescaled onto 0..80. Rounded
 * by hand: nxdk's lrintf is a stub that asserts (it hung the console at the
 * first stick input in flight). */
static void stick(const xhw_pad* p, s8* sx, s8* sy, int port) {
    float x = p->lx / 32767.0f, y = p->ly / 32767.0f;
    float mag = sqrtf(x * x + y * y), dz = (float)g_wx_settings.stick_deadzone / 100.0f, k;
    if (port == 0) s_raw_pct = (int)(mag * 100.0f + 0.5f) > 100 ? 100 : (int)(mag * 100.0f + 0.5f);
    if (mag <= dz || mag <= 0.0f) {
        *sx = *sy = 0;
        return;
    }
    if (mag > 1.0f) {
        x /= mag;
        y /= mag;
        mag = 1.0f;
    }
    k = (mag - dz) / (1.0f - dz) / mag * 80.0f;
    x *= k;
    y *= k;
    *sx = (s8)(x > 80.0f ? 80 : x < -80.0f ? -80 : (int)(x + (x >= 0.0f ? 0.5f : -0.5f)));
    *sy = (s8)(y > 80.0f ? 80 : y < -80.0f ? -80 : (int)(y + (y >= 0.0f ? 0.5f : -0.5f)));
}

void wx_pad_read(void* out) {
    OSContPad* pads = (OSContPad*)out;
    int i, back;
    while (!__sync_bool_compare_and_swap(&s_busy, 0, 1)) xhw_yield();   /* the VI thread is looking */
    xhw_pad_poll();
    back = 0;
    for (i = 0; i < MAXCONTROLLERS; i++) {
        xhw_pad p;
        u16 b = 0;
        if (!xhw_pad_get(i, &p)) {
            memset(&pads[i], 0, sizeof pads[i]);
            pads[i].errno = CONT_NO_RESPONSE_ERROR;
            continue;
        }
        if (!WX_PAD_COMBO_VI_ONLY) combos(i, &p);
        if (i == 0 && (p.buttons & XHW_BTN_BACK) && p.lt < TRIG_ON && p.rt < TRIG_ON) back = 1;
        if (p.buttons & XHW_BTN_A) b |= A_BUTTON;
        if (p.buttons & XHW_BTN_X) b |= B_BUTTON;
        if (p.buttons & XHW_BTN_START) b |= START_BUTTON;
        if (p.buttons & XHW_BTN_UP) b |= U_JPAD;
        if (p.buttons & XHW_BTN_DOWN) b |= D_JPAD;
        if (p.buttons & XHW_BTN_LEFT) b |= L_JPAD;
        if (p.buttons & XHW_BTN_RIGHT) b |= R_JPAD;
        if ((p.buttons & XHW_BTN_Y) || p.ry > CSTICK_ON) b |= U_CBUTTONS;
        if ((p.buttons & XHW_BTN_B) || p.ry < -CSTICK_ON) b |= D_CBUTTONS;
        if ((p.buttons & XHW_BTN_WHITE) || p.rx < -CSTICK_ON) b |= L_CBUTTONS;
        if ((p.buttons & XHW_BTN_BLACK) || p.rx > CSTICK_ON) b |= R_CBUTTONS;
        if (p.lt >= TRIG_ON) b |= Z_TRIG;
        if (p.rt >= TRIG_ON) b |= R_TRIG;
        if (XHW_TEST_BUILD && i == 0) {   /* test builds: what the game is handed, when it changes */
            static u16 s_last;
            if (b != s_last) xhw_logf("[PAD] port 1 buttons %04x at retrace %u", b, xhw_frame_count());
            s_last = b;
        }
        pads[i].button = b;
        stick(&p, &pads[i].stick_x, &pads[i].stick_y, i);
        pads[i].errno = 0;
    }
    if (back && !s_back_now) s_back_presses++;
    s_back_now = back;
    s_read_at = s_retraces;
    s_started = 1;
    __sync_lock_release(&s_busy);
}
