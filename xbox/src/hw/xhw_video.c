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
 * 64 MB (OpenCrossing-Xbox's measurement). Kill switch: -DXHW_VIDEO_1080I=0.
 *
 * Also the retrace and present counters (the watchdog and the log read
 * them) and the screenshot request the renderer takes at present. */
#include <hal/video.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

#include "xhw.h"
#include "xhw_internal.h"

#ifndef XHW_VIDEO_1080I
#define XHW_VIDEO_1080I 1
#endif

/* nxdk's video.c: the region in the encoder settings, and its 640x480i
 * mode for the HDTV pack (NTSC-M and NTSC-J use the same one) */
#define XHW_VIDEO_REGION_PAL 0x00000300
#define XHW_MODE_640x480I_HDTV 0x0801010du
void XVideoInit(DWORD dwMode, int width, int height, int bpp);

static xhw_video_mode s_mode = { 640, 480, 32, 0, 0 };
static int s_pref_out = XHW_OUT_AUTO;
static int s_pref_480p = 1;

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
    int out = s_pref_out;
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
        }
    }
#endif
    xhw_logf("[VIDEO] dashboard: encoder %08x, AV pack %u, widescreen %d, 480p %d, 720p %d, 1080i %d",
             (unsigned)XVideoGetEncoderSettings(), (unsigned)(XVideoGetEncoderSettings() & VIDEO_ADAPTER_MASK),
             xhw_video_widescreen_set(), xhw_video_480p_allowed(), xhw_video_720p_allowed(),
             xhw_video_1080i_allowed());
    if (out == XHW_OUT_AUTO) out = xhw_video_1080i_allowed() ? XHW_OUT_1080I : xhw_video_720p_allowed() ? XHW_OUT_720P
                                                                                                       : XHW_OUT_480P;
    if (!((out == XHW_OUT_1080I && xhw_video_1080i_allowed() && try_hd(1920, 1080, 40 * 1024)) ||
          (out >= XHW_OUT_720P && xhw_video_720p_allowed() && try_hd(1280, 720, 32 * 1024))))
        set_mode_480(out != XHW_OUT_480I && s_pref_480p);
    xhw_logf("[VIDEO] %dx%d %d-bit%s%s", s_mode.width, s_mode.height, s_mode.bpp,
             s_mode.progressive ? " progressive" : " interlaced", s_mode.widescreen ? " 16:9 dashboard" : " 4:3 dashboard");
}

/* The renderer calls this when an HD mode can't start (pb_init or its
 * contiguous allocations fail): a saved setting must never leave a black
 * screen. */
void xhw_video_fallback_480(void) {
    set_mode_480(0);
    xhw_logf("[VIDEO] fell back to 640x480 interlaced");
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

void xhw_video_pb_up(void) {
    VIDEO_MODE m = XVideoGetMode();
    s_pb_up = XHW_VBLANK_PACE && (m.refresh == 0 || m.refresh >= 59);
    xhw_logf("[VIDEO] retrace from %s (refresh %d)", s_pb_up ? "the vblank" : "the ACPI timer at 60.000 Hz", m.refresh);
}

void xhw_wait_vblank(void) {
    static uint64_t next_us;
    if (s_pb_up) {
        pb_wait_for_vbl();
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

/* ---- notices: logged; drawn by the renderer once it exists (M3) ---- */
static char s_notice[2][96];
static volatile LONG s_notice_seq;

void xhw_notice(const char* line1, const char* line2) {
    xhw_logf("[NOTICE] %s | %s", line1 ? line1 : "", line2 ? line2 : "");
    strncpy(s_notice[0], line1 ? line1 : "", sizeof s_notice[0] - 1);
    strncpy(s_notice[1], line2 ? line2 : "", sizeof s_notice[1] - 1);
    InterlockedIncrement(&s_notice_seq);
}
