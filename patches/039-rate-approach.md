# 039-rate-approach

**Why (PLAN.md M5):** `func_80313AF4(target, x, k)` (`app/code_9A960.c`, ~170 callers: cameras, control
smoothing, vehicle states) moves `x` by `k * (target - x) * dt` a frame, snapping to the target on an
overshoot. That explicit-Euler step settles at a rate that depends on the frame rate when `k * dt` is not
small: the game's k = 10-15 give `k * dt` ~ 0.5 per N64 frame at 30 fps, so a value halves in one N64
frame there but only drops to 0.56 in the same time at 60 (two steps of 0.25): smoothing and recoveries
feel slower at 60.

**What (`func_80313AF4`, `DIVERGES`):** for `k > 0` and `k * WX_REF_DT < 1`, the step is the fraction the
N64 covered in `dt`: `(target - x) * (1 - (1 - k * WX_REF_DT) ** (dt / WX_REF_DT))`
(`wx_rate_powf`, `xbox/src/game/rate.c`). At 30 fps (`dt` = `WX_REF_DT`) this is the original step;
`k <= 0` and `k * WX_REF_DT >= 1` (the snap) are unchanged. `func_80313BAC` (fixed rate toward a target)
is rate-independent and left alone. `tests/xbox/test_rate.c` checks that 3 s of the step at 20, 30, 45, 50
and 60 Hz land where 90 N64 frames do.

**Kill switch:** `-DWX_RATE=0` (the original step).

**Origin:** Birdman64 (MIT), `code_9A960.c.patch`; adapted (`wx_rate_powf` instead of libm's `powf`, the
kill switch).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).
