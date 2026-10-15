/* rate.c - the frame-rate scale behind patches/038-045 (wings64_rate.h).
 *
 * wx_rate_frame is func_80313D74's frame time (patches/038): the game's own
 * [0.01, 0.1] s clamp, plus the scale the other patches read. The screens
 * that freeze game time (snap.c's photo screens, replay_screen.c's menu)
 * set a frame time of 1e-5 / 1e-6 s through uvGfxSetFrameTime, which the
 * clamp turned into 0.01 s of game time per frame: 0.34 s a second at the
 * N64's 30 fps, twice that at 60. Here they get the N64's 0.01 s per N64
 * frame of real time instead (Birdman64's rule, MIT), never 0 (callers
 * divide by the frame time).
 *
 * wx_rate_powf: x ** y for x > 0 by exp(y * ln x) in double precision
 * (range reduction on the exponent bits, short series). Not libm's: nxdk's
 * libm is not trusted on the game triple (its lrintf was a stub that
 * asserted; docs/handoff.md M3). Accurate to a few ulp of a float over the
 * range the patches use (x in (0, 1], y in [0.1, 10]); tests/xbox/test_rate.c
 * checks it. Host build: -DWX_RATE_HOST_TEST (no game globals). */
#include <stdint.h>

#include <wings64_rate.h>

float wx_rate_scale_v = 1.0f;

#define LN2 0.69314718055994530942

static double ln_pos(double x) {
    union {
        double d;
        uint64_t u;
    } v;
    double m, s, s2, t, sum;
    int e, i;
    v.d = x;
    e = (int)((v.u >> 52) & 0x7FF) - 1023;
    v.u = (v.u & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;   /* mantissa in [1, 2) */
    m = v.d;
    if (m > 1.4142135623730951) {
        m *= 0.5;
        e++;
    }
    /* ln m = 2 atanh(s), s = (m - 1) / (m + 1), |s| <= 0.1716 */
    s = (m - 1.0) / (m + 1.0);
    s2 = s * s;
    t = s;
    sum = 0.0;
    for (i = 1; i < 30; i += 2) {
        sum += t / (double)i;
        t *= s2;
    }
    return 2.0 * sum + (double)e * LN2;
}

static double exp_any(double y) {
    union {
        double d;
        uint64_t u;
    } v;
    double r, term, sum;
    int n, i;
    if (y > 700.0) y = 700.0;
    if (y < -700.0) return 0.0;
    n = (int)(y / LN2 + (y >= 0.0 ? 0.5 : -0.5));
    r = y - (double)n * LN2;   /* |r| <= ln2 / 2 */
    term = 1.0;
    sum = 1.0;
    for (i = 1; i < 18; i++) {
        term *= r / (double)i;
        sum += term;
    }
    v.d = sum;
    v.u += (uint64_t)(int64_t)n << 52;   /* times 2^n: sum is in [0.7, 1.5], n within +-1010 */
    return v.d;
}

float wx_rate_powf(float x, float y) {
    if (!(x > 0.0f)) return 0.0f;
    if (x == 1.0f || y == 0.0f) return 1.0f;
    if (y == 1.0f) return x;
    return (float)exp_any((double)y * ln_pos((double)x));
}

#ifndef WX_RATE_HOST_TEST
#include <ultra64.h>

/* graphics.c (RECOMP_BUILD exports its STATIC_DATA): the measured frame
 * times and uvGfxSetFrameTime's override */
extern f32 gGfxFrameTime[2];
extern u16 gGfxFbIndex;
extern f32 gGfxFrameTimeFixed;

float wx_rate_frame(float t) {
    float dt;
    if (WX_RATE && gGfxFrameTimeFixed > 0.0f && t < 1e-4f) {
        /* a frozen screen: 0.01 s of game time per N64 frame of real time */
        float real = gGfxFrameTime[gGfxFbIndex ^ 1];
        if (real < 0.01f) real = 0.01f;
        else if (real > 0.1f) real = 0.1f;
        wx_rate_scale_v = real * (1.0f / WX_REF_DT);
        return 0.01f * wx_rate_scale_v;
    }
    /* func_80313D74's own clamp */
    if (t < 0.01f) dt = 0.01f;
    else if (t > 0.1f) dt = 0.1f;
    else dt = t;
    if (WX_RATE) wx_rate_scale_v = dt * (1.0f / WX_REF_DT);
    return dt;
}
#endif
