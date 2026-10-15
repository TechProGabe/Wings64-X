# 034-port-title-back-settings

**Why:** PLAN.md D8 and 3.4: BACK on the title opens the port's Settings panel, as choosing
"Settings" does (the Xbox's BACK has no N64 button behind it; decision 5 in PLAN.md 7: no
platform-drawn emergency menu, the in-game panel instead).

**What:** `app/title_screen.c`, in the title menu's input step (only once the menu is shown): when
`menuCheckInputs` returned no selection and `wings64_settings_back_pressed()` says BACK was just
pressed on player 1's controller, play the confirm sound and treat it as item 2 ("Settings").
`wings64_settings_back_pressed` (`xbox/src/game/settings_menu.c`) reads `pad.c`'s
`wx_pad_back_pressed`: a press not yet seen with BACK still down and neither trigger held
(L + R + BACK + BLACK / WHITE are the dashboard and restart combos), so a press left over from another
screen does not open the panel later.

In test builds (and with `screenshots = 1`) BACK also takes a screenshot, as before.

**Kill switch:** `WINGS64_SETTINGS_MENU=0` (the whole menu).

**Origin:** Wings64-X own.

**N64 build:** unchanged (`#if defined(WINGS64_XBOX) && WINGS64_SETTINGS_MENU`, an addition with no
original).
