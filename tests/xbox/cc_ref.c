/* cc_ref.c - the N64 RDP colour combiner in software: the reference for
 * tests/xbox/test_cc.c.
 *
 * Model: angrylion's RDP (n64video.c: color_combiner_equation,
 * alpha_combiner_equation, combiner_1cycle / combiner_2cycle, the
 * special_9bit_exttable / special_9bit_clamptable), the bit-accurate reference
 * other emulators (ParaLLEl-RDP) are checked against. Inputs are 8-bit
 * (0..255); the constant 1 is 0x100. Per cycle and channel
 *     r = (ext(A) - ext(B)) * sext(C) + (ext(D) << 8) + 0x80
 *     result = (r >> 8) & 0x1FF      (9 bits, kept between cycles)
 * where ext() reads 9 bits as -128..383 (0x180..0x1FF negative) and sext()
 * reads them as -256..255 (C is a signed 9-bit multiplier). The last cycle's
 * result is clamped: 0x100..0x17F -> 255, 0x180..0x1FF -> 0. So C = 255 is
 * 255/256, not 1.0, and sums past 383 wrap (to black).
 *
 * Mux tables: gbi.h G_CCMUX_* (rgb a: 0..5 COMBINED TEXEL0 TEXEL1 PRIM SHADE
 * ENV, 6 1, 7 NOISE, 8+ 0; b: 6 CENTER, 7 K4, 8+ 0; c: 6 SCALE, 7
 * COMBINED_ALPHA, 8..12 TEXEL0/TEXEL1/PRIM/SHADE/ENV alpha, 13 LOD_FRACTION,
 * 14 PRIM_LOD_FRAC, 15 K5, 16+ 0; d: 6 1, 7 0) and G_ACMUX_* (a, b, d: 0..5
 * as rgb, 6 1, 7 0; c: 0 LOD_FRACTION, 1..5, 6 PRIM_LOD_FRAC, 7 0).
 *
 * Not modelled (the compiler follows the same choices; nv2a_cc.c says why):
 * the second cycle's TEXEL0/TEXEL1 one texel ahead; COMBINED in the first
 * cycle reads 0 here (the previous pixel's on the RDP); 1-cycle mode uses
 * cycle 0's fields (the RDP runs cycle 1's; the interpreter normalises).
 *
 * CCREF_RANGE marks results that depend on the 9-bit arithmetic beyond what
 * an NV2A register holds ([-1, 1] = -255..255, clamped at each write): a
 * (A - B) * C past +-1.0, a first-cycle result outside -128..256 read by the
 * second cycle (outside -128..255 when read as C, where 0x100 is -1.0), a
 * last-cycle sum outside -128..383 (it wraps).
 *
 * cc_ideal_eval is the same combiner in exact arithmetic on the NV2A's scale
 * (255 = 1.0, the constant 1 = 1.0, no rounding until the end, the first
 * cycle's result held in [-1, 1]): what a correct register-combiner program
 * computes. */
#include "cc_ref.h"

/* the inputs, resolved from (slot, field) */
enum { I_ZERO, I_ONE, I_COMB, I_COMB_A, I_T0, I_T1, I_PRIM, I_SHADE, I_ENV, I_T0A, I_T1A, I_PRIMA, I_SHADEA, I_ENVA,
       I_LOD, I_PLOD, I_NOISE, I_CENTER, I_K4, I_SCALE, I_K5 };

static int rgb_src(int slot, int v) {
    static const int common[6] = { I_COMB, I_T0, I_T1, I_PRIM, I_SHADE, I_ENV };
    if (v <= 5) return common[v];
    switch (slot) {
        case 0: return v == 6 ? I_ONE : v == 7 ? I_NOISE : I_ZERO;
        case 1: return v == 6 ? I_CENTER : v == 7 ? I_K4 : I_ZERO;
        case 2: {
            static const int c[10] = { I_SCALE, I_COMB_A, I_T0A, I_T1A, I_PRIMA, I_SHADEA, I_ENVA, I_LOD, I_PLOD, I_K5 };
            return v <= 15 ? c[v - 6] : I_ZERO;
        }
        default: return v == 6 ? I_ONE : I_ZERO;
    }
}

static int alpha_src(int slot, int v) {
    static const int common[6] = { I_COMB_A, I_T0A, I_T1A, I_PRIMA, I_SHADEA, I_ENVA };
    if (slot == 2 && v == 0) return I_LOD;
    if (slot == 2 && v == 6) return I_PLOD;
    if (v == 6) return I_ONE;
    if (v == 7) return I_ZERO;
    return common[v];
}

/* an input's 8-bit value (I_COMB*: not here) */
static int value(const CcRefIn* in, int src, int ch) {
    switch (src) {
        case I_ONE: return 0x100;
        case I_T0: return in->t0[ch];
        case I_T1: return in->t1[ch];
        case I_PRIM: return in->prim[ch];
        case I_SHADE: return in->shade[ch];
        case I_ENV: return in->env[ch];
        case I_T0A: return in->t0[3];
        case I_T1A: return in->t1[3];
        case I_PRIMA: return in->prim[3];
        case I_SHADEA: return in->shade[3];
        case I_ENVA: return in->env[3];
        case I_LOD: return in->lod_frac;
        case I_PLOD: return in->prim_lod_frac;
        case I_NOISE: return in->noise;
        case I_CENTER: return in->center[ch < 3 ? ch : 0];
        case I_K4: return in->k4;
        case I_SCALE: return in->scale[ch < 3 ? ch : 0];
        case I_K5: return in->k5;
        default: return 0;
    }
}

typedef struct { int src[2][2][4]; } Fields;   /* [cycle][rgb, alpha][a b c d] */

static void fields(uint32_t w0, uint32_t w1, Fields* f) {
    /* gbi.h GCCc0w0 / GCCc1w0 / GCCc0w1 / GCCc1w1 */
    const int rgb[2][4] = { { (int)(w0 >> 20 & 0xF), (int)(w1 >> 28 & 0xF), (int)(w0 >> 15 & 0x1F), (int)(w1 >> 15 & 7) },
                            { (int)(w0 >> 5 & 0xF), (int)(w1 >> 24 & 0xF), (int)(w0 & 0x1F), (int)(w1 >> 6 & 7) } };
    const int a[2][4] = { { (int)(w0 >> 12 & 7), (int)(w1 >> 12 & 7), (int)(w0 >> 9 & 7), (int)(w1 >> 9 & 7) },
                          { (int)(w1 >> 21 & 7), (int)(w1 >> 3 & 7), (int)(w1 >> 18 & 7), (int)(w1 & 7) } };
    int c, k;
    for (c = 0; c < 2; c++)
        for (k = 0; k < 4; k++) {
            f->src[c][0][k] = rgb_src(k, rgb[c][k]);
            f->src[c][1][k] = alpha_src(k, a[c][k]);
        }
}

/* ---- the RDP ---- */
static int ext9(int x) {
    x &= 0x1FF;
    return (x & 0x180) == 0x180 ? x - 0x200 : x;
}

static int sext9(int x) {
    x &= 0x1FF;
    return (x & 0x100) ? x - 0x200 : x;
}

static int clamp9(int x) {
    x &= 0x1FF;
    if (x & 0x100) return (x & 0x80) ? 0 : 255;
    return x;
}

static int floor_div256(int r) { return r >= 0 ? r / 256 : -((-r + 255) / 256); }

/* the NV2A's view of a value for the product range check: 0x100 (1.0) is 255 */
static int nv(int v) { return v == 256 ? 255 : v; }

typedef struct {
    int u[4];          /* the previous cycle's result, unwrapped (-1 << 20: none) */
    int have;
    uint32_t flags;
} St;

/* 9-bit raw value of input k of a cycle */
static int fetch(St* s, const CcRefIn* in, int src, int ch, int slot) {
    if (src == I_COMB || src == I_COMB_A) {
        int c = src == I_COMB_A ? 3 : ch, u;
        if (!s->have) return 0;
        u = s->u[c];
        if (u < -128 || u > (slot == 2 ? 255 : 256)) s->flags |= CCREF_RANGE;
        return u & 0x1FF;
    }
    return value(in, src, ch);
}

static int eq(St* s, int a, int b, int c, int d) {
    int ea = ext9(a), eb = ext9(b), sc = sext9(c), ed = ext9(d);
    int p = (nv(ea) - nv(eb)) * nv(sc);
    if (p > 255 * 255 || p < -255 * 255) s->flags |= CCREF_RANGE;
    return floor_div256((ea - eb) * sc + (ed << 8) + 0x80);
}

static void cycle(St* s, const CcRefIn* in, const int src[2][4], int u[4]) {
    int ch, k, v[4];
    for (ch = 0; ch < 4; ch++) {
        const int* sr = src[ch == 3];
        for (k = 0; k < 4; k++) v[k] = fetch(s, in, sr[k], ch, k);
        u[ch] = eq(s, v[0], v[1], v[2], v[3]);
    }
}

uint32_t cc_ref_eval(uint32_t w0, uint32_t w1, int two_cycle, const CcRefIn* in, uint8_t out[4]) {
    Fields f;
    St s = { { 0, 0, 0, 0 }, 0, 0 };
    int u[4], ch;
    fields(w0, w1, &f);
    cycle(&s, in, f.src[0], u);
    if (two_cycle) {
        for (ch = 0; ch < 4; ch++) s.u[ch] = u[ch];
        s.have = 1;
        cycle(&s, in, f.src[1], u);
    }
    for (ch = 0; ch < 4; ch++) {
        if (u[ch] < -128 || u[ch] > 383) s.flags |= CCREF_RANGE;
        out[ch] = (uint8_t)clamp9(u[ch]);
    }
    return s.flags;
}

/* ---- exact arithmetic ---- */
static void icycle(const double* prev, const CcRefIn* in, const int src[2][4], double u[4]) {
    int ch, k;
    double v[4];
    for (ch = 0; ch < 4; ch++) {
        const int* sr = src[ch == 3];
        for (k = 0; k < 4; k++) {
            int sk = sr[k];
            if (sk == I_COMB || sk == I_COMB_A) v[k] = prev ? prev[sk == I_COMB_A ? 3 : ch] : 0.0;
            else if (sk == I_ONE) v[k] = 1.0;
            else v[k] = value(in, sk, ch) / 255.0;
        }
        u[ch] = (v[0] - v[1]) * v[2] + v[3];
    }
}

void cc_ideal_eval(uint32_t w0, uint32_t w1, int two_cycle, const CcRefIn* in, double out[4]) {
    Fields f;
    double u0[4];
    int ch;
    fields(w0, w1, &f);
    icycle(0, in, f.src[0], u0);
    /* the first cycle's result in a register: [-1, 1] (where the RDP keeps more, CCREF_RANGE says so) */
    for (ch = 0; ch < 4; ch++) u0[ch] = u0[ch] < -1.0 ? -1.0 : u0[ch] > 1.0 ? 1.0 : u0[ch];
    if (two_cycle) icycle(u0, in, f.src[1], out);
    else for (ch = 0; ch < 4; ch++) out[ch] = u0[ch];
    for (ch = 0; ch < 4; ch++) out[ch] = out[ch] < 0.0 ? 0.0 : out[ch] > 1.0 ? 1.0 : out[ch];
}
