/* asp_ref.c - an independent, deliberately plain reference for test_asp_hle.c.
 *
 * Written from the definitions rather than from asp_hle.c: 64-bit
 * arithmetic everywhere, floor divisions spelled as divisions (no shifts),
 * rounding written in the RSP's own vmulf form ((2ab + 0x8000) / 65536)
 * instead of (ab + 0x4000) >> 15, closed-form positions and ramps where
 * asp_hle.c steps incrementally. Sources:
 *   - VADPCM: the N64 SDK's codebook math (vadpcm_dec): a frame is a header
 *     (scale << 4 | predictor) and 16 nibbles, decoded as two 8-sample vectors;
 *     out[k] = (sum_j book[p][j][k] * y[j-2] + 2048 e[k] + sum_{m<k}
 *     book[p][1][k-1-m] e[m]) / 2048, floored, saturated, e = nibble << scale
 *     (the microcode caps the scale at 12).
 *   - the resampler, envelope, mixer and pole filter: the rules in asp_hle.c's
 *     top comment (Birdman64's abi.rs semantics, MIT).
 * No ROM data: every table and stream in the tests is synthetic. */
#include "asp_ref.h"

static int64_t fdiv(int64_t a, int64_t b)   /* floor(a / b), b > 0 */
{
    int64_t q = a / b;
    if ((a % b) != 0 && a < 0) q -= 1;
    return q;
}

static int16_t clamp16(int64_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

static int64_t vmulf(int64_t a, int64_t b) { return fdiv(2 * a * b + 0x8000, 65536); }

void ref_adpcm(const int16_t book[128], const uint8_t* frames, int nframes, int16_t hist[16], int16_t* out)
{
    int64_t y2 = hist[14], y1 = hist[15];
    int f, v, k, m;
    for (f = 0; f < nframes; f++) {
        const uint8_t* fr = frames + 9 * f;
        int scale = fr[0] / 16, pred = fr[0] % 16;
        if (scale > 12) scale = 12;
        if (pred > 7) pred = 7;
        for (v = 0; v < 2; v++) {
            int64_t e[8], y[8];
            const int16_t* row0 = book + 16 * pred;     /* weights of y[-2] */
            const int16_t* row1 = book + 16 * pred + 8; /* weights of y[-1] and of the in-vector residuals */
            for (k = 0; k < 8; k++) {
                int byte = fr[1 + 4 * v + k / 2];
                int nib = (k % 2 == 0) ? byte / 16 : byte % 16;
                if (nib >= 8) nib -= 16;
                e[k] = (int64_t)nib * ((int64_t)1 << scale);
            }
            for (k = 0; k < 8; k++) {
                int64_t acc = row0[k] * y2 + row1[k] * y1 + 2048 * e[k];
                for (m = 0; m < k; m++) acc += row1[k - 1 - m] * e[m];
                y[k] = clamp16(fdiv(acc, 2048));
                out[16 * f + 8 * v + k] = (int16_t)y[k];
            }
            y2 = y[6];
            y1 = y[7];
        }
    }
    if (nframes)
        for (k = 0; k < 16; k++) hist[k] = out[16 * (nframes - 1) + k];
}

void ref_resample(const int16_t* z, int nout, uint32_t pitch, uint32_t frac0, const int16_t lut[256], int per_tap,
                  int16_t* out)
{
    uint64_t step = (uint64_t)pitch * 2;
    int j, k;
    for (j = 0; j < nout; j++) {
        uint64_t p = frac0 + (uint64_t)j * step;
        uint64_t idx = p / 65536;
        int phase = (int)((p % 65536) / 1024);
        int64_t acc = 0;
        if (per_tap) {
            for (k = 0; k < 4; k++) acc += vmulf(z[idx + k], lut[4 * phase + k]);
            out[j] = clamp16(acc);
        } else {
            for (k = 0; k < 4; k++) acc += (int64_t)z[idx + k] * lut[4 * phase + k];
            out[j] = clamp16(fdiv(2 * acc + 0x8000, 65536));
        }
    }
}

void ref_env_init(RefEnv* e, int16_t vl, int16_t vr, int16_t tl, int16_t tr, int32_t rl, int32_t rr)
{
    e->value[0] = e->seq[0] = vl * 65536;
    e->value[1] = e->seq[1] = vr * 65536;
    e->target[0] = tl * 65536;
    e->target[1] = tr * 65536;
    e->rate[0] = rl;
    e->rate[1] = rr;
}

void ref_env_volumes(RefEnv* e, int n, int16_t* vol_l, int16_t* vol_r)
{
    int b, c, i;
    for (b = 0; b < n; b += 8) {
        for (c = 0; c < 2; c++) {
            int16_t* vol = c ? vol_r : vol_l;
            int64_t v0 = e->value[c], tgt = e->target[c], step = 0;
            if (v0 != tgt) {
                int64_t s = fdiv((int64_t)e->seq[c] * e->rate[c], 65536);
                if (s > INT32_MAX) s = INT32_MAX;
                if (s < INT32_MIN) s = INT32_MIN;
                e->seq[c] = (int32_t)s;
                step = (s - v0) / 8;    /* C division truncates, as the ramp step does */
            }
            /* lane i of the block: v0 + (i + 1) step until it reaches the target, then the target */
            for (i = 0; i < 8; i++) {
                int64_t v = v0 + (i + 1) * step;
                if (step > 0 && v >= tgt) v = tgt, step = 0, v0 = tgt;
                if (step < 0 && v <= tgt) v = tgt, step = 0, v0 = tgt;
                if (step == 0) v = v0;
                vol[b + i] = (int16_t)fdiv(v, 65536);
                if (i == 7) e->value[c] = (int32_t)v;
            }
        }
    }
}

int16_t ref_gain(int16_t vol, int16_t amount) { return clamp16(vmulf(vol, amount)); }

void ref_mix(int16_t* dst, const int16_t* src, int n, int16_t gain)
{
    int i;
    for (i = 0; i < n; i++) dst[i] = clamp16(dst[i] + vmulf(src[i], gain));
}

void ref_mix_var(int16_t* dst, const int16_t* src, int n, const int16_t* gains)
{
    int i;
    for (i = 0; i < n; i++) dst[i] = clamp16(dst[i] + vmulf(src[i], gains[i]));
}

void ref_interleave(int16_t* out, const int16_t* l, const int16_t* r, int n)
{
    int i;
    for (i = 0; i < n; i++) out[2 * i] = l[i], out[2 * i + 1] = r[i];
}

void ref_polef(const int16_t table[16], int16_t gain, const int16_t* x, int n, int16_t y[2], int16_t* out)
{
    int g, k, j;
    for (g = 0; g < n; g += 8) {
        int16_t yy[8];
        for (k = 0; k < 8; k++) {
            int64_t acc = (int64_t)gain * x[g + k] + (int64_t)table[k] * y[0] + (int64_t)table[8 + k] * y[1];
            for (j = 0; j < k; j++) {
                /* the impulse-response tap scaled by the gain, kept as the microcode keeps it: 16 bits */
                int16_t hg = (int16_t)fdiv((int64_t)table[8 + k - 1 - j] * gain, 16384);
                acc += (int64_t)hg * x[g + j];
            }
            yy[k] = clamp16(fdiv(acc, 16384));
        }
        for (k = 0; k < 8; k++) out[g + k] = yy[k];
        y[0] = yy[6];
        y[1] = yy[7];
    }
}
