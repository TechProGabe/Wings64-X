/* xhw_video.c - picks the output mode once, before pbkit starts (from
 * Melee-X's xhw_video.c, extended with 1080i: PLAN.md section 3.2).
 *
 * The dashboard allows, the settings choose:
 *   480i  640x480x32, always available (the safe mode)
 *   480p  640x480x32, HDTV/VGA pack, dashboard 480p flag, NTSC
 *   720p  1280x720 R5G6B5, HDTV/VGA pack, dashboard 720p flag, >= 32 MB free
 *   1080i 1920x1080 R5G6B5, HDTV/VGA pack, dashboard 1080i flag, >= 40 MB free
 * Auto takes the highest the dashboard allows. A mode that can't be set
 * falls to the next one down; xhw_video_fallback_480 is the renderer's way
 * back when pbkit can't start in the chosen one. 16-bit colour at 720p and
 * 1080i: three 32-bit framebuffers plus depth don't fit next to the game in
 * 64 MB (OpenCrossing-Xbox's measurement). 1080i is off by default
 * (XHW_VIDEO_1080I, xhw.h): it ran on the console (run 15) but cost too
 * much frame rate for its picture; 720p is the top mode.
 *
 * Also the retrace and present counters (the watchdog and the log read
 * them) and the screenshot request the renderer takes at present. */
#include <hal/video.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

#include "xhw.h"
#include "xhw_internal.h"


/* nxdk's video.c: the region in the encoder settings, and its 640x480i
 * mode for the HDTV pack (NTSC-M and NTSC-J use the same one) */
#define XHW_VIDEO_REGION_PAL 0x00000300
#define XHW_MODE_640x480I_HDTV 0x0801010du
void XVideoInit(DWORD dwMode, int width, int height, int bpp);

/* The confirm-or-revert net (PLAN.md 3.3, net 3): a 720p or 1080i mode that
 * settings.ini's video_confirmed does not name is on probation. Before the
 * mode is set, E:\UDATA\57580001\video_pending.txt is written; the renderer
 * draws "New video mode 720p. Press A to keep it. Back to 480i in N s." over
 * the frames (CPU text, xhw_video_overlay); A (pad.c) confirms: the file is
 * deleted and video_confirmed saved. No press in 15 s: the XBE restarts with
 * the file still there (pad.c's VI watch, xhw_video_probation_expired), and
 * a boot that finds the file runs 480i and saves 480i (settings.c via
 * xhw_video_reverted), which also covers a console reset or switched off
 * in front of a black TV.
 * DIVERGES from PLAN.md 3.3's first choice (pbkit re-initialised live at
 * 640x480): the restart is M7's documented fallback for it, taken from the
 * start because a second pb_init in one boot has never run on hardware
 * here; the screen is still never black for longer than the timeout.
 * 480i and 480p are never on probation: the dashboard itself runs at 480p
 * when its 480p flag is set, so a TV that shows the dashboard shows them.
 * Kill switch: -DXHW_VIDEO_CONFIRM=0. Test builds: a mode forced by
 * "env MX_VIDEO=" is not put on probation unless "env MX_VIDEO_PROBATION=1". */
#ifndef XHW_VIDEO_CONFIRM
#define XHW_VIDEO_CONFIRM 1
#endif
#define CONFIRM_SECS_DEFAULT 15
static unsigned s_confirm_secs = CONFIRM_SECS_DEFAULT;   /* test builds: "env MX_VIDEO_CONFIRM_SECS=" (a 720p FBDUMP takes ~10 s) */
#define CONFIRM_SECS s_confirm_secs
#define PENDING_FILE XHW_UDATA_DIR "video_pending.txt"

static xhw_video_mode s_mode = { 640, 480, 32, 0, 0 };
static int s_pref_out = XHW_OUT_AUTO;
static int s_pref_480p = 1;
static char s_confirmed[16];
static volatile int s_probation;   /* waiting for A */
static int s_reverted;             /* this boot is 480i: the last one never confirmed its mode */
static uint64_t s_prob_t0;         /* the first frame the prompt was drawn on (us) */

const char* xhw_video_mode_name(void) {
    return s_mode.height >= 1080 ? "1080i" : s_mode.height >= 720 ? "720p" : s_mode.progressive ? "480p" : "480i";
}

void xhw_video_set_confirmed(const char* mode) {
    strncpy(s_confirmed, mode ? mode : "", sizeof s_confirmed - 1);
    s_confirmed[sizeof s_confirmed - 1] = '\0';
}

int xhw_video_reverted(void) { return s_reverted; }
int xhw_video_probation(void) { return s_probation; }

/* counted from the first frame that showed the prompt; if no frame comes
 * (a renderer that never starts), from 10 s after the mode was set */
static uint64_t s_prob_set;
int xhw_video_probation_expired(void) {
    uint64_t now = xhw_time_us();
    if (!s_probation) return 0;
    if (s_prob_t0) return now - s_prob_t0 > CONFIRM_SECS * 1000000ull;
    return now - s_prob_set > (CONFIRM_SECS + 10) * 1000000ull;
}

static void pending_write(const char* name) {
    char text[64];
    int n = snprintf(text, sizeof text, "%s\r\n", name);
    if (!xhw_write_file_atomic(PENDING_FILE, text, (uint32_t)n)) xhw_log("[VIDEO] could not write video_pending.txt");
}

static void pending_clear(void) {
    DeleteFileA(PENDING_FILE);
}

void xhw_video_confirm(void) {
    if (!s_probation) return;
    s_probation = 0;
    pending_clear();
    xhw_video_set_confirmed(xhw_video_mode_name());
    xhw_logf("[VIDEO] %s confirmed (A)", xhw_video_mode_name());
}

const xhw_video_mode* xhw_video(void) { return &s_mode; }

static int hd_pack(void) {
    DWORD pack = XVideoGetEncoderSettings() & VIDEO_ADAPTER_MASK;
    return pack == AV_PACK_HDTV || pack == AV_PACK_VGA;
}

int xhw_video_720p_allowed(void) { return (XVideoGetEncoderSettings() & VIDEO_MODE_720P) && hd_pack(); }

int xhw_video_1080i_allowed(void) {
    return XHW_VIDEO_1080I && (XVideoGetEncoderSettings() & VIDEO_MODE_1080I) && hd_pack() &&
           (XVideoGetEncoderSettings() & VIDEO_STANDARD_MASK) != XHW_VIDEO_REGION_PAL;
}

int xhw_video_480p_allowed(void) {
    /* nxdk has no PAL progressive modes */
    return (XVideoGetEncoderSettings() & VIDEO_MODE_480P) && hd_pack() &&
           (XVideoGetEncoderSettings() & VIDEO_STANDARD_MASK) != XHW_VIDEO_REGION_PAL;
}

int xhw_video_widescreen_set(void) { return (XVideoGetEncoderSettings() & VIDEO_WIDESCREEN) != 0; }

void xhw_video_set_pref(int output, int progressive) {
    s_pref_out = output;
    s_pref_480p = progressive;
}

/* 640x480x32: 480p when the dashboard allows it and the settings don't say
 * otherwise. XVideoSetMode always picks 480p on an HDTV pack set to 480p,
 * so 480i there is set with nxdk's own XVideoInit, after XVideoSetMode has
 * recorded the size for pbkit (XVideoGetMode). */
static void set_mode_480(int progressive) {
    int p480 = xhw_video_480p_allowed() && progressive;
    int hdtv = (XVideoGetEncoderSettings() & VIDEO_ADAPTER_MASK) == AV_PACK_HDTV;
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    if (xhw_video_480p_allowed() && !p480 && hdtv) XVideoInit(XHW_MODE_640x480I_HDTV, 640, 480, 32);
    s_mode.width = 640;
    s_mode.height = 480;
    s_mode.bpp = 32;
    s_mode.progressive = xhw_video_480p_allowed() && (p480 || !hdtv);
    s_mode.widescreen = xhw_video_widescreen_set();
}

static int try_hd(int w, int h, unsigned need_kb) {
    if (xhw_mem_free_kb() < need_kb) {
        xhw_logf("[VIDEO] %dx%d skipped: %u KB free, %u KB needed", w, h, xhw_mem_free_kb(), need_kb);
        return 0;
    }
    if (!XVideoSetMode(w, h, 16, REFRESH_DEFAULT)) {
        xhw_logf("[VIDEO] XVideoSetMode(%d, %d, 16) failed", w, h);
        return 0;
    }
    s_mode.width = w;
    s_mode.height = h;
    s_mode.bpp = 16;
    s_mode.widescreen = 1;
    s_mode.progressive = h == 720;
    return 1;
}

void xhw_video_boot(void) {
    int out = s_pref_out, probation_ok = 1;
    void* f;
    xhw_splash_release();   /* XVideoSetMode frees the splash's framebuffer */
#if defined(XHW_AUTOPAD) && XHW_AUTOPAD
    {   /* test builds: "env MX_VIDEO=480i|480|720|1080" from the autopad
         * script, over settings.ini (docs/testing.md) */
        const char* e = getenv("MX_VIDEO");
        if (e) {
            out = !strcmp(e, "1080") ? XHW_OUT_1080I : !strcmp(e, "720") ? XHW_OUT_720P
                : !strcmp(e, "480i") ? XHW_OUT_480I : XHW_OUT_480P;
            s_pref_480p = out != XHW_OUT_480I;
            xhw_logf("[VIDEO] MX_VIDEO=%s", e);
            probation_ok = getenv("MX_VIDEO_PROBATION") != NULL;
        }
        e = getenv("MX_VIDEO_CONFIRM_SECS");
        if (e && atoi(e) > 0) s_confirm_secs = (unsigned)atoi(e);
    }
#endif
    /* a mode the last boot put on probation and nobody confirmed (no A in
     * 15 s, or the console was reset in front of a black TV): 480i */
    f = XHW_VIDEO_CONFIRM ? xhw_file_open(PENDING_FILE) : NULL;
    if (f) {
        char name[16] = { 0 };
        uint32_t n = xhw_file_size(f);
        if (n > sizeof name - 1) n = sizeof name - 1;
        xhw_file_read(f, 0, name, n);
        xhw_file_close(f);
        name[strcspn(name, "\r\n")] = '\0';
        xhw_logf("[VIDEO] %s was not confirmed (video_pending.txt from the last boot): 480i, saved", name);
        pending_clear();
        out = XHW_OUT_480I;
        s_pref_480p = 0;
        s_reverted = 1;
    }
    xhw_logf("[VIDEO] dashboard: encoder %08x, AV pack %u, widescreen %d, 480p %d, 720p %d, 1080i %d",
             (unsigned)XVideoGetEncoderSettings(), (unsigned)(XVideoGetEncoderSettings() & VIDEO_ADAPTER_MASK),
             xhw_video_widescreen_set(), xhw_video_480p_allowed(), xhw_video_720p_allowed(),
             xhw_video_1080i_allowed());
    if (out == XHW_OUT_AUTO) out = xhw_video_1080i_allowed() ? XHW_OUT_1080I : xhw_video_720p_allowed() ? XHW_OUT_720P
                                                                                                       : XHW_OUT_480P;
    {   /* an HD mode not confirmed yet: the pending file goes down before the
         * TV sees the new mode (PLAN.md 3.3) */
        const char* want = out == XHW_OUT_1080I && xhw_video_1080i_allowed() ? "1080i"
                         : out >= XHW_OUT_720P && xhw_video_720p_allowed()  ? "720p"
                                                                             : NULL;
        if (XHW_VIDEO_CONFIRM && probation_ok && want && strcmp(want, s_confirmed)) pending_write(want);
    }
    if (!((out == XHW_OUT_1080I && xhw_video_1080i_allowed() && try_hd(1920, 1080, 40 * 1024)) ||
          (out >= XHW_OUT_720P && xhw_video_720p_allowed() && try_hd(1280, 720, 32 * 1024))))
        set_mode_480(out != XHW_OUT_480I && s_pref_480p);
    xhw_logf("[VIDEO] %dx%d %d-bit%s%s", s_mode.width, s_mode.height, s_mode.bpp,
             s_mode.progressive ? " progressive" : " interlaced", s_mode.widescreen ? " 16:9 dashboard" : " 4:3 dashboard");
    if (XHW_VIDEO_CONFIRM && probation_ok && s_mode.height >= 720 && strcmp(xhw_video_mode_name(), s_confirmed)) {
        pending_write(xhw_video_mode_name());   /* 1080i may have stepped down to 720p */
        s_prob_set = xhw_time_us();
        s_probation = 1;
        xhw_logf("[VIDEO] %s on probation (confirmed: %s): A keeps it, else 480i in %u s", xhw_video_mode_name(),
                 s_confirmed[0] ? s_confirmed : "none", CONFIRM_SECS);
    } else if (XHW_VIDEO_CONFIRM && !s_reverted) {
        pending_clear();   /* written for an HD mode that could not be set */
    }
}

/* The renderer calls this when an HD mode can't start (pb_init or its
 * contiguous allocations fail): a saved setting must never leave a black
 * screen. */
void xhw_video_fallback_480(void) {
    set_mode_480(0);
    if (s_probation) {   /* 480 is never on probation */
        s_probation = 0;
        pending_clear();
    }
    xhw_logf("[VIDEO] fell back to 640x480 interlaced");
}

/* ---- text over the finished frame: the probation prompt, notices ----
 * nv2a.c's present asks once a frame and draws the lines with the CPU
 * (xhw_overlay.c). Returns the number of lines (0: nothing to draw); the
 * first call during probation starts its countdown. */
static char s_notice[2][96];
static uint64_t s_notice_t0;
#define NOTICE_US 10000000ull

int xhw_video_overlay(char lines[2][96], uint32_t* rgb, int* top) {
    uint64_t now = xhw_time_us();
    if (s_probation) {
        uint64_t gone;
        unsigned left;
        if (!s_prob_t0) s_prob_t0 = now;
        gone = (now - s_prob_t0) / 1000000ull;
        left = gone >= CONFIRM_SECS ? 0u : (unsigned)(CONFIRM_SECS - gone);
        snprintf(lines[0], 96, "New video mode %s. Press A to keep it.", xhw_video_mode_name());
        snprintf(lines[1], 96, "Back to 480i in %u s.", left);
        *rgb = 0xFFFF40;
        *top = 0;
        return 2;
    }
    if (s_notice_t0 && now - s_notice_t0 < NOTICE_US) {
        memcpy(lines, s_notice, sizeof s_notice);
        *rgb = 0xFFFFFF;
        *top = 1;
        return 2;
    }
    return 0;
}

/* ---- retrace and present counters ---- */
static volatile unsigned s_retrace, s_presents;

/* The retrace (the N64's VI interrupt, the game's frame clock). Once
 * pbkit is up and the output runs at 60 Hz, pbkit's vblank: flips and
 * retraces then line up. Otherwise (the boot before the renderer, a 50 Hz
 * mode) 60.000 Hz from the ACPI timer, as Melee-X's vi.c paces: a 50 Hz
 * console must not slow the game. XVideoWaitForVBlank is never used: it
 * hooks the GPU interrupt and pb_init then fails (OpenCrossing). Kill
 * switch -DXHW_VBLANK_PACE=0: always the ACPI pacer. */
#ifndef XHW_VBLANK_PACE
#define XHW_VBLANK_PACE 1
#endif
static volatile int s_pb_up;
DWORD pb_wait_for_vbl(void);
void nv2a_vbl_probe(void);   /* nv2a.c: display-order probe (test builds) */

void xhw_video_pb_up(void) {
    VIDEO_MODE m = XVideoGetMode();
    s_pb_up = XHW_VBLANK_PACE && (m.refresh == 0 || m.refresh >= 59);
    xhw_logf("[VIDEO] retrace from %s (refresh %d)", s_pb_up ? "the vblank" : "the ACPI timer at 60.000 Hz", m.refresh);
}

void xhw_wait_vblank(void) {
    static uint64_t next_us;
    if (s_pb_up) {
        pb_wait_for_vbl();
        nv2a_vbl_probe();
    } else {
        uint64_t now = xhw_time_us();
        if (!next_us || now > next_us + 50000) next_us = now;   /* a hitch: don't catch up */
        next_us += 16667;
        while (now + 1500 < next_us) {
            Sleep(1);
            now = xhw_time_us();
        }
        while (now < next_us) now = xhw_time_us();
    }
    s_retrace++;
}
unsigned xhw_frame_count(void) { return s_retrace; }
void xhw_note_present(void) { s_presents++; }
unsigned xhw_present_count(void) { return s_presents; }

/* ---- screenshots ---- */
static volatile LONG s_shot;

void xhw_shot_request(int to_serial) { InterlockedExchange(&s_shot, to_serial ? 2 : 1); }
int xhw_shot_take(void) { return (int)InterlockedExchange(&s_shot, 0); }

int xhw_running_in_xemu(void) {
    unsigned a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    return !(d & (1u << 1));   /* xemu's CPU model has no VME (Melee-X xhw_pmc.c) */
}

/* ---- notices: logged, and drawn at the top for 10 s (xhw_video_overlay) ---- */
void xhw_notice(const char* line1, const char* line2) {
    xhw_logf("[NOTICE] %s | %s", line1 ? line1 : "", line2 ? line2 : "");
    strncpy(s_notice[0], line1 ? line1 : "", sizeof s_notice[0] - 1);
    strncpy(s_notice[1], line2 ? line2 : "", sizeof s_notice[1] - 1);
    s_notice_t0 = xhw_time_us();
}
