/* settings.c - settings.ini (PLAN.md 3.4, Melee-X's file rules).
 *
 * Only E:\UDATA\57580001\settings.ini is read. It is written with the
 * defaults on first boot, and when a value changes, through
 * xhw_write_file_atomic (settings.tmp, flushed, read back, renamed over
 * settings.ini): a failed write leaves the old file. Comments and unknown
 * keys are dropped on a rewrite.
 *
 *   [video]  output = auto|480i|480p|720p|1080i   progressive = 0|1
 *            widescreen = auto|on|off   video_confirmed = <mode>   fps = 0|1
 *            fps_mode = 60|30 (the frame rate; PLAN.md M5)
 *   [system] screenshots = 0|1
 *   [input]  stick_deadzone = 0..60 (percent, steps of 5)
 *
 * BACK held at boot (Melee-X's xsdk_early; the splash says so): this boot
 * runs 480i 4:3 and the file is saved with output = 480i, progressive = 0,
 * widescreen = off. Test builds take "env MX_VIDEO=" over the file
 * (xhw_video.c). The in-game panel (settings_menu.c) edits the same struct
 * and saves it here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wings64_settings.h"
#include "wx_game.h"
#include "xgfx.h"
#include "xhw.h"

/* The FPS counter ([video] fps, the panel's "FPS counter" row) is the
 * renderer's (nv2a.c fps_overlay, Melee-X's as is: presented frames over
 * half a second of the ACPI clock, yellow 5x7 digits at 2x on a black box at
 * 1/16 of the framebuffer from the top-left corner; maintainer's choice
 * 2026-10-15). -DWX_FPS_GAME_FONT=1 draws wings64-dc's instead (the game's
 * HUD font, through patches/021's frame hook; PLAN.md 3.4's first idea). */
#ifndef WX_FPS_GAME_FONT
#define WX_FPS_GAME_FONT 0
#endif

#ifndef XHW_FPS_DEFAULT
#define XHW_FPS_DEFAULT XHW_TEST_BUILD
#endif

/* [video] fps_mode = 60|30 (the panel's "Frame rate" row, PLAN.md M5): the
 * game draws up to 60 frames a second, or at most 30 as on the N64.
 * Applied at once (os_shim.c vi_thread reads it every retrace). The rate
 * patches (038-045, wings64_rate.h) keep the game's per-frame behaviour the
 * N64's at either. -DWX_FPS_MODE_DEFAULT=30 changes the default. */
#ifndef WX_FPS_MODE_DEFAULT
#define WX_FPS_MODE_DEFAULT 60
#endif

WxSettings g_wx_settings;

static void defaults(WxSettings* s) {
    memset(s, 0, sizeof *s);
    s->output = XHW_OUT_AUTO;
    s->progressive = 1;
    s->widescreen = 2;
    s->fps = XHW_FPS_DEFAULT;
    s->screenshots = XHW_TEST_BUILD;
    s->stick_deadzone = 20;
    s->tex_filter = 0;
    s->aa = 0;
    s->rumble = 1;
    s->fps_mode = WX_FPS_MODE_DEFAULT == 30 ? 30 : 60;
}

static const char* const k_out[] = { "auto", "480i", "480p", "720p", "1080i" };
static const char* const k_ws[] = { "off", "on", "auto" };
static const char* const k_filter[] = { "n64", "smooth", "sharp" };
static const char* const k_aa[] = { "off", "2x", "4x" };

static char* trim(char* s) {
    char* e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
    return s;
}

static int pick(const char* v, const char* const* names, int n, int dflt) {
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(v, names[i])) return i;
    return dflt;
}

static void parse(WxSettings* s, char* text) {
    char* line = text;
    while (line && *line) {
        char* nl = strchr(line, '\n');
        char *k, *v, *eq, *c;
        if (nl) *nl = '\0';
        c = strchr(line, ';');
        if (c) *c = '\0';
        eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            k = trim(line);
            v = trim(eq + 1);
            if (!strcmp(k, "output")) s->output = pick(v, k_out, 5, XHW_OUT_AUTO);
            else if (!strcmp(k, "progressive")) s->progressive = atoi(v) != 0;
            else if (!strcmp(k, "widescreen")) s->widescreen = pick(v, k_ws, 3, 2);
            else if (!strcmp(k, "video_confirmed")) snprintf(s->video_confirmed, sizeof s->video_confirmed, "%s", v);
            else if (!strcmp(k, "fps")) s->fps = atoi(v) != 0;
            else if (!strcmp(k, "fps_mode")) s->fps_mode = atoi(v) == 30 ? 30 : 60;
            else if (!strcmp(k, "screenshots")) s->screenshots = atoi(v) != 0;
            else if (!strcmp(k, "texture_filter")) s->tex_filter = pick(v, k_filter, 3, 0);
            else if (!strcmp(k, "antialiasing")) s->aa = pick(v, k_aa, 3, 0);
            else if (!strcmp(k, "rumble")) s->rumble = atoi(v) != 0;
            else if (!strcmp(k, "stick_deadzone")) {
                int d = atoi(v);
                s->stick_deadzone = d < 0 ? 0 : d > 60 ? 60 : d / 5 * 5;
            }
        }
        line = nl ? nl + 1 : NULL;
    }
}

int wx_settings_save(void) {
    char buf[1024], p[96];
    const WxSettings* s = &g_wx_settings;
    int n = snprintf(buf, sizeof buf,
                     "; Wings64-X settings (docs/platform.md)\r\n"
                     "[video]\r\noutput = %s\r\nprogressive = %d\r\nwidescreen = %s\r\nvideo_confirmed = %s\r\nfps = %d\r\n"
                     "fps_mode = %d\r\ntexture_filter = %s\r\nantialiasing = %s\r\n"
                     "[system]\r\nscreenshots = %d\r\n"
                     "[input]\r\nstick_deadzone = %d\r\nrumble = %d\r\n"
                     "; end of settings\r\n",
                     k_out[s->output], s->progressive, k_ws[s->widescreen], s->video_confirmed, s->fps,
                     s->fps_mode, k_filter[s->tex_filter], k_aa[s->aa], s->screenshots, s->stick_deadzone, s->rumble);
    snprintf(p, sizeof p, "%ssettings.ini", xhw_save_dir());
    if (!xhw_write_file_atomic(p, buf, (uint32_t)n)) {
        xhw_logf("[SETTINGS] could not write %s", p);
        return 0;
    }
    xhw_log("[SETTINGS] saved");
    return 1;
}

static int back_held(void) {
    int i;
    xhw_pad_poll();
    for (i = 0; i < 4; i++) {
        xhw_pad pd;
        if (xhw_pad_get(i, &pd) && (pd.buttons & XHW_BTN_BACK)) return 1;
    }
    return 0;
}

void wx_settings_load(void) {
    static char text[2048];
    char p[96];
    void* f;
    int write = 0;
    defaults(&g_wx_settings);
    snprintf(p, sizeof p, "%ssettings.ini", xhw_save_dir());
    f = xhw_file_open(p);
    if (f) {
        uint32_t n = xhw_file_size(f);
        if (n > sizeof text - 1) n = sizeof text - 1;
        if (xhw_file_read(f, 0, text, n)) {
            text[n] = '\0';
            write = strstr(text, "; end of settings") == NULL;   /* an older or cut file: rewrite it whole */
            parse(&g_wx_settings, text);   /* after the check: parse cuts the text up in place */
        } else {
            write = 1;
        }
        xhw_file_close(f);
    } else {
        write = 1;
        xhw_log("[SETTINGS] no settings.ini: defaults written");
    }
#if defined(XHW_AUTOPAD) && XHW_AUTOPAD
    {   /* test builds: "env MX_FPS_MODE=30|60" from the autopad script, over
         * settings.ini for this boot (not saved): the 30/60 comparisons */
        const char* e = getenv("MX_FPS_MODE");
        if (e) {
            g_wx_settings.fps_mode = atoi(e) == 30 ? 30 : 60;
            xhw_logf("[SETTINGS] MX_FPS_MODE=%s", e);
        }
    }
#endif
    if (back_held()) {
        xhw_log("[SETTINGS] BACK held at boot: 480i 4:3 (saved)");
        g_wx_settings.output = XHW_OUT_480I;
        g_wx_settings.progressive = 0;
        g_wx_settings.widescreen = 0;
        write = 1;
    }
    xhw_logf("[SETTINGS] output %s, progressive %d, widescreen %s, fps %d, frame rate %d, screenshots %d, stick dead "
             "zone %d%%",
             k_out[g_wx_settings.output], g_wx_settings.progressive, k_ws[g_wx_settings.widescreen], g_wx_settings.fps,
             g_wx_settings.fps_mode, g_wx_settings.screenshots, g_wx_settings.stick_deadzone);
    if (write) wx_settings_save();
}

void wx_settings_apply_fps(void) {
    xgfx_set_fps_overlay(g_wx_settings.fps && !(WINGS64_SETTINGS_MENU && WX_FPS_GAME_FONT));
}

/* settings_menu.c draws its counter only when this says so */
int wx_settings_fps_game_font(void) { return g_wx_settings.fps && WINGS64_SETTINGS_MENU && WX_FPS_GAME_FONT; }
