# 031-port-title-exit

**Why:** the maintainer asked for a way to leave the game from its own menus (PLAN.md 3.4 and the
"Leaving the game" decision in 7, 2026-10-15): a fourth title item, "Exit to dashboard", drawn and
chosen like the others. The N64 had no dashboard to go back to.

**What:** `app/title_screen.c`, on top of patch 021's three-item title menu:
- `sMainMenuItems` gets a fourth entry; its text is set with `menuSetItem(3, wings64_title_exit_label())`,
  a glyph string built from font 6's bold set by the same builder as "Settings".
- `sTitleMenuRow` becomes two rows: the menu's anchor and its dark box grow down by two rows, so
  "Start game" and "Option" stay where they were.
- The dark box's right edge comes from `wings64_title_box_right(202)`: the game's 202, or wider when
  "Exit to dashboard" needs it (5 px past the text, the game's own left margin).
- Choosing the item (A or START; `menuCheckInputs` plays the confirm sound) calls
  `wings64_title_exit()` (`xbox/src/game/settings_menu.c`): it logs `[TITLE] Exit to dashboard`, sets
  the exit reason that `xhw_quit_to_dashboard` writes to `lastexit.txt` ("exit to the dashboard from
  the title menu"), waits 300 ms for the sound and goes to the dashboard. No confirm prompt (PLAN.md
  3.4 asks for none; the item is three rows below the default "Start game").

**Kill switch:** `WINGS64_TITLE_EXIT` (default 1, `xbox/include/game/wings64_settings.h`).
`XBOX_CFLAGS=-DWINGS64_TITLE_EXIT=0` gives patch 021's three-item menu. The item uses 021's title
hooks, so `WINGS64_SETTINGS_MENU=0` removes it too (the game's own two-item menu).

**Origin:** Wings64-X own.

**N64 build:** unchanged (every hunk is inside 021's `#if defined(WINGS64_XBOX) && WINGS64_SETTINGS_MENU`,
with `#if WINGS64_TITLE_EXIT ... #else <021's line> #endif`).
