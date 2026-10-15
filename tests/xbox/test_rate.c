/* test_rate.c - host test for xbox/src/game/rate.c and wings64_rate.h
 * (patches/038-045): wx_rate_powf against the host's pow, and the helpers
 * giving the same result per second of game time at 30, 60 and odd rates
 * as one N64 frame at a time does. Build: tools/xbox/test_rate.py
 * (-DWX_RATE_HOST_TEST). Exit status 0 = pass. */
#include <math.h>
#include <stdio.h>

#include <wings64_rate.h>

static int failures, checks;

static void expect_near(const char* what, double got, double want, double tol) {
    checks++;
    if (!(fabs(got - want) <= tol)) {
        printf("FAIL %s: got %.9g want %.9g (tol %.3g)\n", what, got, want, tol);
        failures++;
    }
}

static void expect_int(const char* what, long got, long want) {
    checks++;
    if (got != want) {
        printf("FAIL %s: got %ld want %ld\n", what, got, want);
        failures++;
    }
}

/* patches/039's func_80313AF4 (the approach helper), host copy */
static float approach(float target, float cur, float k, float dt) {
    float d = target - cur, step;
    if (fabsf(d) < 0.00001f) return target;
    if (k > 0.0f && k * WX_REF_DT < 1.0f)
        step = d * (1.0f - wx_rate_powf(1.0f - k * WX_REF_DT, dt * (1.0f / WX_REF_DT)));
    else
        step = k * d * dt;
    if (fabsf(d) < fabsf(step)) return target;
    return cur + step;
}

int main(void) {
    static const float xs[] = { 1e-6f, 0.01f, 0.25f, 0.5f, 0.8f, 0.95f, 0.985f, 0.99f, 0.995f, 0.999f, 1.0f, 1.5f, 3.8f };
    static const float ys[] = { 0.1f, 0.29f, 0.5f, 0.5001f, 1.0f, 1.02454f, 2.0f, 2.93f, 10.0f };
    static const int rates[] = { 30, 60, 45, 50, 20 };
    unsigned i, j;
    int r;

    /* wx_rate_powf vs pow: relative error under 4 float ulp */
    for (i = 0; i < sizeof xs / sizeof xs[0]; i++)
        for (j = 0; j < sizeof ys / sizeof ys[0]; j++) {
            char what[64];
            double want = pow((double)xs[i], (double)ys[j]);
            snprintf(what, sizeof what, "powf(%g, %g)", (double)xs[i], (double)ys[j]);
            expect_near(what, wx_rate_powf(xs[i], ys[j]), want, want * 5e-7 + 1e-30);
        }
    expect_near("powf(0, 2)", wx_rate_powf(0.0f, 2.0f), 0.0, 0.0);
    expect_near("powf(-1, 2)", wx_rate_powf(-1.0f, 2.0f), 0.0, 0.0);
    expect_near("powf(0.7, 1) exact", wx_rate_powf(0.7f, 1.0f), 0.7f, 0.0);

    /* per second: the helpers at N Hz match 30 N64 frames' worth (REF_DT a frame) */
    for (r = 0; r < (int)(sizeof rates / sizeof rates[0]); r++) {
        int hz = rates[r], f, frames = hz * 3;   /* 3 s */
        float dt = (1.0f / (float)hz) * (WX_REF_DT * 30.0f);   /* the game clock's reading of a 1/hz s frame */
        float ref_frames = 3.0f * 30.0f;
        float decay = 1.0f, step = 0.0f, acc = 0.0f, a = 0.0f, a_ref = 0.0f;
        long ticks = 0;
        char what[64];
        wx_rate_scale_v = dt / WX_REF_DT;
        for (f = 0; f < frames; f++) {
            decay *= wx_rate_decay(0.95f);
            step += 3.0f * wx_rate_scale();
            ticks += wx_rate_ticks(&acc);
            a = approach(10.0f, a, 15.0f, dt);
        }
        for (f = 0; f < 90; f++) a_ref = approach(10.0f, a_ref, 15.0f, WX_REF_DT);
        snprintf(what, sizeof what, "decay 0.95 over 3 s at %d Hz", hz);
        expect_near(what, decay, pow(0.95, ref_frames), pow(0.95, ref_frames) * 2e-4);
        snprintf(what, sizeof what, "step 3/frame over 3 s at %d Hz", hz);
        expect_near(what, step, 3.0 * ref_frames, 0.01);
        snprintf(what, sizeof what, "ticks over 3 s at %d Hz", hz);
        expect_int(what, ticks + (acc > 0.999f ? 1 : 0), 90);
        snprintf(what, sizeof what, "approach over 3 s at %d Hz", hz);
        expect_near(what, a, a_ref, 1e-3);
    }

    /* one N64 frame of approach at 60 Hz = two half frames, k * REF_DT = 0.5 */
    {
        float dt = WX_REF_DT * 0.5f;
        float a = approach(1.0f, 0.0f, 0.5f / WX_REF_DT, dt);
        a = approach(1.0f, a, 0.5f / WX_REF_DT, dt);
        expect_near("approach k*REF 0.5: two half frames", a, 0.5, 1e-6);
    }

    /* the walk: steps of variance scale*REF^2*walk^2 add up to the REF variance */
    wx_rate_scale_v = 0.5f;
    expect_near("walk at 60", wx_rate_walk(), sqrt(2.0), 1e-6);
    wx_rate_scale_v = 1.0f;
    expect_near("walk at 30", wx_rate_walk(), 1.0, 0.0);
    expect_near("decay at scale 1", wx_rate_decay(0.985f), 0.985f, 0.0);

    printf("test_rate: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
