# 043-rate-birdman-flap

**Why (PLAN.md M5):** `app/birdman0.c` `birdMovementFrame`: a flap (A or B pressed this frame) adds
`9 * dt` to the flap strength `unk2D0` once, on the press frame, and skips that frame's decays of
`unk2D0` / `unk2D4`. Both are tuned to an N64 frame: at 60 fps each flap adds half as much and skips half
the decay time. Lift and thrust go as `unk2D0` squared and a standing take-off needs `unk2D0 > 1.9`
(`birdman2.c`), so at 60 the birdman flaps but barely climbs, and cannot take off again after landing
(Birdman64 found this in playtesting: at 5 presses a second the flap settled at 1.6 at 60 Hz against
~3.5 at the N64's rate).

**What (`DIVERGES`):** the press adds an N64 frame's impulse, `9 * WX_REF_DT` (`wx_rate_impulse_dt`), and
the decays then skip an N64 frame of decay time (`sWxFlapHold`): whole frames while it lasts, and the
frame that ends it decays by the remainder (`D_8034F854` is lowered around the two `func_80313AF4` calls,
which read the frame time from it, and restored).

**Kill switch:** `-DWX_RATE=0`: the impulse is `9 * dt` and the hold one frame's `dt`, i.e. exactly the
original press-frame skip.

**Origin:** Birdman64 (MIT), `birdman0.c.patch` (its f64 test-timer shadow and split-screen registration
not taken).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).
