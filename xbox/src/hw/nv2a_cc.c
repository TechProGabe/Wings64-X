/* nv2a_cc.c - the N64 colour combiner -> NV2A register combiners (PLAN.md D4
 * "Combiner"). The register encodings (input bytes, ICW/OCW, the final
 * combiner's CW0/CW1, the fog lerp) are Melee-X's xbox/src/hw/nv2a_rc.c and
 * nv2a_fog.c (proven on hardware), relicensed MIT by their author for this
 * port (PLAN.md §7); the compiler itself is new.
 *
 * The RDP's combiner computes per cycle, for rgb and alpha separately,
 *     out = (A - B) * C + D
 * with the inputs picked by the G_SETCOMBINE fields (gbi.h G_CCMUX_* and
 * G_ACMUX_*; field positions GCCc0w0 / GCCc1w0 / GCCc0w1 / GCCc1w1). In
 * two-cycle mode the second cycle reads the first's output as COMBINED (and
 * COMBINED_ALPHA). An NV2A general combiner stage computes, per portion
 * (rgb, alpha), AB + CD with per-input mappings (identity, 1 - x, -x, ...)
 * and writes the sum to a register; the final combiner computes
 * A*B + (1-A)*C + D (rgb) and G (alpha) and clamps to [0, 1].
 *
 * Registers (nv2a_cc.h has what the back end loads into them):
 *   T0, T1   TEXEL0, TEXEL1         V0   SHADE
 *   C0, C1   PRIM, ENV              V1   (LOD_FRACTION x3, PRIM_LOD_FRAC)
 *   R0       COMBINED: each cycle's result; the final combiner reads it
 *   R1       scratch: (A - B) * C of the two-stage form
 *   FOG      final combiner only: fog colour, fog amount in alpha
 *
 * One portion of one cycle becomes one stage when its formula has at most
 * two products, which covers what games use:
 *     C = 0 or A = B         D * 1                     (0 stages if D is COMBINED: R0 already holds it)
 *     B = 0                  A * C + D * 1
 *     A = 0                  (-B) * C + D * 1
 *     A = 1                  (1 - B) * C + D * 1       (UNSIGNED_INVERT on B)
 *     D = B                  A * C + B * (1 - C)       (a lerp: UNSIGNED_INVERT on C)
 *     D = 0                  A * C + (-B) * C
 * and two stages otherwise: R1 = A * C + (-B) * C, then R0 = R1 * 1 + D * 1.
 * The rgb and alpha portions of a cycle share stages (the shorter one runs in
 * the last of them; either order is safe, since the second stage of the
 * two-stage form reads only R1 and D, and a stage reads all its inputs before
 * it writes). Two cycles are at most four stages.
 * Two shortcuts:
 *   - a second cycle that doesn't read COMBINED makes the first one dead
 *     (gsDPSetCombineMode(X, X) in two-cycle mode): only the second is compiled;
 *   - a first cycle whose rgb and alpha are each a single input (C = 0 or
 *     A = B: G_CC_SHADE, G_CC_DECALRGB, ...) is substituted into the second
 *     (G_CC_PASS2, G_CC_DECALRGB2, ...), saving its stage.
 * COMBINED (R0, R1) is read with the signed mappings: a cycle's result can be
 * negative (A < B) and the RDP keeps it signed between cycles. The
 * UNSIGNED_INVERT shortcuts are taken only for inputs that are never negative,
 * which includes a first-cycle result whose formula can't go negative
 * (G_CC_BLENDIA2's lerp by COMBINED after G_CC_MODULATEIA: one stage).
 *
 * Fog (PLAN.md D4 "Fog"; Melee-X fog_final_cw0): without fog the final
 * combiner outputs rgb = R0.rgb (A = B = D = 0, C = R0), alpha = R0.a; with
 * fog rgb = FOG.a * FOG.rgb + (1 - FOG.a) * R0.rgb. On the N64 this lerp is
 * the blender's first cycle (G_RM_FOG_SHADE_A) with 5-bit blend alpha; the
 * NV2A does it at full precision. DIVERGES: fog lerp precision.
 *
 * Where the arithmetic differs (tests/xbox/cc_ref.c models the RDP):
 *   - DIVERGES: the RDP multiplies by C/256 (255 is 0.996, the constant 1 is
 *     0x100) and rounds after each cycle; the NV2A treats 255 as 1.0. Up to
 *     1-2/255 per cycle, measured by test_cc.
 *   - DIVERGES: the RDP's cycle results are 9-bit signed (-128..383 as
 *     A/B/D, -256..255 as C) and wrap beyond that; the NV2A clamps register
 *     writes to [-1, 1] (-255..255). A first cycle above 1.0 (only with
 *     A = 1 or D = 1 sums) or a final sum past 383 (wraps to black on the N64)
 *     come out clamped here. Games don't rely on the wrap.
 *   - DIVERGES: a first cycle that outputs exactly 1.0 (0x100) read by the
 *     second as C is -1.0 on the RDP (9-bit sign extension), 1.0 here.
 *   - The hardware's second cycle reads TEXEL0/TEXEL1 one texel ahead (TEXEL0
 *     is the tile+1 texel, TEXEL1 the next pixel's TEXEL0: angrylion, Birdman64
 *     combiner.rs). This compiler takes the gbi.h meaning in both cycles; the
 *     interpreter binds the units (none of PW64's modes reads a texel in the
 *     second cycle).
 *   - One-cycle mode compiles cycle 0's fields (xgfx.h); the RDP runs cycle
 *     1's. gsDPSetCombineMode(X, X) makes them equal; the interpreter sends
 *     the fields that run.
 *   - COMBINED in the first cycle (the previous pixel's result), NOISE, K4,
 *     K5 and the chroma-key CENTER/SCALE: approximated, flagged in
 *     CcProg.approximated / approx_why (nv2a_cc.h).
 */
#include <string.h>

#include "nv2a_cc.h"

/* combiner input registers (Melee-X nv2a_rc.c S_*; FOG is FC_FOG in nv2a_fog.c) */
enum { S_ZERO = 0, S_C0 = 1, S_C1 = 2, S_FOG = 3, S_V0 = 4, S_V1 = 5, S_T0 = 8, S_T1 = 9, S_R0 = 12, S_R1 = 13 };
/* input mappings: unsigned identity, 1 - x, 2x - 1, -2x + 1, x - 1/2, -x + 1/2, signed x, -x */
enum { M_UID = 0, M_UINV = 1, M_EXPN = 2, M_EXPNEG = 3, M_HBN = 4, M_HBNEG = 5, M_SID = 6, M_SNEG = 7 };
enum { OP_NOSHIFT = 0 };

/* what an operand is */
enum { K_CONST = 0,   /* the ZERO register through a mapping: 0, 1, -1, 1/2 */
       K_IN = 1,      /* a texture, colour or constant input, or a first-cycle result that is never negative */
       K_COMB = 2 };  /* R0 / R1: a cycle's result, signed */
/* which channel of the register: rgb (rgb portion only), alpha (broadcast), blue (alpha portion only) */
enum { CH_RGB = 0, CH_A = 1, CH_B = 2 };

typedef struct {
    uint8_t src, ch, map, kind;
    uint8_t why;   /* CC_APPROX_* when this operand stands in for an input there is no register for */
} Op;

static const Op OP_ZERO = { S_ZERO, CH_RGB, M_UID, K_CONST, 0 };
static const Op OP_ONE = { S_ZERO, CH_RGB, M_UINV, K_CONST, 0 };
static const Op OP_HALF = { S_ZERO, CH_RGB, M_HBNEG, K_CONST, 0 };

static int is_zero(Op o) { return o.src == S_ZERO && (o.map == M_UID || o.map == M_SID || o.map == M_SNEG); }
static int is_one(Op o) { return o.src == S_ZERO && o.map == M_UINV; }
static int same(Op a, Op b) {
    if (a.why != b.why) return 0;   /* an input with no register is not the constant standing in for it */
    if (a.src == S_ZERO || b.src == S_ZERO) {
        if (is_zero(a) && is_zero(b)) return 1;
        return a.src == b.src && a.map == b.map;
    }
    return a.src == b.src && a.ch == b.ch && a.map == b.map;
}

static Op op_reg(uint8_t src, uint8_t ch) {
    Op o = { src, ch, M_UID, K_IN, 0 };
    return o;
}

static Op op_approx(Op o, uint8_t why) {
    o.why = why;
    return o;
}

/* -x */
static Op op_neg(Op o) {
    if (o.src == S_ZERO) {
        if (o.map == M_UINV) o.map = M_EXPN;          /* -1 = 2*0 - 1 */
        else if (o.map == M_HBNEG) o.map = M_HBN;     /* -1/2 = 0 - 1/2 */
        else if (o.map == M_EXPN) o.map = M_UINV;
        else if (o.map == M_HBN) o.map = M_HBNEG;
        return o;
    }
    o.map = M_SNEG;
    return o;
}

/* 1 - x, for x in [0, 1] (K_IN or a 0/1 constant) */
static Op op_inv(Op o) {
    if (o.src == S_ZERO) {
        if (is_zero(o)) return op_approx(OP_ONE, o.why);
        if (o.map == M_UINV) return op_approx(OP_ZERO, o.why);
        if (o.map == M_HBNEG) return o;   /* 1 - 1/2 */
    }
    o.map = M_UINV;
    return o;
}

/* ---- the G_SETCOMBINE fields ---- */
typedef struct { uint8_t rgb[4], a[4]; } Cyc;   /* a b c d, Aa Ab Ac Ad as stored */

static void decode(uint32_t w0, uint32_t w1, Cyc c[2]) {
    c[0].rgb[0] = (uint8_t)(w0 >> 20 & 0xF);
    c[0].rgb[1] = (uint8_t)(w1 >> 28 & 0xF);
    c[0].rgb[2] = (uint8_t)(w0 >> 15 & 0x1F);
    c[0].rgb[3] = (uint8_t)(w1 >> 15 & 0x7);
    c[0].a[0] = (uint8_t)(w0 >> 12 & 0x7);
    c[0].a[1] = (uint8_t)(w1 >> 12 & 0x7);
    c[0].a[2] = (uint8_t)(w0 >> 9 & 0x7);
    c[0].a[3] = (uint8_t)(w1 >> 9 & 0x7);
    c[1].rgb[0] = (uint8_t)(w0 >> 5 & 0xF);
    c[1].rgb[1] = (uint8_t)(w1 >> 24 & 0xF);
    c[1].rgb[2] = (uint8_t)(w0 >> 0 & 0x1F);
    c[1].rgb[3] = (uint8_t)(w1 >> 6 & 0x7);
    c[1].a[0] = (uint8_t)(w1 >> 21 & 0x7);
    c[1].a[1] = (uint8_t)(w1 >> 3 & 0x7);
    c[1].a[2] = (uint8_t)(w1 >> 18 & 0x7);
    c[1].a[3] = (uint8_t)(w1 >> 0 & 0x7);
}

/* what COMBINED / COMBINED_ALPHA read in a cycle */
typedef struct {
    Op rgb;     /* COMBINED in the rgb portion */
    Op a_rgb;   /* COMBINED_ALPHA in the rgb portion (rgb c) */
    Op a;       /* COMBINED in the alpha portion */
} Comb;

static Op in_common(int v, int alpha, const Comb* cb) {
    uint8_t ch = alpha ? CH_A : CH_RGB;
    switch (v) {
        case 0: return alpha ? cb->a : cb->rgb;
        case 1: return op_reg(S_T0, ch);
        case 2: return op_reg(S_T1, ch);
        case 3: return op_reg(S_C0, ch);
        case 4: return op_reg(S_V0, ch);
        case 5: return op_reg(S_C1, ch);
        default: return OP_ZERO;
    }
}

/* G_CCMUX_*: slot 0 a, 1 b, 2 c, 3 d */
static Op rgb_in(int slot, int v, const Comb* cb) {
    if (v <= 5) return in_common(v, 0, cb);
    switch (slot) {
        case 0:
            if (v == 6) return OP_ONE;
            if (v == 7) return op_approx(OP_HALF, CC_APPROX_NOISE);   /* NOISE */
            return OP_ZERO;
        case 1:
            if (v == 6) return op_approx(OP_ZERO, CC_APPROX_KEY);     /* CENTER */
            if (v == 7) return op_approx(OP_ZERO, CC_APPROX_K4);
            return OP_ZERO;
        case 2:
            switch (v) {
                case 6: return op_approx(OP_ZERO, CC_APPROX_KEY);     /* SCALE */
                case 7: return cb->a_rgb;                             /* COMBINED_ALPHA */
                case 8: return op_reg(S_T0, CH_A);
                case 9: return op_reg(S_T1, CH_A);
                case 10: return op_reg(S_C0, CH_A);
                case 11: return op_reg(S_V0, CH_A);
                case 12: return op_reg(S_C1, CH_A);
                case 13: return op_reg(S_V1, CH_RGB);                 /* LOD_FRACTION (V1.rgb) */
                case 14: return op_reg(S_V1, CH_A);                   /* PRIM_LOD_FRAC (V1.a) */
                case 15: return op_approx(OP_ZERO, CC_APPROX_K5);
                default: return OP_ZERO;
            }
        default:
            return v == 6 ? OP_ONE : OP_ZERO;
    }
}

/* G_ACMUX_* */
static Op alpha_in(int slot, int v, const Comb* cb) {
    if (slot == 2) {
        if (v == 0) return op_reg(S_V1, CH_B);   /* LOD_FRACTION (V1.b) */
        if (v == 6) return op_reg(S_V1, CH_A);   /* PRIM_LOD_FRAC */
        if (v == 7) return OP_ZERO;
        return in_common(v, 1, cb);
    }
    if (v == 6) return OP_ONE;
    if (v == 7) return OP_ZERO;
    return in_common(v, 1, cb);
}

/* ---- one portion of one cycle ---- */
typedef struct { Op in[4]; uint8_t dst; } Half;   /* in: A B C D of AB + CD */
typedef struct {
    int n;          /* stages: 0 (COMBINED passed through), 1, 2 */
    Half h[2];
    int nonneg;     /* the result is never negative */
} Plan;

static void put(Half* h, Op a, Op b, Op c, Op d, uint8_t dst) {
    h->in[0] = a;
    h->in[1] = b;
    h->in[2] = c;
    h->in[3] = d;
    h->dst = dst;
}

/* the operands (A - B) * C + D actually depends on */
static void live(const Op o[4], int live_out[4]) {
    int all = !is_zero(o[2]) && !same(o[0], o[1]);
    live_out[0] = live_out[1] = all;
    live_out[2] = all || o[2].why;
    live_out[3] = 1;
}

static uint32_t why_of(const Op o[4]) {
    int l[4], k;
    uint32_t w = 0;
    live(o, l);
    for (k = 0; k < 4; k++)
        if (l[k]) w |= o[k].why;
    return w;
}

static int reads_comb(const Op o[4]) {
    int l[4], k;
    live(o, l);
    for (k = 0; k < 4; k++)
        if (l[k] && o[k].kind == K_COMB) return 1;
    return 0;
}

/* a single input: C = 0 or A = B leaves D */
static int single(const Op o[4]) { return is_zero(o[2]) || same(o[0], o[1]); }

/* never negative: an input, 0, 1/2, 1 */
static int nonneg(Op o) {
    if (o.kind == K_IN) return 1;
    return o.kind == K_CONST && (o.map == M_UID || o.map == M_UINV || o.map == M_HBNEG);
}

static void plan(const Op o[4], int alpha_portion, Plan* p) {
    Op a = o[0], b = o[1], c = o[2], d = o[3];
    memset(p, 0, sizeof *p);
    p->n = 1;
    if (single(o)) {
        if (d.src == S_R0) { p->n = 0; return; }   /* COMBINED: R0 has it */
        put(&p->h[0], d, OP_ONE, OP_ZERO, OP_ZERO, S_R0);
        p->nonneg = nonneg(d);
    } else if (is_zero(b)) {
        put(&p->h[0], a, c, d, is_zero(d) ? OP_ZERO : OP_ONE, S_R0);
        p->nonneg = nonneg(a) && nonneg(c) && nonneg(d);
    } else if (is_zero(a)) {
        put(&p->h[0], op_neg(b), c, d, is_zero(d) ? OP_ZERO : OP_ONE, S_R0);
    } else if (is_one(a) && b.kind == K_IN) {
        put(&p->h[0], op_inv(b), c, d, is_zero(d) ? OP_ZERO : OP_ONE, S_R0);
        p->nonneg = nonneg(c) && nonneg(d);
    } else if (same(d, b) && nonneg(c)) {
        put(&p->h[0], a, c, b, op_inv(c), S_R0);
        p->nonneg = nonneg(a) && nonneg(b);
    } else if (is_zero(d)) {
        put(&p->h[0], a, c, op_neg(b), c, S_R0);
    } else {
        Op r1 = { S_R1, CH_RGB, M_SID, K_COMB, 0 };
        if (alpha_portion) r1.ch = CH_A;
        p->n = 2;
        put(&p->h[0], a, c, op_neg(b), c, S_R1);
        put(&p->h[1], r1, OP_ONE, d, OP_ONE, S_R0);
    }
}

/* ---- encoding (Melee-X nv2a_rc.c enc_in / make_icw / make_ocw) ---- */
static uint32_t enc_in(Op o, int alpha_portion) {
    uint32_t a = alpha_portion ? (o.ch != CH_B) : (o.ch == CH_A);
    return (uint32_t)(o.src & 0xF) | a << 4 | (uint32_t)(o.map & 7) << 5;
}

static uint32_t make_icw(const Op in[4], int alpha_portion) {
    return enc_in(in[0], alpha_portion) << 24 | enc_in(in[1], alpha_portion) << 16 |
           enc_in(in[2], alpha_portion) << 8 | enc_in(in[3], alpha_portion);
}

/* the AB + CD sum to dst (0: discarded) */
static uint32_t make_ocw(uint8_t dst, uint8_t op) { return (uint32_t)(dst & 0xF) << 8 | (uint32_t)(op & 7) << 15; }

/* ---- a cycle ---- */
typedef struct { Op rgb[4], a[4]; } Ops;

static void cycle_ops(const Cyc* c, const Comb* cb, Ops* o) {
    int k;
    for (k = 0; k < 4; k++) {
        o->rgb[k] = rgb_in(k, c->rgb[k], cb);
        o->a[k] = alpha_in(k, c->a[k], cb);
    }
}

/* returns bit 0: the rgb result is never negative, bit 1: the alpha result */
static int emit_cycle(const Ops* o, CcProg* out) {
    Plan pc, pa;
    int groups, k;
    plan(o->rgb, 0, &pc);
    plan(o->a, 1, &pa);
    out->approx_why |= why_of(o->rgb) | why_of(o->a);
    groups = pc.n > pa.n ? pc.n : pa.n;
    for (k = 0; k < groups && out->nstages < CC_MAX_STAGES; k++) {
        int ci = k - (groups - pc.n), ai = k - (groups - pa.n), n = out->nstages++;
        static const Op z[4] = { { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } };
        out->cicw[n] = make_icw(ci >= 0 ? pc.h[ci].in : z, 0);
        out->cocw[n] = ci >= 0 ? make_ocw(pc.h[ci].dst, OP_NOSHIFT) : 0;
        out->aicw[n] = make_icw(ai >= 0 ? pa.h[ai].in : z, 1);
        out->aocw[n] = ai >= 0 ? make_ocw(pa.h[ai].dst, OP_NOSHIFT) : 0;
    }
    return pc.nonneg | pa.nonneg << 1;
}

static uint32_t use_of_byte(uint32_t b) {
    switch (b & 0xF) {
        case S_T0: return CC_USE_T0;
        case S_T1: return CC_USE_T1;
        case S_V0: return CC_USE_SHADE;
        case S_C0: return CC_USE_PRIM;
        case S_C1: return CC_USE_ENV;
        case S_V1: return CC_USE_V1;
        default: return 0;
    }
}

void cc_compile(uint32_t cc0, uint32_t cc1, int two_cycle, int fog, CcProg* out) {
    Cyc cyc[2];
    Ops o0, o1;
    /* the first cycle compiled: COMBINED is the previous pixel's, approximated as 0 */
    const Op z0 = op_approx(OP_ZERO, CC_APPROX_COMBINED0);
    const Comb first = { z0, z0, z0 };
    const Op r0 = { S_R0, CH_RGB, M_SID, K_COMB, 0 }, r0a = { S_R0, CH_A, M_SID, K_COMB, 0 };
    const Comb second = { r0, r0a, r0a };
    int i;

    memset(out, 0, sizeof *out);
    decode(cc0, cc1, cyc);
    if (!two_cycle) {
        cycle_ops(&cyc[0], &first, &o0);
        emit_cycle(&o0, out);
    } else {
        cycle_ops(&cyc[1], &second, &o1);
        if (!reads_comb(o1.rgb) && !reads_comb(o1.a)) {
            /* the first cycle is dead */
            emit_cycle(&o1, out);
        } else {
            cycle_ops(&cyc[0], &first, &o0);
            if (single(o0.rgb) && single(o0.a)) {
                /* substitute the first cycle's inputs for COMBINED */
                Comb sub;
                sub.rgb = o0.rgb[3];
                sub.a = o0.a[3];
                sub.a_rgb = o0.a[3];
                if (sub.a_rgb.ch == CH_B) sub.a_rgb.ch = CH_RGB;   /* V1.b = V1.rgb (LOD_FRACTION) */
                out->approx_why |= why_of(o0.rgb) | why_of(o0.a);
                cycle_ops(&cyc[1], &sub, &o1);
                emit_cycle(&o1, out);
            } else {
                /* a first-cycle result that can't be negative is an unsigned input
                 * to the second (the UNSIGNED_INVERT shortcuts apply) */
                int nn = emit_cycle(&o0, out);
                Comb c1 = second;
                if (nn & 1) c1.rgb.kind = K_IN, c1.rgb.map = M_UID;
                if (nn & 2) c1.a.kind = c1.a_rgb.kind = K_IN, c1.a.map = c1.a_rgb.map = M_UID;
                cycle_ops(&cyc[1], &c1, &o1);
                emit_cycle(&o1, out);
            }
        }
    }
    if (out->nstages == 0) {   /* not reached: a first cycle always writes R0 */
        out->nstages = 1;
        out->cocw[0] = make_ocw(S_R0, OP_NOSHIFT);
        out->aocw[0] = make_ocw(S_R0, OP_NOSHIFT);
    }
    out->approximated = out->approx_why != 0;

    /* final combiner (Melee-X rc_compile / fog_final_cw0): rgb = A*B + (1-A)*C + D,
     * alpha = G; inputs are bytes of register | alpha << 4 | invert << 5 */
    out->cw0 = (uint32_t)S_R0 << 8;   /* A = B = D = 0, C = R0.rgb */
    if (fog) {
        out->cw0 |= (uint32_t)(S_FOG | 1 << 4) << 24 | (uint32_t)S_FOG << 16;   /* A = FOG.a, B = FOG.rgb */
        out->factor_use |= CC_USE_FOG;
    }
    out->cw1 = (uint32_t)(S_R0 | 1 << 4) << 8 | 0x80;   /* G = R0.a; specular clamp */

    for (i = 0; i < out->nstages; i++) {
        int k;
        for (k = 0; k < 32; k += 8) {
            out->factor_use |= use_of_byte(out->cicw[i] >> k & 0xFF);
            out->factor_use |= use_of_byte(out->aicw[i] >> k & 0xFF);
        }
    }
}
