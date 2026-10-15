/* wings64_settings.h - the in-game Settings menu (wings64-dc's settings.c,
 * ported in M7 as xbox/src/game/settings.c) as seen from the decomp hooks
 * (patches/021). Plain prototypes only: decomp units and port units both
 * include it. Until M7 the functions are stubs (xbox/src/game/ultra/hooks.c).
 *
 * WINGS64_SETTINGS_MENU (default 1) is the kill switch: 0 compiles out the
 * title's "Settings" item, the panel and the frame hook, leaving the game's
 * own title menu.
 *   XBOX_CFLAGS=-DWINGS64_SETTINGS_MENU=0 tools/xbox/msys/build.sh */
#ifndef WINGS64_SETTINGS_H
#define WINGS64_SETTINGS_H

#ifndef WINGS64_SETTINGS_MENU
#define WINGS64_SETTINGS_MENU 0   /* TODO(M7): 1 once xbox/src/game/settings_menu.c draws the panel */
#endif

/* Title screen (title_screen.c), with the title level loaded. Selects font 6
 * at scale 1 (as menuCreateItems does) and returns one menu row in pixels
 * (font height + 1, the step menuUtilRender uses), so the title can make room
 * for the extra item. */
int wings64_settings_title_init(void);
/* The title item's text, "Settings", as a 16-bit glyph string of the title
 * menu font. */
short* wings64_settings_title_label(void);
/* The title item was chosen: remember it and return the game state to go to
 * (GAME_STATE_OPTIONS, whose level load the panel shares with the game's
 * Option screen). */
int wings64_settings_open(void);
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
