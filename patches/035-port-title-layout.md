# 035-port-title-layout

**Why:** maintainer, 2026-10-15: with "Settings" and "Exit to dashboard" added (patches/021, 031) the
title's menu box grew up over the pilots' faces. The port settings move into the Option menu
(patches/036), and the menu goes to the bottom of the screen, where the copyright line was.

**What (`app/title_screen.c`, all `DIVERGES`):**
- With `WINGS64_SETTINGS_IN_OPTIONS` (default 1): three items, "Start game", "Option", "Exit to
  dashboard", at the game's font size; the selection index 2 is mapped to the exit case (3), so BACK
  (patches/034, which reports 2) still opens the port settings panel.
- The menu and its box rest on `WINGS64_TITLE_MENU_BOTTOM` (18 px up from the screen's bottom, the
  game's own bottom overscan margin), keeping the game's margins around the text
  (`xbox/src/game/settings_menu.c` `wings64_title_menu_y`, `wings64_title_box_top`).
- With `WINGS64_TITLE_COPYRIGHT` 0 (default): the copyright line is not drawn (its dark band, 30..285 x
  18..34, and sprite 4).

**Kill switches:** `-DWINGS64_SETTINGS_IN_OPTIONS=0` (the title as patches/031 left it: four items,
"Settings" on the title), `-DWINGS64_TITLE_COPYRIGHT=1` (the copyright line back; the box then rests
at 40).

**Origin:** Wings64-X, own.

**N64 build:** unchanged (`WINGS64_XBOX` only).
