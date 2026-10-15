/* cc_ref.h - a software model of the N64 RDP colour combiner (cc_ref.c), the
 * reference test_cc.c checks the NV2A register-combiner compiler against. */
#ifndef CC_REF_H
#define CC_REF_H
#include <stdint.h>

typedef struct {
    uint8_t t0[4], t1[4], shade[4], prim[4], env[4];   /* rgba */
    uint8_t lod_frac, prim_lod_frac;
    uint8_t k4, k5;                                    /* YUV convert constants (9-bit signed on the RDP; 0..255 here) */
    uint8_t center[3], scale[3];                       /* chroma key */
    uint8_t noise;                                     /* this pixel's noise */
} CcRefIn;

/* cc_ref_eval's flags: the RDP's result here depends on its 9-bit arithmetic
 * (a wrap, an intermediate past what an NV2A register holds); see cc_ref.c */
enum { CCREF_RANGE = 1 };

/* out: rgba 0..255. Returns CCREF_* flags. */
uint32_t cc_ref_eval(uint32_t cc0, uint32_t cc1, int two_cycle, const CcRefIn* in, uint8_t out[4]);

/* The same combiner in exact arithmetic on the NV2A's scale (255 = 1.0, the
 * constant 1 = 1.0), clamped to [0, 1] at the end: rgba. */
void cc_ideal_eval(uint32_t cc0, uint32_t cc1, int two_cycle, const CcRefIn* in, double out[4]);

#endif
