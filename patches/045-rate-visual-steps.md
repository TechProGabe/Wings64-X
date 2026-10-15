# 045-rate-visual-steps

**Why (PLAN.md M5):** steps and decays applied once a frame to things the player sees but that do not
fly the vehicle; at 60 fps they run twice as fast:

- `app/code_82B90.c`: the Jumble Hopper's leg angles (`+= stick * 25` a frame) and leg swing amplitudes
  (`*= 0.95 / 0.985 / 0.99 / 0.995` a frame);
- `app/camera.c`: the Jumble Hopper camera's elevation limit (3 degrees a frame, `camera_802D3790`) and the
  fly-by camera's rise while occluded (2 a frame, `camera_802D559C`);
- `app/ski_lift.c` (the chairs, `+= 0.03` a frame), `app/shuttle.c` (the launch: altitude `+= thrust` a
  frame, the shake `+= / -= 5` a frame), `app/splash.c` (ripple fade and growth),
  `app/falco.c` (the hawk's height filter, `0.8 x + 0.2 target` a frame), `app/env_sound.c` (the
  emitters' fade, `vol *= 0.95` every game frame), `app/code_AE460.c` (the rocket belt's jet flame spin),
  `app/hud.c` (the radar waypoint blink, alpha +-50 a frame).

**What (`DIVERGES`):** steps are multiplied by `wx_rate_scale()`, factors become `wx_rate_decay(k)` =
`k ** scale` (the same change per second as one N64 frame's per N64 frame); the hawk's affine filters are
rewritten as decays toward their fixed points (`target + (x - target) * 0.8 ** scale`; 5.526 / 0.2 for
the second); the blink step is rounded to an integer.

**Not taken from Birdman64** (unchanged at the port's 60 fps cap, or cosmetic): the replay screen's fade
(`+= 1/60` a frame of a menu loop: right at 60), the HUD timer's rolling hundredths, the cannonball's
3-frame camera switch, the rocket belt flame's flicker, the model-trail period (`fx.c`), the chase-camera
history size (50 entries: too short only above 217 Hz), the sound-event throttle (`snd.c`, needed above
~144 Hz).

**Kill switch:** `-DWX_RATE=0` (scale 1: the original steps).

**Origin:** Birdman64 (MIT), `code_82B90.c.patch`, `camera.c.patch`, `ski_lift.c.patch`,
`shuttle.c.patch`, `splash.c.patch`, `falco.c.patch`, `env_sound.c.patch`, `code_AE460.c.patch`,
`hud.c.patch` (their split-screen and widescreen hunks not taken).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).
