# 036-port-settings-in-options

**Why:** maintainer, 2026-10-15: the port's settings panel ("Port settings") and "Exit to dashboard" are items
of the game's Option menu instead of the title's (patches/035 takes them off the title).

**What (`DIVERGES`):**
- `app/options.c` `optionsInitMain`: "Port settings" and "Exit to dashboard" above "Quit" (texts from
  `xbox/src/game/settings_menu.c`), the menu starting two rows lower (100 - 20 per item past three);
  `sOptionMenuItems` holds six.
- `optionsHandlerMain`: "Port settings" opens the panel (`wings64_settings_screen`, after the screen fade the
  Option screen's other items use) and comes back to the Option menu; "Exit to dashboard" calls
  `wings64_title_exit` (lastexit.txt, then the dashboard); "Quit" is mapped back to the game's own index.
- `app/menu.c`: `sMenuItems` holds eight (a completed game's Option menu has six items).

**Kill switch:** `-DWINGS64_SETTINGS_IN_OPTIONS=0` (or `WINGS64_SETTINGS_MENU=0`).

**Origin:** Wings64-X, own.

**N64 build:** unchanged (`#if defined(WINGS64_XBOX) && ...` / `#else` original).
