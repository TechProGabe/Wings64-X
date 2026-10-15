/* wings64_settings.h - the in-game Settings menu (wings64-dc's settings.c,
 * ported as xbox/src/game/settings_menu.c) as seen from the decomp hooks
 * (patches/021, 031, 034). Plain prototypes only: decomp units and port
 * units both include it.
 *
 * WINGS64_SETTINGS_MENU (default 1) is the kill switch: 0 compiles out the
 * title's "Settings" and "Exit to dashboard" items, BACK on the title, the
 * panel and the frame hook, leaving the game's own title menu (the FPS
 * counter is then the renderer's, nv2a.c).
 *   XBOX_CFLAGS=-DWINGS64_SETTINGS_MENU=0 tools/xbox/msys/build.sh
 * WINGS64_TITLE_EXIT (default 1; PLAN.md 3.4): the fourth title item "Exit
 * to dashboard" (patches/031). 0 removes it and leaves "Settings" as the
 * third. It rides on the menu's title hooks, so WINGS64_SETTINGS_MENU=0
 * removes it too. */
#ifndef WINGS64_SETTINGS_H
#define WINGS64_SETTINGS_H

#ifndef WINGS64_SETTINGS_MENU
#define WINGS64_SETTINGS_MENU 1
#endif
#ifndef WINGS64_TITLE_EXIT
#define WINGS64_TITLE_EXIT 1
#endif

/* Title screen (title_screen.c), with the title level loaded. Selects font 6
 * at scale 1 (as menuCreateItems does) and returns one menu row in pixels
 * (font height + 1, the step menuUtilRender uses), so the title can make room
 * for the extra items. */
int wings64_settings_title_init(void);
/* The title items' text, "Settings" and "Exit to dashboard", as 16-bit glyph
 * strings of the title menu font. */
short* wings64_settings_title_label(void);
short* wings64_title_exit_label(void);
/* patches/031: the right edge of the title's dark box, at least `right`
 * (the game's 202), wide enough for the longest port item. */
int wings64_title_box_right(int right);
/* The title item was chosen: remember it and return the game state to go to
 * (GAME_STATE_OPTIONS, whose level load the panel shares with the game's
 * Option screen). */
int wings64_settings_open(void);
/* "Exit to dashboard" was chosen: lastexit.txt, then the dashboard. Does not
 * return. */
void wings64_title_exit(void);
/* patches/034: 1 when BACK was pressed on player 1's controller in the
 * latest controller read (and no trigger is held: L+R+BACK+... is a combo). */
int wings64_settings_back_pressed(void);
/* game.c, GAME_STATE_OPTIONS: 1 once if the panel (not the game's Option
 * screen) was asked for. */
int wings64_settings_take_request(void);
/* The settings panel: runs until the player leaves it, like optionsTopRender(). */
int wings64_settings_screen(void);

/* uvGfxEnd (graphics.c), every frame before the display list is closed: the
 * frame-rate monitor. */
void wings64_settings_frame(void);

/* font.c helper (patches/021): print one string in the given font and emit
 * its display list now, leaving the game's current font, scale and colour as
 * they were. Returns 0 if the font is not loaded. */
int uvFontOverlayStr_port(int fontId, int x, int y, unsigned char r, unsigned char g, unsigned char b,
                          const char* str);

#endif
