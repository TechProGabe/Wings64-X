/* nv2a_cc.h - the N64 colour combiner (G_SETCOMBINE) -> NV2A register
 * combiners (nv2a_cc.c). Plain C: no pbkit or kernel calls, so the host test
 * (tools/xbox/test_cc.py) compiles it as is.
 *
 * What the back end must provide for a CcProg to compute what the RDP does:
 *
 *   texture unit 0 (T0)    TEXEL0 (the tile the interpreter binds as unit 0), rgba
 *   texture unit 1 (T1)    TEXEL1, rgba
 *   V0 (oD0, diffuse)      SHADE rgba (XgfxVtx.rgba)
 *   V1 (oD1, specular)     a constant per draw: (LOD_FRACTION, LOD_FRACTION, LOD_FRACTION, PRIM_LOD_FRAC),
 *                          i.e. r = g = b = XgfxState.lod_frac and a = XgfxState.prim_lod_frac (0..255 -> 0..1).
 *                          The pass-through vertex program writes it from a constant register. LOD_FRACTION is
 *                          broadcast in rgb because a combiner can read a register's rgb, its alpha broadcast,
 *                          or (alpha portion only) its blue, but not its red or green alone.
 *   C0 of every stage      NV097_SET_COMBINER_FACTOR0 + 4*i = PRIM (XgfxState.prim, as 0xAARRGGBB)
 *   C1 of every stage      NV097_SET_COMBINER_FACTOR1 + 4*i = ENV  (XgfxState.env,  as 0xAARRGGBB)
 *                          with NV097_SET_COMBINER_CONTROL = CC_COMBINER_CONTROL(prog) (each stage reads its own
 *                          factors, as Melee-X does: write the same PRIM/ENV into stages 0..nstages-1)
 *   FOG register           only when compiled with fog = 1: rgb = XgfxState.fog_color, alpha = the per-pixel fog
 *                          amount (0: none, 1: all fog colour). Melee-X's setup gives exactly that: vertex
 *                          program oFog.x = XgfxVtx.fog, NV097_SET_FOG_ENABLE 1, NV097_SET_FOG_MODE LINEAR,
 *                          NV097_SET_FOG_GEN_MODE SPEC_ALPHA (ignored with a vertex program),
 *                          NV097_SET_FOG_PARAMS (1, 1, 0) (LINEAR computes p0 + p1 * oFog.x - 1),
 *                          NV097_SET_FOG_COLOR = fog colour.
 *   final combiner         NV097_SET_COMBINER_SPECULAR_FOG_CW0/CW1 = cw0/cw1. The specular-fog factor
 *                          registers (NV097_SET_SPECULAR_FOG_FACTOR) are not read.
 *
 * K4, K5 (YUV), NOISE and the chroma-key CENTER/SCALE have no register: a
 * combiner reading them is approximated (see CC_APPROX_*). PW64 never enables
 * YUV or chroma key (Birdman64 docs/notes/gfx.md "GBI coverage audit"). */
#ifndef NV2A_CC_H
#define NV2A_CC_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC_MAX_STAGES 8

/* CcProg.factor_use: the registers the program reads (the back end may skip
 * what is not read: texture units, factor uploads, the oD1 constant) */
enum {
    CC_USE_T0 = 1u << 0,      /* TEXEL0: texture unit 0 */
    CC_USE_T1 = 1u << 1,      /* TEXEL1: texture unit 1 */
    CC_USE_SHADE = 1u << 2,   /* V0 */
    CC_USE_PRIM = 1u << 3,    /* C0 (FACTOR0) */
    CC_USE_ENV = 1u << 4,     /* C1 (FACTOR1) */
    CC_USE_V1 = 1u << 5,      /* V1 = (lod_frac, lod_frac, lod_frac, prim_lod_frac) */
    CC_USE_FOG = 1u << 6,     /* the final combiner blends toward the fog colour */
};

/* CcProg.approx_why: what made `approximated` 1 */
enum {
    CC_APPROX_NOISE = 1u << 0,      /* NOISE (rgb a): read as 0.5 */
    CC_APPROX_K4 = 1u << 1,         /* K4 (rgb b): read as 0 */
    CC_APPROX_K5 = 1u << 2,         /* K5 (rgb c): read as 0 */
    CC_APPROX_KEY = 1u << 3,        /* chroma-key CENTER (rgb b) / SCALE (rgb c): read as 0 */
    CC_APPROX_COMBINED0 = 1u << 4,  /* COMBINED read in the first cycle (the previous pixel's): read as 0 */
};

typedef struct {
    int nstages;                                  /* 1..8 general combiner stages */
    uint32_t cicw[8], cocw[8], aicw[8], aocw[8];  /* NV097_SET_COMBINER_{COLOR,ALPHA}_{ICW,OCW} per stage */
    uint32_t cw0, cw1;                            /* NV097_SET_COMBINER_SPECULAR_FOG_CW0/CW1 (final combiner) */
    uint32_t factor_use;                          /* CC_USE_*: which inputs the program reads */
    int approximated;                             /* 1: some input/mode could not be reproduced exactly */
    uint32_t approx_why;                          /* CC_APPROX_*: why */
} CcProg;

/* NV097_SET_COMBINER_CONTROL for a program: nstages, FACTOR0 and FACTOR1
 * per stage (EACH_STAGE), mux select LSB */
#define CC_COMBINER_CONTROL(p) ((uint32_t)(p)->nstages | (1u << 12) | (1u << 16))

/* cc0 = G_SETCOMBINE w0 & 0x00FFFFFF, cc1 = w1; two_cycle 0: cycle 0's
 * fields only; fog 1: the final combiner lerps toward the fog colour. */
void cc_compile(uint32_t cc0, uint32_t cc1, int two_cycle, int fog, CcProg* out);

#ifdef __cplusplus
}
#endif
#endif
