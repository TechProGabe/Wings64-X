/* test_cc.c - the N64 combiner -> NV2A register combiner compiler
 * (xbox/src/hw/nv2a_cc.c) against a software N64 combiner (cc_ref.c).
 * Run by tools/xbox/test_cc.py.
 *
 * A model of the NV2A register combiners runs each compiled CcProg: it
 * decodes the ICW/OCW/CW0/CW1 words itself (not with the encoder's tables):
 * per input the register (bits 0-3), the alpha/blue select (bit 4) and the
 * mapping (bits 5-7); per stage the AB and CD products (or dot products),
 * their sum, the output shift/bias, the clamp to [-1, 1] at each register
 * write, all reads of a stage before its writes; the final combiner's
 * A*B + (1-A)*C + D and G with unsigned inputs and the [0, 1] clamp. Float
 * arithmetic on 8-bit inputs (the NV2A's internal precision is not modelled).
 * The back end's register contract (nv2a_cc.h) loads the registers.
 *
 * Checks: every PW64 combiner mode (table below) compiles exactly
 * (approximated = 0) to the expected number of stages and matches the RDP
 * model within +-2/255 per channel in 1- and 2-cycle mode, fog off and on;
 * ~2000 random combiner words (1- and 2-cycle) with random inputs: every
 * exact program matches within the same tolerance wherever the RDP's result
 * doesn't depend on its 9-bit wrap (CCREF_RANGE, counted); approximated
 * programs are counted by cause.
 * Usage: test_cc [random words, default 2000] [inputs per word, default 200] */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cc_ref.h"
#include "nv2a_cc.h"

/* |RC - RDP| allowed (/255): the PW64 modes and any one-cycle word */
#define TOL 2
/* ... a random two-cycle word: the RDP's C/256 multiply and 0x100 constant
 * are each up to ~1.5/255 off exact arithmetic per cycle, and the first
 * cycle's error is carried into the second (cc_ref.c) */
#define TOL2 4
/* |RC - exact arithmetic| allowed (/255): the encoding itself */
#define ITOL 0.01

/* ---------------------------------------------------------------- RNG */
static uint32_t s_rng = 0x12345678u;
static uint32_t rnd(void) {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}
/* a byte, often at the ends of the range */
static uint8_t rbyte(void) {
    uint32_t r = rnd();
    switch (r & 7) {
        case 0: return 0;
        case 1: return 255;
        default: return (uint8_t)(r >> 8);
    }
}

/* ------------------------------------------------- gbi.h combiner words */
/* G_CCMUX_* / G_ACMUX_* (gbi.h:444-476) by name, for token pasting */
#define CCM_COMBINED 0
#define CCM_TEXEL0 1
#define CCM_TEXEL1 2
#define CCM_PRIMITIVE 3
#define CCM_SHADE 4
#define CCM_ENVIRONMENT 5
#define CCM_CENTER 6
#define CCM_SCALE 6
#define CCM_COMBINED_ALPHA 7
#define CCM_TEXEL0_ALPHA 8
#define CCM_TEXEL1_ALPHA 9
#define CCM_PRIMITIVE_ALPHA 10
#define CCM_SHADE_ALPHA 11
#define CCM_ENV_ALPHA 12
#define CCM_LOD_FRACTION 13
#define CCM_PRIM_LOD_FRAC 14
#define CCM_NOISE 7
#define CCM_K4 7
#define CCM_K5 15
#define CCM_1 6
#define CCM_0 31
#define ACM_COMBINED 0
#define ACM_TEXEL0 1
#define ACM_TEXEL1 2
#define ACM_PRIMITIVE 3
#define ACM_SHADE 4
#define ACM_ENVIRONMENT 5
#define ACM_LOD_FRACTION 0
#define ACM_PRIM_LOD_FRAC 6
#define ACM_1 6
#define ACM_0 7

/* gbi.h _SHIFTL and GCCc0w0 / GCCc1w0 / GCCc0w1 / GCCc1w1 */
#define SH(v, s, w) ((uint32_t)((v) & ((1u << (w)) - 1)) << (s))
#define W0(a0, c0, Aa0, Ac0, a1, c1) (SH(a0, 20, 4) | SH(c0, 15, 5) | SH(Aa0, 12, 3) | SH(Ac0, 9, 3) | SH(a1, 5, 4) | SH(c1, 0, 5))
#define W1(b0, d0, Ab0, Ad0, b1, Aa1, Ac1, d1, Ab1, Ad1)                                                              \
    (SH(b0, 28, 4) | SH(d0, 15, 3) | SH(Ab0, 12, 3) | SH(Ad0, 9, 3) | SH(b1, 24, 4) | SH(Aa1, 21, 3) | SH(Ac1, 18, 3) |  \
     SH(d1, 6, 3) | SH(Ab1, 3, 3) | SH(Ad1, 0, 3))
#define LERP(a0, b0, c0, d0, Aa0, Ab0, Ac0, Ad0, a1, b1, c1, d1, Aa1, Ab1, Ac1, Ad1)                              \
    W0(CCM_##a0, CCM_##c0, ACM_##Aa0, ACM_##Ac0, CCM_##a1, CCM_##c1),                                         \
        W1(CCM_##b0, CCM_##d0, ACM_##Ab0, ACM_##Ad0, CCM_##b1, ACM_##Aa1, ACM_##Ac1, CCM_##d1, ACM_##Ab1, ACM_##Ad1)
#define LERPX(...) LERP(__VA_ARGS__)
#define MODE(a, b) LERPX(a, b)

/* gbi.h:479-555 */
#define G_CC_PRIMITIVE 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE
#define G_CC_SHADE 0, 0, 0, SHADE, 0, 0, 0, SHADE
#define G_CC_MODULATEI TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE
#define G_CC_MODULATEIA TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0
#define G_CC_MODULATEIDECALA TEXEL0, 0, SHADE, 0, 0, 0, 0, TEXEL0
#define G_CC_MODULATEIDECALA_PRIM TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, TEXEL0
#define G_CC_MODULATEIA_PRIM TEXEL0, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0
#define G_CC_MODULATERGBA_PRIM G_CC_MODULATEIA_PRIM
#define G_CC_DECALRGB 0, 0, 0, TEXEL0, 0, 0, 0, SHADE
#define G_CC_DECALRGBA 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0
#define G_CC_BLENDI ENVIRONMENT, SHADE, TEXEL0, SHADE, 0, 0, 0, SHADE
#define G_CC_BLENDIA ENVIRONMENT, SHADE, TEXEL0, SHADE, TEXEL0, 0, SHADE, 0
#define G_CC_BLENDRGBA TEXEL0, SHADE, TEXEL0_ALPHA, SHADE, 0, 0, 0, SHADE
#define G_CC_ADDRGB 1, 0, TEXEL0, SHADE, 0, 0, 0, SHADE
#define G_CC_REFLECTRGB ENVIRONMENT, 0, TEXEL0, SHADE, 0, 0, 0, SHADE
#define G_CC_HILITERGBA PRIMITIVE, SHADE, TEXEL0, SHADE, PRIMITIVE, SHADE, TEXEL0, SHADE
#define G_CC_BLENDPE PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, SHADE, 0
#define G_CC_TEMPLERP TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0
#define G_CC_TRILERP TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0
#define G_CC_INTERFERENCE TEXEL0, 0, TEXEL1, 0, TEXEL0, 0, TEXEL1, 0
#define G_CC_1CYUV2RGB TEXEL0, K4, K5, TEXEL0, 0, 0, 0, SHADE
#define G_CC_YUV2RGB TEXEL1, K4, K5, TEXEL1, 0, 0, 0, 0
#define G_CC_PASS2 0, 0, 0, COMBINED, 0, 0, 0, COMBINED
#define G_CC_MODULATEIA2 COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0
#define G_CC_DECALRGB2 0, 0, 0, COMBINED, 0, 0, 0, SHADE
#define G_CC_DECALRGBA2 COMBINED, SHADE, COMBINED_ALPHA, SHADE, 0, 0, 0, SHADE
#define G_CC_BLENDIA2 ENVIRONMENT, SHADE, COMBINED, SHADE, COMBINED, 0, SHADE, 0
#define G_CC_CHROMA_KEY2 TEXEL0, CENTER, SCALE, 0, 0, 0, 0, 0
#define G_CC_HILITERGBA2 ENVIRONMENT, COMBINED, TEXEL0, COMBINED, ENVIRONMENT, COMBINED, TEXEL0, COMBINED

/* --------------------------------------------------- the modes PW64 uses
 * Birdman64 counted 13 distinct combiners over four traced frames (docs/notes
 * gfx.md "GBI coverage audit") without listing them; this table is every
 * combiner the decomp's code sets (n64decomp/pw64 @ 1a8558f) plus the ones
 * Birdman64 and wings64-dc name from the game's data (UVTX/UVMD lists), plus
 * the interpreter's FILL / COPY rewrites (xgfx.h). game = 1: must be exact.
 * Then gbi.h modes the game doesn't use, for coverage. */
typedef struct {
    const char* name;
    const char* src;
    uint32_t cc0, cc1;
    int game;      /* 1: PW64 (exact, stage count checked); 0: library coverage */
    int stages2;   /* expected stages in two-cycle mode (0: don't check) */
    int stages1;   /* ... in one-cycle mode */
    int approx;    /* expected `approximated`: bit 0 one-cycle, bit 1 two-cycle */
} Mode;

static const Mode k_modes[] = {
    { "G_CC_SHADE, G_CC_PASS2", "graphics.c:49/221/415, chan.c:226: untextured", MODE(G_CC_SHADE, G_CC_PASS2), 1, 1, 1, 0 },
    { "shadow pass 1: black, alpha 1; PASS2", "graphics.c:209 uvGfxStateDrawDL",
      LERPX(0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED), 1, 1, 1, 0 },
    { "G_CC_DECALRGB, G_CC_DECALRGB2", "graphics.c:322: env map (GFX_STATE_LIGHTING)",
      MODE(G_CC_DECALRGB, G_CC_DECALRGB2), 1, 1, 1, 0 },
    { "G_CC_MODULATEIDECALA, G_CC_PASS2", "graphics.c:418: fog + XLU|AA|ZBUFFER, textured",
      MODE(G_CC_MODULATEIDECALA, G_CC_PASS2), 1, 1, 1, 0 },
    { "G_CC_MODULATEIA, G_CC_PASS2", "kernel/sprite.c:16; IA blits (Birdman64 gfx.md:109)",
      MODE(G_CC_MODULATEIA, G_CC_PASS2), 1, 1, 1, 0 },
    { "G_CC_MODULATEIDECALA_PRIM x2", "libultra sp/sprite.c:489/499",
      MODE(G_CC_MODULATEIDECALA_PRIM, G_CC_MODULATEIDECALA_PRIM), 1, 1, 1, 0 },
    { "G_CC_MODULATEIA_PRIM x2 (= MODULATERGBA_PRIM)", "libultra sp/sprite.c:493/503",
      MODE(G_CC_MODULATEIA_PRIM, G_CC_MODULATERGBA_PRIM), 1, 1, 1, 0 },
    { "G_CC_SHADE, G_CC_SHADE", "libultra sp/sprite.c:739", MODE(G_CC_SHADE, G_CC_SHADE), 1, 1, 1, 0 },
    { "UVTX mip lerp (T1-T0)*LOD_FRAC+T0, then *SHADE", "0x26A004_1F1093FF: Birdman64 gfx.md:43,76 (~65% of flight)",
      0x0026A004u, 0x1F1093FFu, 1, 2, 1, 0 },
    { "T0*T1, then *SHADE (sea, thermals)", "wings64-dc gfx_f3d.c cc_is_t0_times_t1, Birdman64 renderer.md:224",
      LERPX(TEXEL0, 0, TEXEL1, 0, TEXEL0, 0, TEXEL1, 0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0), 1, 2, 1, 0 },
    { "sky ring (1-T0)*SHADE+T0; PASS2", "Birdman64 renderer.md:221 (alpha SHADE assumed)",
      LERPX(1, TEXEL0, SHADE, TEXEL0, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED), 1, 1, 1, 0 },
    { "G_CC_MODULATEI, G_CC_PASS2 (T0*SHADE)", "Birdman64 gfx.md:77 'T0*SHADE fog modes'",
      MODE(G_CC_MODULATEI, G_CC_PASS2), 1, 1, 1, 0 },
    { "G_CC_TRILERP, G_CC_MODULATEIA2", "the mip lerp as the gbi.h modes (same words as the UVTX one)",
      MODE(G_CC_TRILERP, G_CC_MODULATEIA2), 1, 2, 1, 0 },
    { "FILL rect: G_CC_PRIMITIVE", "xgfx.h: the interpreter's FILL rewrite", MODE(G_CC_PRIMITIVE, G_CC_PRIMITIVE), 1, 1, 1, 0 },
    { "COPY rect: G_CC_DECALRGBA", "xgfx.h: the interpreter's COPY rewrite", MODE(G_CC_DECALRGBA, G_CC_DECALRGBA), 1, 1, 1, 0 },
    /* library coverage */
    { "G_CC_TEMPLERP, G_CC_MODULATEIA2", "gbi.h: PRIM_LOD_FRAC", MODE(G_CC_TEMPLERP, G_CC_MODULATEIA2), 0, 2, 1, 0 },
    { "G_CC_BLENDI, G_CC_PASS2", "gbi.h", MODE(G_CC_BLENDI, G_CC_PASS2), 0, 0, 0, 0 },
    { "G_CC_BLENDIA, G_CC_BLENDIA2", "gbi.h", MODE(G_CC_BLENDIA, G_CC_BLENDIA2), 0, 0, 0, 0 },
    { "G_CC_BLENDRGBA, G_CC_PASS2", "gbi.h", MODE(G_CC_BLENDRGBA, G_CC_PASS2), 0, 0, 0, 0 },
    { "G_CC_ADDRGB, G_CC_PASS2", "gbi.h", MODE(G_CC_ADDRGB, G_CC_PASS2), 0, 0, 0, 0 },
    { "G_CC_REFLECTRGB, G_CC_PASS2", "gbi.h", MODE(G_CC_REFLECTRGB, G_CC_PASS2), 0, 0, 0, 0 },
    { "G_CC_HILITERGBA, G_CC_HILITERGBA2", "gbi.h", MODE(G_CC_HILITERGBA, G_CC_HILITERGBA2), 0, 0, 0, 0 },
    { "G_CC_BLENDPE, G_CC_PASS2", "gbi.h", MODE(G_CC_BLENDPE, G_CC_PASS2), 0, 0, 0, 0 },
    { "G_CC_MODULATEIA, G_CC_DECALRGBA2", "gbi.h", MODE(G_CC_MODULATEIA, G_CC_DECALRGBA2), 0, 0, 0, 0 },
    { "G_CC_INTERFERENCE, G_CC_PASS2", "gbi.h", MODE(G_CC_INTERFERENCE, G_CC_PASS2), 0, 0, 0, 0 },
    { "G_CC_1CYUV2RGB x2", "gbi.h: K4/K5 (approximated)", MODE(G_CC_1CYUV2RGB, G_CC_1CYUV2RGB), 0, 0, 0, 3 },
    { "G_CC_MODULATEIA, G_CC_CHROMA_KEY2", "gbi.h: CENTER/SCALE (approximated)", MODE(G_CC_MODULATEIA, G_CC_CHROMA_KEY2), 0, 0, 0, 2 },
};
#define NMODES ((int)(sizeof k_modes / sizeof k_modes[0]))

/* -------------------------------------------- NV2A register combiner model */
typedef struct { float v[16][4]; } Regs;   /* rgba per register */

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static float map_in(float x, int map) {
    switch (map) {
        case 0: return fmaxf(x, 0.0f);                       /* UNSIGNED_IDENTITY */
        case 1: return 1.0f - clampf(x, 0.0f, 1.0f);         /* UNSIGNED_INVERT */
        case 2: return 2.0f * fmaxf(x, 0.0f) - 1.0f;         /* EXPAND_NORMAL */
        case 3: return -2.0f * fmaxf(x, 0.0f) + 1.0f;        /* EXPAND_NEGATE */
        case 4: return fmaxf(x, 0.0f) - 0.5f;                /* HALF_BIAS_NORMAL */
        case 5: return -fmaxf(x, 0.0f) + 0.5f;               /* HALF_BIAS_NEGATE */
        case 6: return x;                                    /* SIGNED_IDENTITY */
        default: return -x;                                  /* SIGNED_NEGATE */
    }
}

/* one input byte of a general combiner: rgb portion reads rgb or alpha
 * (broadcast), alpha portion alpha or blue */
static void gen_in(const Regs* r, uint32_t b, int alpha_portion, float out[3]) {
    int reg = b & 0xF, sel = b >> 4 & 1, map = b >> 5 & 7, k;
    for (k = 0; k < 3; k++) {
        float x;
        if (alpha_portion) x = sel ? r->v[reg][3] : r->v[reg][2];
        else x = sel ? r->v[reg][3] : r->v[reg][k];
        out[k] = map_in(x, map);
    }
}

static int out_op(int op, float* x) {
    switch (op) {
        case 0: break;
        case 1: *x -= 0.5f; break;
        case 2: *x *= 2.0f; break;
        case 3: *x = 2.0f * (*x - 0.5f); break;
        case 4: *x *= 4.0f; break;
        case 6: *x *= 0.5f; break;
        default: return -1;
    }
    *x = clampf(*x, -1.0f, 1.0f);
    return 0;
}

typedef struct { int dst; float v[4]; int nch, ch0; } Write;

/* returns 0, or -1 for an encoding the model rejects */
static int rc_run(const CcProg* p, Regs* r, float out[4]) {
    int s, k;
    if (p->nstages < 1 || p->nstages > 8) return -1;
    for (s = 0; s < p->nstages; s++) {
        Write w[6];
        int nw = 0, i;
        /* rgb portion */
        {
            uint32_t icw = p->cicw[s], ocw = p->cocw[s];
            float a[3], b[3], c[3], d[3], ab[3], cd[3], sum[3];
            int op = ocw >> 15 & 7;
            gen_in(r, icw >> 24 & 0xFF, 0, a);
            gen_in(r, icw >> 16 & 0xFF, 0, b);
            gen_in(r, icw >> 8 & 0xFF, 0, c);
            gen_in(r, icw & 0xFF, 0, d);
            if (ocw & (1u << 14)) return -1;   /* mux: the compiler doesn't emit it */
            for (k = 0; k < 3; k++) {
                ab[k] = a[k] * b[k];
                cd[k] = c[k] * d[k];
            }
            if (ocw & (1u << 13)) ab[0] = ab[1] = ab[2] = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
            if (ocw & (1u << 12)) cd[0] = cd[1] = cd[2] = c[0] * d[0] + c[1] * d[1] + c[2] * d[2];
            for (k = 0; k < 3; k++) {
                sum[k] = ab[k] + cd[k];
                if (out_op(op, &ab[k]) || out_op(op, &cd[k]) || out_op(op, &sum[k])) return -1;
            }
            if (ocw >> 4 & 0xF) {
                w[nw].dst = ocw >> 4 & 0xF; w[nw].nch = 3; w[nw].ch0 = 0;
                memcpy(w[nw].v, ab, sizeof ab);
                nw++;
                if (ocw & (1u << 19)) { w[nw].dst = ocw >> 4 & 0xF; w[nw].nch = 1; w[nw].ch0 = 3; w[nw].v[3] = ab[2]; nw++; }
            }
            if (ocw & 0xF) {
                w[nw].dst = ocw & 0xF; w[nw].nch = 3; w[nw].ch0 = 0;
                memcpy(w[nw].v, cd, sizeof cd);
                nw++;
                if (ocw & (1u << 18)) { w[nw].dst = ocw & 0xF; w[nw].nch = 1; w[nw].ch0 = 3; w[nw].v[3] = cd[2]; nw++; }
            }
            if (ocw >> 8 & 0xF) {
                w[nw].dst = ocw >> 8 & 0xF; w[nw].nch = 3; w[nw].ch0 = 0;
                memcpy(w[nw].v, sum, sizeof sum);
                nw++;
            }
        }
        /* alpha portion */
        {
            uint32_t icw = p->aicw[s], ocw = p->aocw[s];
            float a[3], b[3], c[3], d[3], ab, cd, sum;
            int op = ocw >> 15 & 7;
            gen_in(r, icw >> 24 & 0xFF, 1, a);
            gen_in(r, icw >> 16 & 0xFF, 1, b);
            gen_in(r, icw >> 8 & 0xFF, 1, c);
            gen_in(r, icw & 0xFF, 1, d);
            if (ocw & (7u << 12)) return -1;   /* mux / no dot products in alpha */
            ab = a[0] * b[0];
            cd = c[0] * d[0];
            sum = ab + cd;
            if (out_op(op, &ab) || out_op(op, &cd) || out_op(op, &sum)) return -1;
            if (ocw >> 4 & 0xF) { w[nw].dst = ocw >> 4 & 0xF; w[nw].nch = 1; w[nw].ch0 = 3; w[nw].v[3] = ab; nw++; }
            if (ocw & 0xF) { w[nw].dst = ocw & 0xF; w[nw].nch = 1; w[nw].ch0 = 3; w[nw].v[3] = cd; nw++; }
            if (ocw >> 8 & 0xF) { w[nw].dst = ocw >> 8 & 0xF; w[nw].nch = 1; w[nw].ch0 = 3; w[nw].v[3] = sum; nw++; }
        }
        /* writes after all reads; only R0, R1, T0..T3, V0, V1 are writable */
        for (i = 0; i < nw; i++) {
            int d = w[i].dst;
            if (d == 0) continue;
            if (d < 4 || d == 6 || d == 7 || d > 13) return -1;
            for (k = w[i].ch0; k < w[i].ch0 + w[i].nch; k++) r->v[d][k] = w[i].v[k];
        }
    }
    /* final combiner: inputs unsigned [0, 1]; 0xE = V1 + R0, 0xF = E * F */
    {
        float e[3], f[3], ef[4], vin[4][3], g;
        uint32_t cw0 = p->cw0, cw1 = p->cw1;
        int i;
        for (i = 0; i < 2; i++) {
            uint32_t b = cw1 >> (24 - 8 * i) & 0xFF;
            int reg = b & 0xF, sel = b >> 4 & 1, inv = b >> 5 & 1;
            if (reg >= 14) return -1;
            for (k = 0; k < 3; k++) {
                float x = clampf(sel ? r->v[reg][3] : r->v[reg][k], 0.0f, 1.0f);
                (i ? f : e)[k] = inv ? 1.0f - x : x;
            }
        }
        for (k = 0; k < 3; k++) ef[k] = e[k] * f[k];
        ef[3] = 0.0f;
        for (i = 0; i < 4; i++) {
            uint32_t b = cw0 >> (24 - 8 * i) & 0xFF;
            int reg = b & 0xF, sel = b >> 4 & 1, inv = b >> 5 & 1;
            for (k = 0; k < 3; k++) {
                float x;
                if (reg == 15) x = sel ? 0.0f : ef[k];
                else if (reg == 14) x = clampf(r->v[5][sel ? 3 : k], 0, 1) + clampf(r->v[12][sel ? 3 : k], 0, 1);
                else x = sel ? r->v[reg][3] : r->v[reg][k];
                x = clampf(x, 0.0f, 1.0f);
                vin[i][k] = inv ? 1.0f - x : x;
            }
        }
        for (k = 0; k < 3; k++) out[k] = clampf(vin[0][k] * vin[1][k] + (1.0f - vin[0][k]) * vin[2][k] + vin[3][k], 0, 1);
        {
            uint32_t b = cw1 >> 8 & 0xFF;
            int reg = b & 0xF, sel = b >> 4 & 1, inv = b >> 5 & 1;
            if (reg >= 14) return -1;
            g = clampf(sel ? r->v[reg][3] : r->v[reg][2], 0.0f, 1.0f);
            out[3] = inv ? 1.0f - g : g;
        }
    }
    return 0;
}

/* the registers as the back end loads them (nv2a_cc.h) */
static void load_regs(Regs* r, const CcRefIn* in, const uint8_t fogc[3], uint8_t fogf) {
    int k;
    memset(r, 0, sizeof *r);
    for (k = 0; k < 4; k++) {
        r->v[1][k] = in->prim[k] / 255.0f;
        r->v[2][k] = in->env[k] / 255.0f;
        r->v[4][k] = in->shade[k] / 255.0f;
        r->v[8][k] = in->t0[k] / 255.0f;
        r->v[9][k] = in->t1[k] / 255.0f;
        r->v[5][k] = (k < 3 ? in->lod_frac : in->prim_lod_frac) / 255.0f;
        r->v[3][k] = (k < 3 ? fogc[k] : fogf) / 255.0f;
    }
    r->v[12][3] = r->v[8][3];   /* spare0.a starts as texture 0's alpha */
}

static void rand_in(CcRefIn* in) {
    int k;
    for (k = 0; k < 4; k++) {
        in->t0[k] = rbyte();
        in->t1[k] = rbyte();
        in->shade[k] = rbyte();
        in->prim[k] = rbyte();
        in->env[k] = rbyte();
    }
    in->lod_frac = rbyte();
    in->prim_lod_frac = rbyte();
    in->k4 = rbyte();
    in->k5 = rbyte();
    for (k = 0; k < 3; k++) {
        in->center[k] = rbyte();
        in->scale[k] = rbyte();
    }
    in->noise = rbyte();
}

/* one comparison: returns the largest channel error against the RDP model
 * (-1 when the register-combiner model rejected the program); *ierr: the
 * largest error against exact arithmetic (/255), *range set when the RDP's
 * result was CCREF_RANGE */
static int compare(const CcProg* p, uint32_t cc0, uint32_t cc1, int two, int fog, const CcRefIn* in, int* range,
                   double* ierr) {
    uint8_t ref[4], fogc[3] = { rbyte(), rbyte(), rbyte() }, fogf = rbyte();
    double ideal[4];
    Regs r;
    float o[4];
    int k, err = 0;
    *range = (cc_ref_eval(cc0, cc1, two, in, ref) & CCREF_RANGE) != 0;
    cc_ideal_eval(cc0, cc1, two, in, ideal);
    if (fog) {   /* the blender's fog lerp (G_RM_FOG_SHADE_A) at full precision */
        for (k = 0; k < 3; k++) {
            ref[k] = (uint8_t)((ref[k] * (255 - fogf) + fogc[k] * fogf + 127) / 255);
            ideal[k] += (fogc[k] / 255.0 - ideal[k]) * (fogf / 255.0);
        }
    }
    load_regs(&r, in, fogc, fogf);
    if (rc_run(p, &r, o)) return -1;
    *ierr = 0.0;
    for (k = 0; k < 4; k++) {
        int v = (int)lrintf(o[k] * 255.0f), d = abs(v - ref[k]);
        double di = fabs(o[k] - ideal[k]) * 255.0;
        if (d > err) err = d;
        if (di > *ierr) *ierr = di;
    }
    return err;
}

static const char* why_name(uint32_t bit) {
    switch (bit) {
        case CC_APPROX_NOISE: return "NOISE";
        case CC_APPROX_K4: return "K4";
        case CC_APPROX_K5: return "K5";
        case CC_APPROX_KEY: return "CENTER/SCALE";
        case CC_APPROX_COMBINED0: return "COMBINED in the first cycle";
    }
    return "?";
}

/* --------------------------------------------------------- random words */
/* is field value v of slot (0 a, 1 b, 2 c, 3 d) COMBINED / COMBINED_ALPHA? */
static int is_comb_rgb(int slot, int v) { return v == 0 || (slot == 2 && v == 7); }
static int is_comb_a(int slot, int v) { return v == 0 && slot != 2; }

static void rand_cycle(int rgb[4], int a[4], int first) {
    static const int bits_rgb[4] = { 4, 4, 5, 3 };
    int k;
    for (k = 0; k < 4; k++) {
        do rgb[k] = (int)(rnd() % (1u << bits_rgb[k]));
        while (first && is_comb_rgb(k, rgb[k]) && rnd() % 16);
        do a[k] = (int)(rnd() % 8);
        while (first && is_comb_a(k, a[k]) && rnd() % 16);
    }
    /* make the c inputs that make a cycle trivial (c = 0) rarer */
    if (rgb[2] >= 16 && rnd() % 4) rgb[2] = (int)(rnd() % 16);
}

static void encode(const int rgb0[4], const int a0[4], const int rgb1[4], const int a1[4], uint32_t* w0, uint32_t* w1) {
    *w0 = W0(rgb0[0], rgb0[2], a0[0], a0[2], rgb1[0], rgb1[2]);
    *w1 = W1(rgb0[1], rgb0[3], a0[1], a0[3], rgb1[1], a1[0], a1[2], rgb1[3], a1[1], a1[3]);
}

/* error bookkeeping for a set of samples */
typedef struct {
    long n, over_tol;      /* samples compared; with |RC - RDP| > TOL */
    int max_rdp;           /* max |RC - RDP| (/255) */
    double max_ideal;      /* max |RC - exact| (/255) */
} Err;

static void err_add(Err* e, int rdp, double ideal) {
    e->n++;
    if (rdp > TOL) e->over_tol++;
    if (rdp > e->max_rdp) e->max_rdp = rdp;
    if (ideal > e->max_ideal) e->max_ideal = ideal;
}

int main(int argc, char** argv) {
    int nwords = argc > 1 ? atoi(argv[1]) : 2000, nin = argc > 2 ? atoi(argv[2]) : 200;
    int fails = 0, i, j, two, fog;

    /* ---- the mode table ---- */
    printf("PW64 combiner modes: stages 2-cycle / 1-cycle; max |RC - RDP| and |RC - exact| (/255) over %d inputs,\n"
           "1- and 2-cycle, fog off and on:\n", nin * 10);
    for (i = 0; i < NMODES; i++) {
        const Mode* m = &k_modes[i];
        int st[2] = { 0, 0 }, nrange = 0, appr = 0;
        Err e;
        memset(&e, 0, sizeof e);
        if (i > 0 && m->game == 0 && k_modes[i - 1].game == 1) printf("gbi.h modes PW64 doesn't use (coverage):\n");
        for (two = 1; two >= 0; two--) {
            int want = two ? m->stages2 : m->stages1, ap = m->approx >> two & 1;
            for (fog = 0; fog < 2; fog++) {
                CcProg p;
                cc_compile(m->cc0, m->cc1, two, fog, &p);
                if (!fog) {
                    st[two] = p.nstages;
                    appr |= p.approximated;
                    if (p.approximated != ap) {
                        printf("  FAIL %s (%s): approximated %d (why %#x)\n", m->name, two ? "2-cycle" : "1-cycle",
                               p.approximated, p.approx_why);
                        fails++;
                    }
                    if (want && p.nstages != want) {
                        printf("  FAIL %s (%s): %d stages, expected %d\n", m->name, two ? "2-cycle" : "1-cycle",
                               p.nstages, want);
                        fails++;
                    }
                }
                if (!!(p.factor_use & CC_USE_FOG) != fog) {
                    printf("  FAIL %s: factor_use %#x with fog %d\n", m->name, p.factor_use, fog);
                    fails++;
                }
                for (j = 0; j < nin * 5 / 2; j++) {
                    CcRefIn in;
                    int range, err;
                    double ierr;
                    rand_in(&in);
                    err = compare(&p, m->cc0, m->cc1, two, fog, &in, &range, &ierr);
                    if (err < 0) {
                        printf("  FAIL %s: the register-combiner model rejects the program\n", m->name);
                        fails++;
                        break;
                    }
                    if (range) { nrange++; continue; }
                    if (ap) continue;
                    err_add(&e, err, ierr);
                    if (err > (m->game || !two ? TOL : TOL2) || ierr > ITOL) {
                        if (fails < 40)
                            printf("  FAIL %s (%s, fog %d): |RC - RDP| %d, |RC - exact| %.3f\n", m->name,
                                   two ? "2-cycle" : "1-cycle", fog, err, ierr);
                        fails++;
                    }
                }
            }
        }
        printf("  %-46s %06X_%08X  %d / %d  ", m->name, m->cc0, m->cc1, st[1], st[0]);
        if (appr) printf("APPROXIMATED       ");
        else printf("err %d  exact %.3f  ", e.max_rdp, e.max_ideal);
        printf("[%s]%s\n", m->src, nrange ? " (samples on the RDP's 9-bit wrap skipped)" : "");
        if (m->game && nrange) {
            printf("  FAIL %s: %d samples depend on the RDP's 9-bit wrap\n", m->name, nrange);
            fails++;
        }
    }

    /* ---- random words ---- */
    {
        int nexact = 0, napprox = 0, why_count[8] = { 0 }, bad_words = 0, stage_hist[9] = { 0 };
        long ranged = 0, approx_samples = 0;
        int approx_max = 0;
        Err e1, e2;
        memset(&e1, 0, sizeof e1);
        memset(&e2, 0, sizeof e2);
        for (i = 0; i < nwords; i++) {
            int rgb0[4], a0[4], rgb1[4], a1[4], word_bad = 0;
            uint32_t w0, w1;
            CcProg p;
            two = i & 1;
            fog = (i >> 1) & 1;
            rand_cycle(rgb0, a0, 1);
            rand_cycle(rgb1, a1, 0);
            encode(rgb0, a0, rgb1, a1, &w0, &w1);
            cc_compile(w0, w1, two, fog, &p);
            if (p.nstages < 1 || p.nstages > (two ? 4 : 2)) {
                printf("  FAIL %06X_%08X %d-cycle: %d stages\n", w0, w1, two + 1, p.nstages);
                fails++;
            }
            stage_hist[p.nstages & 7]++;
            if (p.approximated) {
                int b;
                napprox++;
                for (b = 0; b < 8; b++)
                    if (p.approx_why & (1u << b)) why_count[b]++;
            } else {
                nexact++;
            }
            for (j = 0; j < nin; j++) {
                CcRefIn in;
                int range, err;
                double ierr;
                rand_in(&in);
                err = compare(&p, w0, w1, two, fog, &in, &range, &ierr);
                if (err < 0) {
                    printf("  FAIL %06X_%08X %d-cycle: the register-combiner model rejects the program\n", w0, w1, two + 1);
                    fails++;
                    break;
                }
                if (range) { ranged++; continue; }
                if (p.approximated) {
                    approx_samples++;
                    if (err > approx_max) approx_max = err;
                    continue;
                }
                err_add(two ? &e2 : &e1, err, ierr);
                if (err > (two ? TOL2 : TOL) || ierr > ITOL) {
                    if (!word_bad && bad_words < 20)
                        printf("  FAIL %06X_%08X %d-cycle fog %d: |RC - RDP| %d, |RC - exact| %.3f "
                               "(rgb %d %d %d %d / a %d %d %d %d | rgb %d %d %d %d / a %d %d %d %d)\n",
                               w0, w1, two + 1, fog, err, ierr, rgb0[0], rgb0[1], rgb0[2], rgb0[3], a0[0], a0[1], a0[2],
                               a0[3], rgb1[0], rgb1[1], rgb1[2], rgb1[3], a1[0], a1[1], a1[2], a1[3]);
                    word_bad = 1;
                    fails++;
                }
            }
            bad_words += word_bad;
        }
        printf("random words: %d (half 1-cycle, half 2-cycle; fog off/on), %d inputs each\n", nwords, nin);
        printf("  exact %d, approximated %d, words failing %d\n", nexact, napprox, bad_words);
        printf("  exact, 1-cycle: %ld samples, max |RC - RDP| %d (> %d: %ld), max |RC - exact| %.3f\n", e1.n, e1.max_rdp,
               TOL, e1.over_tol, e1.max_ideal);
        printf("  exact, 2-cycle: %ld samples, max |RC - RDP| %d (> %d: %ld, allowed up to %d), max |RC - exact| %.3f\n",
               e2.n, e2.max_rdp, TOL, e2.over_tol, TOL2, e2.max_ideal);
        printf("  approximated: %ld samples, max |RC - RDP| %d; causes:\n", approx_samples, approx_max);
        for (i = 0; i < 8; i++)
            if (why_count[i]) printf("    %-28s %d words\n", why_name(1u << i), why_count[i]);
        printf("  skipped %ld samples whose RDP result depends on its 9-bit wrap (CCREF_RANGE)\n", ranged);
        printf("  stages:");
        for (i = 1; i <= 8; i++)
            if (stage_hist[i]) printf("  %d: %d words", i, stage_hist[i]);
        printf("\n");
    }

    printf(fails ? "test_cc: FAIL (%d)\n" : "test_cc: OK\n", fails);
    return fails ? 1 : 0;
}
