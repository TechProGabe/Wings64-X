# 038-rate-frame-scale

**Why (PLAN.md M5, real 60 fps):** measured first: the game is delta-time driven. `func_80313D74`
(`app/code_9A960.c`, once per game frame) sets the frame time `D_8034F854` from the wall time between two
`uvGfxEnd` calls (`uvGfxGetFrameTime`, the CPU counter), clamped to [0.01, 0.1] s, and adds it to the game
time `D_8034F850`; the vehicles, cameras, test timers and the world integrate with it. The scheduler takes
a display list at every retrace (`numFields` 1, `kernel/sched.c`), so the port already draws up to 60
frames a second and the game runs at the right speed. What changes at 60 is the code that steps once per
call, i.e. once per N64 frame (patches 039-045). They need to know how many N64 frames this frame stands
for.

**What (`func_80313D74`, `DIVERGES`):** `D_8034F854 = wx_rate_frame(uvGfxGetFrameTime())`
(`xbox/src/game/rate.c`): the same [0.01, 0.1] clamp, and `wx_rate_scale_v = D_8034F854 / WX_REF_DT`
(`xbox/include/game/wings64_rate.h`), `WX_REF_DT` = 0.034152 s, an N64 frame at 30 fps as the game's
clock reads it (the game divides the 46.875 MHz counter by 45.75 MHz, `uv_clocks.h`, so its seconds run
2.46 % fast on the N64 and here alike). The frozen-time screens (`snap.c`'s photo screens and
`replay_screen.c`'s menu set a 1e-5 / 1e-6 s frame time through `uvGfxSetFrameTime`) had the clamp's 0.01
s of game time per frame, i.e. per N64 frame at 30 fps and twice that at 60: they get 0.01 s per N64
frame of real time (the scale is then the real frame time over `WX_REF_DT`).

**Not taken from Birdman64** (needed only above ~100 Hz, the port's cap is 60): the lower clamp 0.01 ->
0.002, the f64 shadow of `D_8034F850` and of the five test timers.

**Kill switch:** `-DWX_RATE=0`: `wx_rate_frame` is the original clamp, the scale stays 1 and patches
039-045 compute the original per-frame steps. The Frame rate row (`fps_mode`, 60 / 30) is independent.

**Origin:** Birdman64 (https://github.com/NoahVl/Birdman64, MIT), `code_9A960.c.patch` and
`native/include/pw64_rate.h`, its `docs/notes/framerate.md` audit; adapted (own `powf`, `WX_REF_DT` from the
game's clock constant, the kill switch, `wx_` names).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).
