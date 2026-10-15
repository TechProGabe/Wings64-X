# 041-rate-frame-counters

**Why (PLAN.md M5):** three frame counters are durations, so at 60 fps they run out in half the time:

- `app/hang_glider2.c` `hangGlider_802F3A80`: `hgData->unk90++` per frame in contact with the ground (after
  the first 2 s); at 91 the glider crashes (`unk8C = 2`). 3 s of scraping at 30 fps, 1.5 s at 60.
- `app/gyrocopter.c` `func_80309090`: the hands-off level-flight trim counts `gcData->unk7C` per frame
  and blends the trim over 50 / 100 frames; `unk88 = .../(100 * dt)` is the blend's length in seconds.
- `app/hud.c` `hudDrawCamera`: the photo shutter's bars last `CAMERA_SHUTTER_FRAMES` (3) frames.

**What (`DIVERGES`):** each counts N64 frames: `n += wx_rate_ticks(&acc)` (whole N64 frames elapsed this
frame, the fraction kept in a file-static accumulator). The gyrocopter's blends use the fractional count,
its `100 * dt` is `100 * WX_REF_DT` (`wx_rate_impulse_dt`), and its `unk7C == 1` setup runs once per
trigger (`sWxGcTrimFresh`), not on every frame of that N64 frame. The shutter count stops at 0 (its high
bits are the `HUD_CAM_RENDER_*` flags).

**Kill switch:** `-DWX_RATE=0` (one tick a frame: the original counts).

**Origin:** Birdman64 (MIT), `hang_glider2.c.patch`, `gyrocopter.c.patch`, `hud.c.patch` (its split-screen
registration and widescreen hunks not taken).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).
