/* wings64_rate.h - the game at 60 fps with the N64's per-frame behaviour
 * (PLAN.md M5; patches/038-045). Included by the rate patches' hunks and by
 * xbox/src/game/rate.c; plain C, game triple.
 *
 * What was measured first (docs/handoff.md, M5): the game is delta-time
 * driven. func_80313D74 (code_9A960.c) sets the frame time D_8034F854 from
 * the wall time between two uvGfxEnd calls, clamped to [0.01, 0.1] s, and
 * the vehicles, cameras, timers and the world integrate with it; the
 * scheduler takes a display list at every retrace (numFields 1, sched.c), so
 * the port already draws up to 60 frames a second and the game's speed is
 * right. What is not right at 60 is the code that steps something once per
 * call, i.e. once per N64 frame: counters used as durations, one-frame
 * impulses, steps without dt, per-frame random draws, per-frame decays.
 * These helpers rescale such steps to WX_REF_DT, the frame time the N64
 * game was tuned at, so they behave per second as they did on the N64.
 *
 * Derived from Birdman64's pw64_rate.h (https://github.com/NoahVl/Birdman64,
 * MIT; its docs/notes/framerate.md audit), with three changes: powf is our
 * own (wx_rate_powf: nxdk's libm is not trusted, its lrintf was a stub),
 * WX_REF_DT is the N64's 30 fps frame as the game's clock reads it, and
 * WX_RATE=0 turns every helper back into the original per-frame step.
 *
 * Kill switch: -DWX_RATE=0 (all of patches/038-045 then compute what the
 * N64 code computes; the frame rate is still the Settings row's). */
#ifndef WINGS64_RATE_H
#define WINGS64_RATE_H

#ifndef WX_RATE
#define WX_RATE 1
#endif

/* The frame time the per-frame constants assume: an N64 frame at 30 fps
 * (two NTSC fields) as the game measures it. The game divides the CPU
 * counter by 45.75 MHz (uv_clocks.h UV_CLK_TICK_FREQ) while the counter
 * runs at 46.875 MHz, so on the N64 (and here, os_shim.c osGetCount) a
 * 1/30 s frame reads 0.034152 s. With the Frame rate row at 30 the scale
 * below is then 1 and every patch reduces to the original step. */
#define WX_REF_DT 0.0341516f

/* N64 frames per current frame: D_8034F854 / WX_REF_DT, or the real frame
 * time over WX_REF_DT on the screens that freeze game time (patches/038).
 * Set once a frame by func_80313D74; 1 when WX_RATE is 0. */
extern float wx_rate_scale_v;

/* x ** y for x > 0 (0 for x <= 0); rate.c, double precision inside. */
float wx_rate_powf(float x, float y);

/* func_80313D74's frame time (patches/038): `frame_time` is
 * uvGfxGetFrameTime(). Returns D_8034F854 and sets wx_rate_scale_v. */
float wx_rate_frame(float frame_time);

/* A per-frame step `x += c` becomes `x += c * wx_rate_scale()`. */
static inline float wx_rate_scale(void) {
    return wx_rate_scale_v;
}

/* A per-frame factor `x *= k` (k > 0) becomes `x *= wx_rate_decay(k)`:
 * the same decay per second as k per N64 frame. */
static inline float wx_rate_decay(float k) {
    return WX_RATE ? wx_rate_powf(k, wx_rate_scale_v) : k;
}

/* A frame counter used as a duration (`n++` per frame) becomes
 * `n += wx_rate_ticks(&acc)`: whole N64 frames elapsed this frame (0..3),
 * the fraction kept in *acc (starts at 0). 1 every frame when WX_RATE is 0. */
static inline int wx_rate_ticks(float* acc) {
    float a = *acc + wx_rate_scale_v;
    int n = (int)a;
    *acc = a - (float)n;
    return n;
}

/* A random walk stepped once a frame (`x += (rand - 0.5) * r * dt`) spreads
 * as sqrt(dt): multiply the step by this for the N64 frame's spread. */
static inline float wx_rate_walk(void) {
    return WX_RATE ? __builtin_sqrtf(1.0f / wx_rate_scale_v) : 1.0f;
}

/* A one-frame impulse `x += c * dt` (applied on a button's press frame)
 * becomes `x += c * wx_rate_impulse_dt(dt)`: an N64 frame's worth. */
static inline float wx_rate_impulse_dt(float dt) {
    return WX_RATE ? WX_REF_DT : dt;
}

#endif
