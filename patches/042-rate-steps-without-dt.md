# 042-rate-steps-without-dt

**Why (PLAN.md M5):** three physics steps are applied once a frame without `dt`, so at 60 fps they act
twice as often:

- `app/birdman2.c` `bird_802CF24C` and `app/hang_glider2.c` `hangGlider_802F2804`: the sideslip
  weathervane, `yawRate -= 0.005 * vx * |vx|` (the yaw rate is integrated with `dt` after): twice as strong.
- `app/birdman2.c` `bird_802D0080`: after landing, each contact frame nudges the bird `0.01` along the
  ground normal.
- `app/code_82B90.c` `func_802FD388`: the Jumble Hopper turns 5 degrees a frame (at full stick) while
  crouching or charging a hop: twice the turn rate.

**What (`DIVERGES`):** each step is multiplied by `wx_rate_scale()` (N64 frames per frame,
`wings64_rate.h`): the same change per second as on the N64.

**Not taken from Birdman64:** its sample-and-hold of the birdman's stall pitch torque (`bird_802CF24C`):
its own measurement has the stall landing as on the N64 from 30 to 90 Hz and crashing only at 100 Hz and
up; the port's cap is 60.

**Kill switch:** `-DWX_RATE=0` (scale 1).

**Origin:** Birdman64 (MIT), `birdman2.c.patch`, `hang_glider2.c.patch`, `code_82B90.c.patch`.

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).
