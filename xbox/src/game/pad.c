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
 *
 * L + R + BACK + BLACK on any port quits to the dashboard (Melee-X);
 * L + R + BACK + WHITE restarts the game (relaunches this XBE). */
#include <ultra64.h>
#include <math.h>
#include <string.h>

#include "wx_game.h"
#include "xhw.h"

#define TRIG_ON 128
#define CSTICK_ON 16384   /* half tilt */

static int s_raw_pct;
static volatile uint32_t s_retraces;

void wx_pad_retrace(void) { s_retraces++; }
int wx_pad_stick_raw_pct(void) { return s_raw_pct; }

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
    int i;
    xhw_pad_poll();
    for (i = 0; i < MAXCONTROLLERS; i++) {
        xhw_pad p;
        u16 b = 0;
        if (!xhw_pad_get(i, &p)) {
            memset(&pads[i], 0, sizeof pads[i]);
            pads[i].errno = CONT_NO_RESPONSE_ERROR;
            continue;
        }
        if ((p.buttons & XHW_BTN_BACK) && p.lt > 200 && p.rt > 200) {
            if (p.buttons & XHW_BTN_BLACK) {
                xhw_log("[PAD] L + R + BACK + BLACK: to the dashboard");
                xhw_exit_reason("quit by the controller (L+R+BACK+BLACK)");
                xhw_quit_to_dashboard();
            }
            if (p.buttons & XHW_BTN_WHITE) {
                xhw_log("[PAD] L + R + BACK + WHITE: restart");
                xhw_exit_reason("restart by the controller (L+R+BACK+WHITE)");
                xhw_reboot_self();
            }
        }
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
}
