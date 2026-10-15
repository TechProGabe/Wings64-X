# 036-port-settings-in-options

**Why:** maintainer, 2026-10-15: the port's settings panel is an item of the game's Option menu, "Port
settings", instead of a title item (patches/035 takes it off the title).

**What (`app/options.c`, `DIVERGES`):** `optionsInitMain` adds "Port settings" above "Quit" (text from
`wings64_settings_options_label`, `xbox/src/game/settings_menu.c`) and starts the menu a row lower
(the game's 80 / 100 become 60 / 80); `optionsHandlerMain` opens the panel for that item
(`wings64_settings_screen`, after the screen fade the Option screen's other items use) and comes back
to the Option menu, and maps "Quit" back to the game's own index.

**Kill switch:** `-DWINGS64_SETTINGS_IN_OPTIONS=0` (or `WINGS64_SETTINGS_MENU=0`).

**Origin:** Wings64-X, own.

**N64 build:** unchanged (`#if defined(WINGS64_XBOX) && ...` / `#else` original).
