/* boot.c - from the verified ROM to the running game (game side of
 * xhw_main.c's wx_boot).
 *
 * xhw_main.c has checked the ROM (xhw_rom.c: size, byte order, header,
 * SHA-1) before calling here; nothing runs on an unvalidated ROM. Here:
 * settings.ini, the video mode (ends the splash), the renderer, the RDRAM
 * arena, then the decomp's bootproc, which creates the kernel thread and
 * returns. This thread then only watches the game's state machine for the
 * log ([GAME] lines: what an xemu scenario and a console log are read by). */
#include <ultra64.h>
#include <string.h>

#include "app/game.h"
#include <wings64_widescreen.h>
#include "wx_game.h"
#include "xgfx.h"
#include "xhw.h"

extern void bootproc(void* arg);
void wings64_rdram_init(void);

static const uint8_t* s_rom;
static uint32_t s_rom_size;

const uint8_t* wx_rom_ptr(uint32_t dev_addr, uint32_t nbytes) {
    dev_addr &= 0x0FFFFFFFu;   /* a PI cart address (0x10000000 + offset) or the offset itself */
    if (dev_addr >= s_rom_size || nbytes > s_rom_size - dev_addr) return NULL;
    return s_rom + dev_addr;
}

/* The game's top-level state (0 title, 5 flight, ...), -1 before it exists. */
int wx_game_state(void) { return D_80362690 != NULL ? (int)D_80362690->state : -1; }

static const char* const k_states[] = { "title", "1", "test details", "pilot select", "test setup", "test update",
                                        "results", "options", "demo pilot", "demo test setup", "file menu",
                                        "vehicle & class select", "test overview", "results cb", "congratulations",
                                        "credits" };

u32 wx_vi_retraces(void);
u32 wx_gfx_tasks(void);

void wx_boot(const uint8_t* rom, uint32_t size) {
    int last = -2;
    unsigned ms = 0;
    s_rom = rom;
    s_rom_size = size;
#if defined(XGFX_SELFTEST) && XGFX_SELFTEST
    {   /* the back end on its own (xbox/src/hw/xgfx_selftest.c) */
        void xgfx_selftest(void);
        xgfx_selftest();
    }
#endif
    wx_settings_load();
    xhw_video_set_pref(g_wx_settings.output, g_wx_settings.progressive);
    xhw_video_set_confirmed(g_wx_settings.video_confirmed);
    xhw_video_boot();
    if (xhw_video_reverted()) {   /* PLAN.md 3.3: the last boot's mode was never confirmed */
        g_wx_settings.output = XHW_OUT_480I;
        g_wx_settings.progressive = 0;
        wx_settings_save();
    }
    xgfx_init();
    xgfx_set_tex_filter(g_wx_settings.tex_filter ? 2 : 0);
    wx_settings_apply_fps();
    wx_settings_apply_widescreen();   /* after xgfx_init: a fallback to 480 changes what Auto means */
    xhw_pad_set_shots(g_wx_settings.screenshots);
    wings64_rdram_init();
    xhw_log("[BOOT] starting the game (bootproc)");
    bootproc(NULL);
    /* the boot thread is the N64's idle thread from here: the lowest level */
    xhw_thread_adopt_self(2);
    for (;;) {
        int st = wx_game_state();
        xhw_sleep_ms(250);
        ms += 250;
        if (st != last) {
            xhw_logf("[GAME] state %d (%s) at %u ms, retrace %u, frames %u", st,
                     st >= 0 && st < (int)(sizeof k_states / sizeof k_states[0]) ? k_states[st] : "?", ms,
                     (unsigned)wx_vi_retraces(), (unsigned)wx_gfx_tasks());
            last = st;
        }
    }
}

/* f3d_interp.c's end-of-frame hook: the port's own drawing outside the
 * game's display list. The FPS counter is the back end's (xgfx); the
 * settings panel draws through the game's own font inside its display list
 * (patches/021), so nothing is needed here yet. */
void wx_frame_overlay(void) {}

/* Display-list addresses are host pointers (os_convert.h is the identity on
 * the Xbox; f3d_interp.c resolves segments first). */
void* wx_addr(uint32_t a) { return (void*)(uintptr_t)a; }
