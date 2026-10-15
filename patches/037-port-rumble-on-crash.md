# 037-port-rumble-on-crash

**Why:** maintainer, 2026-10-15: the controller rumbles on a crash (Pilotwings 64 has no Rumble Pak support).

**What (`app/snd.c` `sndSetMusic`, `DIVERGES`):** when the music changes to one of the crash jingles
(`BGM_HANG_GLIDER_CRASH`, `BGM_ROCKET_BELT_CRASH`, `BGM_GYROCOPTER_CRASH`, `BGM_SKYDIVING_CRASH`,
`BGM_BIRDMAN_CRASH`: every vehicle's crash path sets one, `*_sound.c`), it calls `wings64_rumble_crash`
(`xbox/src/game/ultra/hooks.c`), which asks `pad.c` for ~2/3 s on both motors of player 1's controller; the
rumble is sent at the next controller read, under the controllers' lock. The Port settings panel's Rumble row
turns it off (`settings.ini` `[input] rumble`).

**Kill switch:** `-DWX_RUMBLE=0` (the hook does nothing).

**Origin:** Wings64-X, own.

**N64 build:** unchanged (`#ifdef WINGS64_XBOX`).
