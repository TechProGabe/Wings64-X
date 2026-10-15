# 021-port-settings-menu

**Why:** the port has settings the N64 never had (on the Dreamcast an FPS monitor and the SD card log; on the Xbox
the video, widescreen, frame-rate and dead-zone rows of PLAN.md section 3.4). The maintainer wants
them on the title screen in a menu that looks and behaves like the game's own. This patch adds the
hooks. The menu itself is port code (wings64-dc `src/dc/settings.c`; Wings64-X M7), which calls the game's own UI routines.

**What:**
- `app/title_screen.c`: the title menu gets a third item, "Settings", below "Start game" and "Option".
  Its text is set with `menuSetItem`, as a glyph string built from font 6. The menu's anchor moves down
  one row and its dark box grows down one row, so "Start game" and "Option" stay where they were.
  Choosing the item calls `wings64_settings_open()`, which returns `GAME_STATE_OPTIONS`.
  "Start game" is still the default item, so A on the title still starts the game.
- `app/game.c`: `gameUpdateStateOptions` keeps its level load. If the title asked for the settings
  panel (`wings64_settings_take_request()`), it runs `wings64_settings_screen()` instead of
  `optionsTopRender()`. It then returns to the title without the intro, as after Option.
- `kernel/graphics.c`: `uvGfxEnd` calls `wings64_settings_frame()` just before the display list is
  closed. That function samples the frame rate and draws the FPS monitor when it is on.
- `kernel/font.c`: adds `uvFontOverlayStr_port`. It prints one string in a game font and emits its sprite
  display list right away, then restores the game's font id, width, scale, colour and pending-message
  count. It cannot go through `uvFontGenDlist`, which restarts its display-list cursor at
  base + messages-so-far. A second call in one frame would then overwrite the commands of text already
  emitted that frame. So the helper carries on from where the last batch ended (`rsp_dl_next`).

**Kill switch:** `WINGS64_SETTINGS_MENU` (default 1, `xbox/include/game/wings64_settings.h`).
`XBOX_CFLAGS=-DWINGS64_SETTINGS_MENU=0` compiles all four hooks out (compile_game.py passes XBOX_CFLAGS
to every game unit).

**Origin:** wings64-dc, own.

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original; the font.c helper is port-only).

**Import:** wings64-dc `0c384bd` `patches/021-port-settings-menu.patch`, guard `WINGS64_DC` renamed `WINGS64_XBOX` (one `sed`). The font helper `uvFontOverlayStr_dc` renamed `uvFontOverlayStr_port`. The SD-card-log row lived in wings64-dc's `settings.c`, not in this patch: nothing to drop here, and the M7 port of `settings.c` leaves it out. Until M7 the port side is `xbox/src/game/ultra/hooks.c`: `wings64_settings_frame` does nothing, `wings64_settings_take_request` returns 0, the rest stop with `pw64_fatal("TODO: ...")`. Kill switch `-DWINGS64_SETTINGS_MENU=0` through `XBOX_CFLAGS` (compile_game.py passes it to every game unit). Header: `xbox/include/game/wings64_settings.h`.
