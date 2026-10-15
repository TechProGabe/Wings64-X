/* settings_menu.c - the port's Settings menu (PLAN.md 3.4): the title's
 * "Settings" and "Exit to dashboard" items, a panel drawn and driven like the
 * game's own Option screen, and the frame hook for wings64-dc's game-font
 * FPS counter (off by default: the row switches nv2a.c's). wings64-dc's
 * src/dc/settings.c (its 0c384bd), re-targeted at the Xbox's settings.ini.
 * DIVERGES: port settings menu (title items, settings panel, FPS counter;
 * none exist on the N64)
 *
 * A game unit (compile_game.py builds xbox/src/game with the decomp's
 * include path), so it calls the game's UI code directly:
 *  - backdrop: options_80316B80() + optionsDrawBorder(), the Option screen's
 *    own level, sprite 0 and black border;
 *  - rows: menuUtilCreate / menuUtilCheckInputs / menuUtilRender
 *    (menu_utils.c: the yellow triangle cursor, white items and yellow
 *    selection in font 6, the move chime, the cancel (B) sound, the stick's
 *    0.75 threshold). menu.c's wrapper holds 5 items and the panel has up to
 *    6, so the rows are drawn by our own gFcnDrawMenuItem, the way menu.c's
 *    menuDrawItem does (uvFontPrintStr16);
 *  - values change like the Sound panel's Stereo/Mono row (options.c
 *    optionsHandlerSound): stick left/right (and here also A and the D-pad)
 *    with sound 0x54; the dead zone repeats while a direction is held,
 *    counted in retraces;
 *  - text is the game's: labels built from font 6's own glyph table (STRG),
 *    "Return" is TEXT_QUIT from the text block, values in a column through
 *    the font's 0xFD x-position code;
 *  - leaving fades out with func_8032D51C(0) like the Option screen, and the
 *    title comes back without the intro (game.c's GAME_STATE_OPTIONS path).
 *
 * Pages (k_pages): "Port settings" is Video >, Controls >, System > and Return;
 * Video: Resolution (restart: `*`), Widescreen (Off / On / Auto, at once),
 * Texture filter, FPS counter, Frame rate (60 / 30, at once; PLAN.md M5),
 * Apply and restart; Controls: Stick dead zone
 * with the raw stick's tilt next to it, Rumble; System: Screenshots. Each
 * sub-menu ends in Return (B does the same), which goes back up one level.
 * Buttons come with remapping. settings.ini is
 * written on leaving when something changed (settings.c).
 *
 * Kill switches: WINGS64_SETTINGS_MENU=0 (wings64_settings.h) compiles this
 * out with the decomp hooks; WINGS64_TITLE_EXIT=0 drops the title's fourth
 * item. */
#include <ultra64.h>
#include <stdio.h>
#include "common.h"
#include <uv_font.h>
#include <uv_graphics.h>
#include <uv_string.h>
#include "app/demo.h"
#include "app/game.h"
#include "app/menu.h"
#include "app/menu_utils.h"
#include "app/replay_screen.h"
#include "app/snd.h"
#include "app/text_data.h"
#include <wings64_settings.h>
#include <wings64_widescreen.h>

#include "wx_game.h"
#include "xgfx.h"
#include "xhw.h"

#if WINGS64_SETTINGS_MENU

/* Game code with no header (src/app/options.c). */
void options_80316B80(void);
void optionsDrawBorder(void);

u32 wx_gfx_tasks(void);   /* os_shim.c: graphics tasks run */

#define MENU_FONT 6            /* title and Option menus */
#define SFX_OPTION_FLIP 0x54   /* options.c: Stereo/Mono, Sound Track changes */

static u8 sRequested;          /* the title chose "Settings" */
static s16 sTitleLabel[24];
static s16 sExitLabel[24];
static s16 sOptionsLabel[24];  /* "Port settings" in the Option menu (patches/036) */
static s32 sTitleTextRight;    /* where the title's longest port item ends (screen x) */
static s32 sTitleH;            /* the title menu font's height (font 6 at scale 1) */
static int sBootOutput = -1;   /* settings.ini's output when this boot started: `*` rows compare with it */

static void settings_boot_values(void) {
    if (sBootOutput < 0) sBootOutput = g_wx_settings.output;
}

/* ------------------------------------------------------------------ glyph strings */

/* Where the game's text glyphs start in a font's STRG (ASCII -> bitmap index
   table). Font 6 carries two alphabets: a thin one first, then the bold one
   the text tables use ("Option" = 0x78 0x93 ... in menu.c is 'O' and 'p' of
   the second set). uvFontPrintStr would pick the first, so look from the last
   "0123456789A" run instead. */
static s32 text_set_base(const char* str) {
    s32 i, base = 0;
    for (i = 0; str[i] != '\0'; i++) {
        if (str[i] == 'A' && i >= 10 && str[i - 10] == '0' && str[i - 1] == '9') base = i - 10;
    }
    return base;
}

/* ASCII -> 16-bit glyph string of a loaded font, as the game's text tables
   encode it (STRG position = bitmap index; unknown characters become -2,
   drawn as a blank cell like uvFontPrintStr does). */
static s32 glyphs(s16* dst, s32 at, s32 cap, s32 font, const char* s) {
    ParsedUVFT* f = gGfxUnkPtrs->fonts[font];
    s32 base = f != NULL ? text_set_base(f->str) : 0;
    for (; *s != '\0' && at < cap - 1; s++) {
        char* p = NULL;
        if (f != NULL) {
            p = uvStrchr(f->str + base, *s);
            if (p == NULL) p = uvStrchr(f->str, *s);
        }
        dst[at++] = p != NULL ? (s16)(p - f->str) : -2;
    }
    dst[at] = -1;
    return at;
}

/* 0xFD x y: uvFontPrintStr16 (US) starts a new run at screen x. Used for the value column. */
static s32 column(s16* dst, s32 at, s32 cap, s32 x) {
    if (at + 3 >= cap) return at;
    dst[at++] = 0xFD;
    dst[at++] = (s16)x;
    dst[at++] = 0;
    dst[at] = -1;
    return at;
}

/* test builds: the menu font's character table once, so a label's
   characters can be checked against it */
static void font_log(void) {
    static u8 sLogged;
    ParsedUVFT* f = gGfxUnkPtrs->fonts[MENU_FONT];
    if (!XHW_TEST_BUILD || sLogged || f == NULL) return;
    sLogged = 1;
    xhw_logf("[SETTINGS] font %d characters: %s", MENU_FONT, f->str);
}

/* ------------------------------------------------------------------ title screen */

int wings64_settings_title_init(void) {
    settings_boot_values();
    uvFontSet(MENU_FONT);
    uvFontScale(1.0f, 1.0f);
    font_log();
    glyphs(sTitleLabel, 0, ARRAY_COUNT(sTitleLabel), MENU_FONT, WINGS64_SETTINGS_IN_OPTIONS ? "Port settings" : "Settings");
    glyphs(sExitLabel, 0, ARRAY_COUNT(sExitLabel), MENU_FONT, "Exit to dashboard");
    /* title_screen.c: menuCreateItems(102, ...); menuUtilRender starts the text half an "A" + 3 further */
    sTitleTextRight = 102 + uvFontWidth("A") / 2 + 3 + uvFontStr16Width(WINGS64_TITLE_EXIT ? sExitLabel : sTitleLabel);
    sTitleH = uvFontHeight();
    return uvFontHeight() + 1;
}

/* patches/035: the title menu at the bottom. menuUtilRender puts its lowest
 * item's text at y + font height + 1 (y up from the screen's bottom) and the
 * game's box runs from 4 below the lowest row's top (its 76 for y 60) to 4
 * below the top row's (its 118): the same margins, measured from the
 * bottom edge the box is given. */
int wings64_title_menu_y(void) {
    return WINGS64_TITLE_MENU_BOTTOM - sTitleH + 4;
}

int wings64_title_box_bottom(void) {
    return WINGS64_TITLE_MENU_BOTTOM;
}

int wings64_title_box_top(int items) {
    return wings64_title_menu_y() + items * (sTitleH + 1) + sTitleH - 4;
}

/* patches/036: the Option menu's "Port settings" item */
short* wings64_settings_options_label(void) {
    if (!sOptionsLabel[0]) glyphs(sOptionsLabel, 0, ARRAY_COUNT(sOptionsLabel), MENU_FONT, "Port settings");
    return sOptionsLabel;
}

short* wings64_settings_title_label(void) {
    return sTitleLabel;
}

short* wings64_title_exit_label(void) {
    return sExitLabel;
}

/* the box keeps the game's left edge and 5 px of margin (the game's: 97 to
   the menu's 102) on the right */
int wings64_title_box_right(int right) {
    return sTitleTextRight + 5 > right ? sTitleTextRight + 5 : right;
}

int wings64_settings_open(void) {
    sRequested = 1;
    return GAME_STATE_OPTIONS;
}

int wings64_settings_take_request(void) {
    int r = sRequested;
    sRequested = 0;
    return r;
}

int wings64_settings_back_pressed(void) {
    return wx_pad_back_pressed();
}

/* PLAN.md 3.4: chosen like the other items (menuCheckInputs has played the
   confirm sound), written to lastexit.txt, then the dashboard. */
void wings64_title_exit(void) {
    xhw_log("[MENU] Exit to dashboard");
    xhw_exit_reason(WINGS64_SETTINGS_IN_OPTIONS ? "exit to the dashboard from the Option menu"
                                                : "exit to the dashboard from the title menu");
    xhw_sleep_ms(300);   /* the confirm sound */
    wx_pad_quiesce();
    xhw_quit_to_dashboard();
}

/* ------------------------------------------------------------------ video output */

static const char* const k_out_names[] = { "Auto", "480i", "480p", "720p", "1080i" };

static int out_allowed(int out) {
    switch (out) {
    case XHW_OUT_480P:
        return xhw_video_480p_allowed();
    case XHW_OUT_720P:
        return xhw_video_720p_allowed();
    case XHW_OUT_1080I:
        return xhw_video_1080i_allowed();
    default:
        return 1;
    }
}

/* what xhw_video_boot makes of a setting: Auto is the best the dashboard
   allows, a mode it does not allow steps down (1080i, 720p, 480p, 480i) */
static int out_resolved(int out) {
    if (out == XHW_OUT_AUTO) {
        return xhw_video_1080i_allowed() ? XHW_OUT_1080I
             : xhw_video_720p_allowed()  ? XHW_OUT_720P
             : xhw_video_480p_allowed()  ? XHW_OUT_480P
                                         : XHW_OUT_480I;
    }
    while (out > XHW_OUT_480I && !out_allowed(out)) out--;
    return out;
}

/* "Auto, 720p", "480p", "720p > 480i" (font 6 draws ">" as a double chevron) */
static void out_text(char* buf, int out) {
    int r = out_resolved(out);
    if (out == XHW_OUT_AUTO) snprintf(buf, 32, "Auto, %s", k_out_names[r]);
    else if (r != out) snprintf(buf, 32, "%s > %s", k_out_names[out], k_out_names[r]);
    else snprintf(buf, 32, "%s", k_out_names[out]);
}

/* ------------------------------------------------------------------ the panel */

enum { ROW_VIDEO, ROW_FPS, ROW_SHOTS, ROW_DEADZONE, ROW_RESTART, ROW_RETURN, ROW_FILTER, ROW_RUMBLE, ROW_WIDE, ROW_RATE,
       ROW_PAGE_VIDEO, ROW_PAGE_CONTROLS, ROW_PAGE_SYSTEM };

/* The panel is a top level ("Port settings") and three sub-menus. A page is
   a table of row kinds: adding a row to a page is one line in its table
   (a new kind also needs its label, value and change cases below). Every
   page ends with ROW_RETURN: "back" in a sub-menu, "leave" at the top.
   ROW_SHOTS is dropped from the page outside test builds. */
enum { PAGE_TOP, PAGE_VIDEO, PAGE_CONTROLS, PAGE_SYSTEM, PAGE_COUNT };

static const s32 k_page_top[]      = { ROW_PAGE_VIDEO, ROW_PAGE_CONTROLS, ROW_PAGE_SYSTEM, ROW_RETURN };
/* "Apply and restart" sits under the rows it applies (the `*` ones);
   System holds Screenshots (every build: maintainer, console run 8) */
static const s32 k_page_video[]    = { ROW_VIDEO, ROW_WIDE, ROW_FILTER, ROW_FPS, ROW_RATE, ROW_RESTART, ROW_RETURN };
static const s32 k_page_controls[] = { ROW_DEADZONE, ROW_RUMBLE, ROW_RETURN };
static const s32 k_page_system[]   = { ROW_SHOTS, ROW_RETURN };

static const struct { const s32* rows; s32 count; } k_pages[PAGE_COUNT] = {
    { k_page_top,      ARRAY_COUNT(k_page_top) },
    { k_page_video,    ARRAY_COUNT(k_page_video) },
    { k_page_controls, ARRAY_COUNT(k_page_controls) },
    { k_page_system,   ARRAY_COUNT(k_page_system) },
};

static const char* const k_filter_names[] = { "N64", "Sharp" };   /* Smooth dropped (maintainer, console run 6) */

/* optionsInitMain: menuCreateItems(80, ...); the panel starts further left
   so the value column and the stick readout fit on the 320-wide screen */
#define PANEL_X 48
#define MAX_ROWS 12
#define ROW_CAP 44   /* FONT_MAX_MSG_LEN (US) */
#define REPEAT_FIRST 24   /* retraces held before the dead zone repeats */
#define REPEAT_EVERY 6

static s32 sPage;
static s32 sPanelRowKind[MAX_ROWS];
static s32 sPanelCount;
static s16 sRowText[MAX_ROWS][ROW_CAP];
static s16 sLegend[ROW_CAP];
/* the hint under the rows for the row under the cursor (font 6, one line) */
#define ROW_KINDS (ROW_PAGE_SYSTEM + 1)
static s16 sHint[ROW_KINDS][ROW_CAP];
static s16 sFilterNote[2][ROW_CAP];   /* the texture filter's hint follows its value */
static s16 sRestartIdle[ROW_CAP];     /* Apply and restart with nothing to apply */
static s16* sReturnText;
static s32 sStickHeld;
static u8 sDirty;
static s32 sPanelY;
static s32 sPanelTextX;
static s32 sValueX;
static s32 sReadoutX;
static s32 sHeldDir;
static u32 sHeldSince, sHeldLast;

static const char* row_label(s32 kind) {
    switch (kind) {
    case ROW_PAGE_VIDEO:
        return "Video >";   /* font 6 draws '>' as a double chevron: the sub-menu marker */
    case ROW_PAGE_CONTROLS:
        return "Controls >";
    case ROW_PAGE_SYSTEM:
        return "System >";
    case ROW_VIDEO:
        return "Resolution *";
    case ROW_WIDE:
        return "Widescreen";
    case ROW_FILTER:
        return "Texture filter";
    case ROW_RUMBLE:
        return "Rumble";
    case ROW_FPS:
        return "FPS counter";
    case ROW_RATE:
        return "Frame rate";
    case ROW_SHOTS:
        return "Screenshots";
    case ROW_DEADZONE:
        return "Stick dead zone";
    case ROW_RESTART:
        return "Apply and restart";
    default:
        return "";
    }
}

static int restart_pending(void) {
    return g_wx_settings.output != sBootOutput;
}

static void panel_set_row(s32 idx) {
    char v[32];
    s32 n, kind = sPanelRowKind[idx];
    s16* t = sRowText[idx];
    if (kind == ROW_RETURN) return;
    n = glyphs(t, 0, ROW_CAP, MENU_FONT, row_label(kind));
    switch (kind) {
    case ROW_VIDEO:
        out_text(v, g_wx_settings.output);
        break;
    case ROW_WIDE:   /* "Auto, 16:9" / "Auto, 4:3" for this boot's mode (PLAN.md 3.4) */
        if (g_wx_settings.widescreen == 2) snprintf(v, sizeof v, "Auto, %s", wx_settings_widescreen_on() ? "16:9" : "4:3");
        else snprintf(v, sizeof v, "%s", g_wx_settings.widescreen ? "On" : "Off");
        break;
    case ROW_FILTER:
        snprintf(v, sizeof v, "%s", k_filter_names[g_wx_settings.tex_filter ? 1 : 0]);
        break;
    case ROW_RUMBLE:
        snprintf(v, sizeof v, "%s", g_wx_settings.rumble ? "On" : "Off");
        break;
    case ROW_FPS:
        snprintf(v, sizeof v, "%s", g_wx_settings.fps ? "On" : "Off");
        break;
    case ROW_RATE:
        snprintf(v, sizeof v, "%d", g_wx_settings.fps_mode == 30 ? 30 : 60);
        break;
    case ROW_SHOTS:
        snprintf(v, sizeof v, "%s", g_wx_settings.screenshots ? "On" : "Off");
        break;
    case ROW_DEADZONE:
        snprintf(v, sizeof v, "%d", g_wx_settings.stick_deadzone);   /* percent: font 6 has no '%' */
        break;
    default:
        return;
    }
    n = column(t, n, ROW_CAP, sValueX);
    n = glyphs(t, n, ROW_CAP, MENU_FONT, v);
    if (kind == ROW_DEADZONE) {   /* the raw stick's tilt now, to pick the value for this pad */
        snprintf(v, sizeof v, "Stick %d", wx_pad_stick_raw_pct());
        n = column(t, n, ROW_CAP, sReadoutX);
        glyphs(t, n, ROW_CAP, MENU_FONT, v);
    }
}

/* menuUtilRender's item callback, as menu.c's menuDrawItem */
static void panel_draw_item(s16 x, s16 y, s16 idx) {
    s16* t = sPanelRowKind[idx] == ROW_RETURN ? sReturnText : sRowText[idx];
    if (t != NULL) uvFontPrintStr16(x, y, t, 100, 0xFFE);
}

/* (re)build the rows of a page and put the cursor on `cursor` */
static void panel_open_page(s32 page, s32 cursor) {
    s16 tmp[ROW_CAP];
    s32 i, w, wmax;

    sPage = page;
    sPanelCount = 0;
    for (i = 0; i < k_pages[page].count; i++) {
        s32 kind = k_pages[page].rows[i];
        sPanelRowKind[sPanelCount++] = kind;
    }
    sReturnText = textGetDataByIdx(TEXT_QUIT);

    /* optionsInitMain centres 3 rows at y 100; one row is 17 px. From 10
     * rows (test builds) the legend goes on the Return row and the rows move
     * down a row and a half, clear of the top edge (TV overscan) */
    sPanelY = 100 + (3 - sPanelCount) * 9;
    uvFontSet(MENU_FONT);
    uvFontScale(1.0f, 1.0f);
    menuUtilCreate(PANEL_X, sPanelY, MENU_FONT, 1.0f, 1.0f, NULL, sPanelCount);
    gFcnDrawMenuItem = panel_draw_item;
    sPanelTextX = PANEL_X + uvFontWidth("A") / 2 + 3;   /* where menuUtilRender starts item text */
    /* the value column clears the longest label of the rows that have values */
    wmax = 0;
    for (i = 0; i < sPanelCount; i++) {
        s32 kind = sPanelRowKind[i];
        if (kind == ROW_RESTART || kind == ROW_RETURN || kind == ROW_PAGE_VIDEO || kind == ROW_PAGE_CONTROLS ||
            kind == ROW_PAGE_SYSTEM) {
            continue;
        }
        glyphs(tmp, 0, ROW_CAP, MENU_FONT, row_label(kind));
        w = uvFontStr16Width(tmp);
        if (w > wmax) wmax = w;
    }
    sValueX = sPanelTextX + wmax + 10;
    glyphs(tmp, 0, ROW_CAP, MENU_FONT, "60");
    sReadoutX = sValueX + uvFontStr16Width(tmp) + 12;

    menuUtilSetColors(MENU_COLOR_ITEM, 0xFF, 0xFF, 0xFF);
    menuUtilSetColors(MENU_COLOR_SELECTED, 0xFF, 0xFF, 0x00);
    for (i = 0; i < sPanelCount; i++) panel_set_row(i);
    menuUtilSetSoundFlags(MENU_SOUND_CHANGE | MENU_SOUND_BACK);   /* as the Sound panel: A is handled here */
    if (cursor > 0 && cursor < sPanelCount) menuUtilSetCurItem(cursor);
    sStickHeld = 1;   /* a push that opened the page is not a value change */
    sHeldDir = 0;
    xhw_logf("[SETTINGS] page %d: %d rows, value column x %d", (int)page, (int)sPanelCount, (int)sValueX);
}

static void panel_init(void) {
    settings_boot_values();
    glyphs(sLegend, 0, ROW_CAP, MENU_FONT, "* After a restart");
    glyphs(sFilterNote[0], 0, ROW_CAP, MENU_FONT, "The game's own look");
    glyphs(sFilterNote[1], 0, ROW_CAP, MENU_FONT, "Crisp square pixels");
    glyphs(sRestartIdle, 0, ROW_CAP, MENU_FONT, "Nothing to apply yet");
    glyphs(sHint[ROW_PAGE_VIDEO], 0, ROW_CAP, MENU_FONT, "Picture and display");
    glyphs(sHint[ROW_PAGE_CONTROLS], 0, ROW_CAP, MENU_FONT, "Stick and rumble");
    glyphs(sHint[ROW_PAGE_SYSTEM], 0, ROW_CAP, MENU_FONT, "Screenshots");
    glyphs(sHint[ROW_VIDEO], 0, ROW_CAP, MENU_FONT, "HD needs an HD cable");
    glyphs(sHint[ROW_WIDE], 0, ROW_CAP, MENU_FONT, "Auto: 16:9 at 720p and 1080i");
    glyphs(sHint[ROW_FPS], 0, ROW_CAP, MENU_FONT, "Frame rate, top left");
    glyphs(sHint[ROW_RATE], 0, ROW_CAP, MENU_FONT, "30 caps it like the N64");
    glyphs(sHint[ROW_RESTART], 0, ROW_CAP, MENU_FONT, "Restarts to apply *");
    glyphs(sHint[ROW_DEADZONE], 0, ROW_CAP, MENU_FONT, "Ignores small stick drift");
    glyphs(sHint[ROW_RUMBLE], 0, ROW_CAP, MENU_FONT, "Shakes when you crash");
    glyphs(sHint[ROW_SHOTS], 0, ROW_CAP, MENU_FONT, "BACK saves a picture");
    panel_open_page(PAGE_TOP, 0);
    sDirty = 0;
}

/* dir: +1 right / A, -1 left */
static void panel_change(s32 idx, s32 dir) {
    s32 kind = sPanelRowKind[idx];
    switch (kind) {
    case ROW_VIDEO:
        g_wx_settings.output = (g_wx_settings.output + dir + 5) % 5;
        g_wx_settings.progressive = g_wx_settings.output != XHW_OUT_480I;   /* 480i is the one interlaced pick */
        break;
    case ROW_WIDE:   /* Off, On, Auto; at once (the panel's own backdrop shows it) */
        g_wx_settings.widescreen = (g_wx_settings.widescreen + dir + 3) % 3;
        wx_settings_apply_widescreen();
        break;
    case ROW_FILTER:
        g_wx_settings.tex_filter = !g_wx_settings.tex_filter;
        xgfx_set_tex_filter(g_wx_settings.tex_filter ? 2 : 0);   /* the back end's 0 N64, 2 sharp */
        break;
    case ROW_RUMBLE:
        g_wx_settings.rumble = !g_wx_settings.rumble;
        if (g_wx_settings.rumble) wx_pad_rumble(40000, 250);   /* a taste */
        break;
    case ROW_FPS:
        g_wx_settings.fps = !g_wx_settings.fps;
        wx_settings_apply_fps();
        break;
    case ROW_RATE:   /* applied at the next retrace (os_shim.c vi_thread) */
        g_wx_settings.fps_mode = g_wx_settings.fps_mode == 30 ? 60 : 30;
        xhw_logf("[SETTINGS] frame rate %d", g_wx_settings.fps_mode);
        break;
    case ROW_SHOTS:
        g_wx_settings.screenshots = !g_wx_settings.screenshots;
        xhw_pad_set_shots(g_wx_settings.screenshots);
        break;
    case ROW_DEADZONE: {
        s32 d = g_wx_settings.stick_deadzone + dir * 5;
        if (d < 0 || d > 60) return;   /* at an end: no sound */
        g_wx_settings.stick_deadzone = d;
        break;
    }
    default:
        return;
    }
    sDirty = 1;
    sndPlaySfx(SFX_OPTION_FLIP);
    panel_set_row(idx);
}

/* D-pad up/down: same move, wrap and chime as the stick in menuUtilCheckInputs. */
static void panel_dpad_move(s32 cur) {
    s32 next = cur;
    if (demoButtonPress(D_80362690->unk9C, U_JPAD)) next = cur - 1;
    if (demoButtonPress(D_80362690->unk9C, D_JPAD)) next = cur + 1;
    if (next == cur) return;
    if (next < 0) next = sPanelCount - 1;
    if (next >= sPanelCount) next = 0;
    menuUtilSetCurItem(next);
    sndPlaySfxVolPitchPan(SFX_UI_MOVE_CHIME, 1.0f, 1.03f, 0.0f);
    func_8033FB14();
}

static void panel_save(void) {
    if (!sDirty) return;
    xhw_logf("[SETTINGS] output %s, widescreen %d, texture filter %s, rumble %d, fps %d, frame rate %d, "
             "screenshots %d, stick dead zone %d%%",
             k_out_names[g_wx_settings.output], g_wx_settings.widescreen, k_filter_names[g_wx_settings.tex_filter ? 1 : 0],
             g_wx_settings.rumble, g_wx_settings.fps, g_wx_settings.fps_mode, g_wx_settings.screenshots,
             g_wx_settings.stick_deadzone);
    wx_settings_save();
    sDirty = 0;
}

/* -1 = stay, 0 = leave. Mirrors optionsHandlerSound. */
static s32 panel_update(void) {
    s32 sel, cur, dir, held;
    f32 stickX;
    u32 now = wx_pad_retraces();

    sel = menuUtilCheckInputs();
    cur = menuUtilGetCurItem();
    if (sel == -1) {   /* B: back to the top level, or (there) saved, as Return */
        if (sPage != PAGE_TOP) {
            panel_open_page(PAGE_TOP, sPage - 1);
            return -1;
        }
        func_8032D51C(0);
        return 0;
    }
    if (sel >= 0) {
        switch (sPanelRowKind[sel]) {
        case ROW_PAGE_VIDEO:
        case ROW_PAGE_CONTROLS:
        case ROW_PAGE_SYSTEM:
            sndPlaySfx(SFX_UI_CONFIRM);
            panel_open_page(PAGE_VIDEO + (sPanelRowKind[sel] - ROW_PAGE_VIDEO), 0);
            break;
        case ROW_RETURN:
            sndPlaySfx(SFX_UI_CONFIRM);
            if (sPage != PAGE_TOP) {
                panel_open_page(PAGE_TOP, sPage - 1);
                break;
            }
            func_8032D51C(0);
            return 0;
        case ROW_RESTART:
            if (!restart_pending()) {   /* nothing waits for a restart: stay (the hint says so) */
                sndPlaySfx(SFX_OPTION_FLIP);
                return -1;
            }
            sndPlaySfx(SFX_UI_CONFIRM);
            sDirty = 1;
            panel_save();
            xhw_logf("[SETTINGS] save and restart: output %s", k_out_names[g_wx_settings.output]);
            xhw_exit_reason("restart from the settings menu");
            xhw_sleep_ms(300);   /* the confirm sound */
            wx_pad_quiesce();
            xhw_reboot_self();
            break;
        case ROW_DEADZONE:
            break;   /* left/right only */
        default:
            panel_change(sel, 1);
            break;
        }
        return -1;
    }
    /* left/right: the stick past 0.75 (once per push, as the Sound panel) or the D-pad */
    dir = 0;
    stickX = demoGetInputs(0, INPUT_AXIS_X);
    if (ABS_NOEQ(stickX) < 0.75f) {
        sStickHeld = FALSE;
    } else if (!sStickHeld) {
        dir = stickX > 0.0f ? 1 : -1;
        sStickHeld = TRUE;
    }
    if (demoButtonPress(D_80362690->unk9C, R_JPAD)) dir = 1;
    if (demoButtonPress(D_80362690->unk9C, L_JPAD)) dir = -1;
    if (dir != 0) {
        panel_change(cur, dir);
        sHeldDir = dir;
        sHeldSince = sHeldLast = now;
    } else if (sPanelRowKind[cur] == ROW_DEADZONE && sHeldDir != 0) {
        /* held: repeat by retrace count, so the rate does not follow the frame rate */
        held = sHeldDir > 0 ? (stickX > 0.75f || demoButtonCheck(D_80362690->unk9C, R_JPAD))
                            : (stickX < -0.75f || demoButtonCheck(D_80362690->unk9C, L_JPAD));
        if (!held) {
            sHeldDir = 0;
        } else if (now - sHeldSince >= REPEAT_FIRST && now - sHeldLast >= REPEAT_EVERY) {
            sHeldLast = now;
            panel_change(cur, sHeldDir);
        }
    } else {
        sHeldDir = 0;
    }
    panel_dpad_move(cur);
    return -1;
}

/* under the rows: the hint for the row under the cursor (what the texture
   filter picked does, ...), else on the Video page what the `*` means */
static void panel_draw_legend(void) {
    s32 cur = menuUtilGetCurItem(), kind = cur >= 0 && cur < sPanelCount ? sPanelRowKind[cur] : -1;
    s16* line = NULL;
    int warn = 0;
    uvFontSet(MENU_FONT);
    uvFontScale(1.0f, 1.0f);
    if (kind == ROW_FILTER) {
        line = sFilterNote[g_wx_settings.tex_filter ? 1 : 0];
    } else if (kind == ROW_RESTART) {
        line = restart_pending() ? sHint[ROW_RESTART] : sRestartIdle;
        warn = restart_pending();
    } else if (kind >= 0 && kind < ROW_KINDS && sHint[kind][0]) {
        line = sHint[kind];
    } else if (sPage == PAGE_VIDEO) {
        line = sLegend;
        warn = restart_pending();
    }
    if (!line) return;
    if (warn) uvFontColor(0xFF, 0xFF, 0x00, 0xFF);
    else uvFontColor(0xFF, 0xFF, 0xFF, 0xFF);
    uvFontPrintStr16(sPanelTextX, sPanelY - 2 * (uvFontHeight() + 1), line, 100, 0xFFE);
}

int wings64_settings_screen(void) {
    s32 i, ret;

    options_80316B80();   /* the Option screen's level, backdrop sprite and text block */
    panel_init();
    while ((ret = panel_update()) == -1) {
        for (i = 0; i < sPanelCount; i++)
            if (sPanelRowKind[i] == ROW_DEADZONE) panel_set_row(i);   /* the live stick readout */
        uvGfxBegin();
        optionsDrawBorder();
        menuUtilRender();
        panel_draw_legend();
        uvFontGenDlist();
        uvGfxEnd();
    }
    menuUtilDeinit();
    gFcnDrawMenuItem = NULL;
    panel_save();
    xhw_log("[SETTINGS] panel closed");
    return ret;
}

/* ------------------------------------------------------------------ FPS counter */

/* The counter the "FPS counter" row switches is the renderer's (nv2a.c,
   Melee-X's; settings.c wx_settings_apply_fps). This game-font one is
   wings64-dc's, drawn only with -DWX_FPS_GAME_FONT=1. */

static u32 sFps;
static u32 sFpsTasks0;
static u64 sFpsT0;

/* Presented frames per second over a 1 s window: graphics tasks (one per
   uvGfxEnd) counted against the ACPI timer (xhw_time_ns). Integer only. */
static void fps_sample(void) {
    u64 now = xhw_time_ns();
    u32 tasks = wx_gfx_tasks();
    u64 dt;

    if (sFpsT0 == 0) {
        sFpsT0 = now;
        sFpsTasks0 = tasks;
        return;
    }
    dt = now - sFpsT0;
    if (dt >= 1000000000ull) {
        sFps = (u32)(((u64)(tasks - sFpsTasks0) * 1000000000ull + dt / 2) / dt);
        sFpsT0 = now;
        sFpsTasks0 = tasks;
    }
}

/* Drawn like the flight HUD's TIME readout: a small yellow label and the HUD
   number font (font 8, as speed / altitude / timer). In flight it sits at the
   top between the timer and the compass; elsewhere in the top-right corner,
   clear of the centred screen titles. Levels without those fonts fall back
   to the menu font. */
#define FPS_X_FLIGHT 150
#define FPS_X_CORNER 262
#define FPS_TOP 16

void wings64_settings_frame(void) {
    char str[12];
    s32 x, numX;

    if (!wx_settings_fps_game_font()) {
        sFpsT0 = 0;   /* a fresh window when it comes back on */
        return;
    }
    fps_sample();
    if (sFps == 0) return;
    x = D_80362690 != NULL && D_80362690->state == GAME_STATE_TEST_UPDATE ? FPS_X_FLIGHT : FPS_X_CORNER;
    snprintf(str, sizeof str, "%d", (int)sFps);
    numX = x;
    /* y is the glyph bottom, counted up from the screen bottom (uvFontPrintStr) */
    if (uvFontOverlayStr_port(3, x, SCREEN_HEIGHT - FPS_TOP - 11, 0xFF, 0xFF, 0x00, "FPS")) numX += 26;
    if (!uvFontOverlayStr_port(8, numX, SCREEN_HEIGHT - FPS_TOP - 14, 0xFF, 0xFF, 0xFF, str)) {
        uvFontOverlayStr_port(MENU_FONT, numX, SCREEN_HEIGHT - FPS_TOP - 17, 0xFF, 0xFF, 0xFF, str);
    }
}

#endif /* WINGS64_SETTINGS_MENU */
