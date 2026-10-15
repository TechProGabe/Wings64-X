# 035-port-title-layout

**Why:** maintainer, 2026-10-15: the title's menu box grew up over the pilots' faces once "Settings" and
"Exit to dashboard" were added (patches/021, 031). Both move into the Option menu (patches/036); the title
keeps the game's two items, in a box at the bottom of the screen, where the copyright line was.

**What (`app/title_screen.c`, all `DIVERGES`):**
- With `WINGS64_SETTINGS_IN_OPTIONS` (default 1): the game's two items, "Start game" and "Option", resting on
  `WINGS64_TITLE_MENU_BOTTOM` (40 px up from the screen's bottom: where it would sit above the copyright line, which leaves room below it) with
  the game's margins around the text (`xbox/src/game/settings_menu.c` `wings64_title_menu_y`,
  `wings64_title_box_top`). BACK on the title still opens the port settings panel (patches/034).
- With `WINGS64_TITLE_COPYRIGHT` 0 (default): the copyright line is not drawn (its dark band, 30..285 x
  18..34, and sprite 4).

**Kill switches:** `-DWINGS64_SETTINGS_IN_OPTIONS=0` (the title as patches/031 left it: four items),
`-DWINGS64_TITLE_COPYRIGHT=1` (the copyright line back; the box then rests at 40).

**Origin:** Wings64-X, own.

**N64 build:** unchanged (`WINGS64_XBOX` only).
