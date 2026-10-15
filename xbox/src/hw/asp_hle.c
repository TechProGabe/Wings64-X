/* asp_hle.c - the RSP audio microcode (aspMain, the classic libultra audio ABI) in C.
 *
 * PLAN.md D6 / M6. The decomp's own synthesizer (src/libultra/audio: alSyn,
 * 22050 Hz, 32 PVoices, 368-sample frames cut into <= 160-sample chunks,
 * AL_FX_CUSTOM reverb) builds Acmd lists for aspMain; this file runs them on
 * the CPU when the port's osSpTaskStartGo sees an M_AUDTASK. Commands
 * (include/libultra/PR/abi.h): SPNOOP, ADPCM, CLEARBUFF, ENVMIXER, LOADBUFF,
 * RESAMPLE, SAVEBUFF, SEGMENT, SETBUFF, SETVOL, DMEMMOVE, LOADADPCM, MIXER,
 * INTERLEAVE, POLEF, SETLOOP.
 *
 * Origin: a C port of Birdman64's crates/pw64-audio/src/abi.rs (Copyright (c)
 * 2026 Birdman64 contributors, MIT; https://github.com/NoahVl/Birdman64), the
 * oracle for each command's arithmetic in this game: Birdman64 derived it from
 * the emitting C and checked every handler against a disassembly of this ROM's
 * aspMain (its docs/notes/audio.md). sm64-port's mixer.c was read as a second
 * opinion only; nothing from mupen64plus-rsp-hle (GPL). Where this file departs
 * from abi.rs it says so ("Birdman64:").
 *
 * --- Model ---------------------------------------------------------------
 * DMEM: the command list addresses aspMain's buffer area by 16-bit offsets
 * (synthInternals.h: AL_DECODER_IN 0 ... AL_AUX_R_OUT 2048 + 320 bytes, so
 * 2368 bytes are used). Modelled as 4 KB indexed `offset & 0xFFF`, s16 lanes at
 * `(offset >> 1) & 0x7FF` (odd offsets floor to even, as abi.rs). DMEM persists
 * between tasks; everything the microcode keeps in its data segment (segment
 * table, SETBUFF/SETVOL registers, loop address, LOADADPCM table) is cleared at
 * each task start, as the RSP reloads that segment with every task.
 * DMA (LOADBUFF, SAVEBUFF, state and table loads): both addresses & ~7 and the
 * length rounded up to 8, as the SP DMA engine does (_decodeChunk relies on it).
 * Segments: an address word resolves as seg[(w >> 24) & 15] + (w & 0xFFFFFF),
 * then goes through the caller's asp_addr_fn. PW64 only ever sets segment 0 to
 * 0, so the words are K0_TO_PHYS / osVirtualToPhysical values below 16 MB.
 * Kill switch -DASP_HLE_SEGMENTS=0 hands the raw word to the callback instead
 * (for glue that puts full host pointers in the list).
 *
 * --- Byte order on the port (x86, little-endian) --------------------------
 * DMEM s16 lanes are host-endian; LOADBUFF/SAVEBUFF/DMEMMOVE copy raw bytes.
 * So every s16 buffer in RDRAM the microcode reads or writes is native s16:
 *   - ADPCM frames (A_LOADBUFF from the __amDMA 0x800-byte copies of the
 *     .tbl in the ROM image): big-endian ROM bytes, copied raw, decoded byte by
 *     byte (header, then nibbles high first). No swap anywhere.
 *   - Codebook (A_LOADADPCM <- ALADPCMBook.book): native s16. The port's bank
 *     loader (wings64-dc patch 015 / pw64_audio_load.c load_book) builds the
 *     book with rd16() from the BE .ctl, i.e. converts it.
 *   - Loop state (A_SETLOOP <- ALADPCMloop.state via ALLoadFilter.lstate,
 *     alCopy'd): native s16, converted by the same loader (load_adpcm_loop).
 *   - POLEF coefficients (A_LOADADPCM <- ALLowPass.fcvec.fccoef, computed by
 *     _init_lpfilter on the CPU): native s16.
 *   - ADPCM_STATE, RESAMPLE_STATE, ENVMIX_STATE, POLEF_STATE: only the
 *     microcode reads them (the CPU allocates them and never looks inside), so
 *     their layout is private to this file and native: ADPCM 16 x s16 (the last
 *     frame); RESAMPLE s16 tail[4] + u16 frac; ENVMIX s32 value[2], target[2],
 *     rate[2], seq[2] + s16 dry, wet (36 of 80 bytes); POLEF s16 {0, 0, y2, y1}.
 *   - Reverb delay line (LOADBUFF/SAVEBUFF of AL_AUX_* data): native s16; the
 *     CPU only zeroes it.
 *   - The AI buffer (save.c: A_INTERLEAVE then A_SAVEBUFF to ALSave.dramout):
 *     native s16 stereo, L first; the port's AI shim reads it natively.
 *   - RAW16 waves (alRaw16Pull) would load BE PCM raw and play it byte-swapped;
 *     PW64 has none (all 164 waves are VADPCM, Birdman64 audio.md).
 *
 * --- Arithmetic -----------------------------------------------------------
 * Saturating s16 stores; Q15 products rounded as vmulf: (a*b + 0x4000) >> 15.
 * ADPCM: matrix form (8-sample groups, order 2, book[pred][0] weights y[-2],
 * book[pred][1] weights y[-1] and the in-group residuals), floor(acc / 2048),
 * scale shift capped at 12. RESAMPLE: Q16.16 position, step = pitch << 1
 * (pitch Q1.15, 0x8000 = unity; resample.c clamps the ratio to MAX_RATIO
 * 1.99996, i.e. pitch 0xFFFE), phase = frac >> 10 picks 4 taps of the 64-phase
 * table, outputs counted in groups of 8. ENVMIXER: volumes Q16.16, every 8
 * samples seq *= rate (Q16.16, env.c _getRate returns the 8th power) and the 8
 * samples ramp linearly toward it, clamped at the target; gain = vol x dry|wet,
 * 2 or 4 sends. MIXER: dst += src x gain, 32 bytes per step. POLEF: order-2 IIR
 * in the same 8-wide matrix form, Q14, gain read as 16 bits (PW64's VERSION_D
 * aPoleFilter packs fgain & 0xFF, so the three lowpass reverb sections are
 * silent; faithful, also on hardware per Birdman64).
 *
 * --- SIMD (PLAN.md D13) ---------------------------------------------------
 * The hot loops (ADPCM frames, RESAMPLE, the ENVMIXER and MIXER mixes) have an
 * MMX path, the Pentium III's 16-bit integer SIMD: pmaddwd gives exact 32-bit
 * sums of s16 products, psrad the floor shifts, packssdw the s16 saturation, so
 * each lane does exactly what the C does. SSE1 floats cannot (s16 x s16 needs
 * 31 bits). It is inline assembly: LLVM 20+ implements the MMX intrinsics with
 * SSE2, which the P3 lacks ("requires target feature 'sse2'"); MMX assembly is
 * still supported. A path is taken only where it is provably the same bits:
 * ADPCM when every book entry is within +-7000 and RESAMPLE when every phase's
 * sum |tap| < 65535 (no 32-bit wrap; else the scalar int64 code runs); all of
 * them only when their DMEM ranges neither wrap nor partially overlap (the
 * synthesizer's never do). Scalar only: POLEF (silent in PW64), INTERLEAVE,
 * DMA/CLEARBUFF/DMEMMOVE (memcpy), the per-tap resampler variant, the
 * envelope ramp and gain computation (the mixes it feeds are MMX).
 * -DASP_SCALAR builds the plain C only; tools/xbox/test_asp_hle.py builds both
 * (and 32-bit pentium3 builds with a second compiler) and requires identical
 * output.
 *
 * --- Cost (Pentium III 733 MHz) -----------------------------------------
 * Work per voice and 368-sample frame at pitch ~1: ADPCM ~23 frames, RESAMPLE
 * 368 outputs, ENVMIXER 368 x 4 sends, ~200 bytes of LOADBUFF. Host benchmark
 * (test_asp_hle --bench: 32 voices all playing, pitches 0.37..2.0, the
 * reverb-shaped bus, 3 chunks per frame; Ryzen 7 5700X3D): this file as the
 * XBE's object (nxdk clang -O2 -march=pentium3, linked into the 32-bit test)
 * 91 us per frame; 32-bit gcc MMX 107 us (ADPCM ~42 %, ENVMIXER ~31 %,
 * RESAMPLE ~14 %), scalar 273 us. Scaled to the P3 by ~14-17x (clock 4.1 GHz /
 * 733 MHz, Zen 3's ~2.5-3x IPC on this integer code): MMX ~1.3-1.5 ms per
 * 16.7 ms frame = 8-9 % of the CPU with all 32 voices playing, scalar ~23-28 %;
 * play with 8-16 voices is a third to a half of that. An estimate: [PERF]
 * audio on the console is the number (M6; PLAN.md's fallback threshold is 8 %
 * of a frame).
 *
 * --- DIVERGES (from the RSP) -----------------------------------------------
 * Marked inline. In short: DMEM offsets >= 0xA40 alias microcode data on the
 * RSP (buffer base 0x5C0) but are free space here; transfers longer than 4 KB
 * are cut to 4 KB; ADPCM predictor indices 8..15 are clamped to 7; the resampler
 * table falls back to linear interpolation until the ROM's is installed;
 * INTERLEAVE and DMEMMOVE read all input before writing; unresolvable addresses
 * read as zeros; the vector unit's exact accumulation order is not reproduced
 * (Birdman64: outputs may differ from hardware by an LSB or two). */
#include <stdint.h>
#include <string.h>

#include "asp_hle.h"

#ifndef ASP_HLE_SEGMENTS
#ifdef XBOX   /* the console: patches/029 puts full host pointers in the list */
#define ASP_HLE_SEGMENTS 0
#else
#define ASP_HLE_SEGMENTS 1          /* 0: address words go to the callback unresolved */
#endif
#endif
#ifndef ASP_HLE_ENV_FASTPATH
#define ASP_HLE_ENV_FASTPATH 1      /* 0: ENVMIXER recomputes the gains every sample even when no ramp runs */
#endif
#ifndef ASP_HLE_RESAMPLE_PER_TAP
#define ASP_HLE_RESAMPLE_PER_TAP 0  /* 1: each tap rounded like a lone vmulf (sm64-port's reading); scalar only */
#endif

/* GNU-style inline asm: gcc, and clang in every mode (nxdk's i386-pc-win32 is
 * clang's MSVC environment: _MSC_VER, no __GNUC__) */
#if !defined(ASP_SCALAR) && (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))
#define ASP_MMX 1
#else
#define ASP_MMX 0
#endif

/* abi.h */
enum {
    A_SPNOOP, A_ADPCM, A_CLEARBUFF, A_ENVMIXER, A_LOADBUFF, A_RESAMPLE, A_SAVEBUFF, A_SEGMENT,
    A_SETBUFF, A_SETVOL, A_DMEMMOVE, A_LOADADPCM, A_MIXER, A_INTERLEAVE, A_POLEF, A_SETLOOP
};
#define A_INIT 0x01
#define A_LOOP 0x02
#define A_LEFT 0x02
#define A_VOL 0x04
#define A_AUX 0x08

#define DMEM_SIZE 0x1000u
#define DMEM_MASK 0xFFFu
#define LANE_MASK 0x7FFu            /* s16 lanes */

static union {
    uint8_t b[DMEM_SIZE];
    int16_t s[DMEM_SIZE / 2];
    uint64_t align;
} s_dmem;

#define DB(off) s_dmem.b[(off) & DMEM_MASK]
#define DS(lane) s_dmem.s[(lane) & LANE_MASK]

/* What the microcode keeps in its data segment: cleared at every task start. */
static struct {
    uint32_t seg[16];
    uint32_t in, out, count;                    /* A_SETBUFF */
    uint32_t dry_right, wet_left, wet_right;    /* A_SETBUFF | A_AUX */
    int16_t vol[2], target[2];                  /* A_SETVOL, [0] = left */
    int32_t rate[2];
    int16_t dry, wet;
    uint32_t loop;                              /* A_SETLOOP, resolved */
    int16_t table[128];                         /* A_LOADADPCM */
    int table_small;                            /* every entry within +-7000: int32 sums cannot wrap */
    uint32_t mtx_built;                         /* predictors with s_mtx built from this table */
} T;

static asp_addr_fn s_addr;
static void* s_user;

static int16_t s_lut[256];
static int s_lut_small;     /* every phase's sum |tap| < 65535: the 4-tap dot product fits int32 */
static int s_inited;
static asp_hle_stats_t s_stats;

static inline int16_t sat16(int32_t v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }
static inline int16_t sat16_64(int64_t v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }
static inline int32_t mulq15(int32_t a, int32_t b) { return (a * b + 0x4000) >> 15; }
static inline uint32_t align_up(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

/* ---- RDRAM ------------------------------------------------------------- */

static uint32_t resolve(uint32_t w)
{
#if ASP_HLE_SEGMENTS
    return T.seg[(w >> 24) & 15] + (w & 0xFFFFFFu);
#else
    return w;
#endif
}

static uint8_t* rdram_at(uint32_t a)
{
    uint8_t* p = (uint8_t*)s_addr(a & ~7u, s_user);
    if (!p) s_stats.bad_addr++;
    return p;
}

/* DIVERGES: an address the callback cannot map reads as zeros and is not written. */
static void rd_load(uint32_t a, void* dst, uint32_t n)
{
    uint8_t* p = rdram_at(a);
    if (p) memcpy(dst, p, n);
    else memset(dst, 0, n);
}

static void rd_store(uint32_t a, const void* src, uint32_t n)
{
    uint8_t* p = rdram_at(a);
    if (p) memcpy(p, src, n);
}

/* ---- DMEM bulk (wrap-aware) --------------------------------------------- */

static void dmem_from(uint32_t off, const uint8_t* src, uint32_t n)
{
    while (n) {
        uint32_t o = off & DMEM_MASK, c = DMEM_SIZE - o;
        if (c > n) c = n;
        memcpy(s_dmem.b + o, src, c);
        src += c, off += c, n -= c;
    }
}

static void dmem_to(uint8_t* dst, uint32_t off, uint32_t n)
{
    while (n) {
        uint32_t o = off & DMEM_MASK, c = DMEM_SIZE - o;
        if (c > n) c = n;
        memcpy(dst, s_dmem.b + o, c);
        dst += c, off += c, n -= c;
    }
}

static void dmem_zero(uint32_t off, uint32_t n)
{
    while (n) {
        uint32_t o = off & DMEM_MASK, c = DMEM_SIZE - o;
        if (c > n) c = n;
        memset(s_dmem.b + o, 0, c);
        off += c, n -= c;
    }
}

/* DIVERGES: a transfer longer than DMEM wraps over itself on the RSP; it is cut
 * to 4 KB here (keeps the host copy inside what the callback mapped). */
static uint32_t cap_dmem(uint32_t n) { return n > DMEM_SIZE ? DMEM_SIZE : n; }

#if ASP_MMX
/* SIMD eligibility: an s16 lane range that does not wrap DMEM. */
static int lanes_flat(uint32_t lane, uint32_t n) { return (lane & LANE_MASK) + n <= DMEM_SIZE / 2; }
/* Two lane ranges whose lane-by-lane updates commute: the same start or disjoint. */
static int same_or_apart(uint32_t a, uint32_t b, uint32_t n)
{
    a &= LANE_MASK, b &= LANE_MASK;
    return a == b || a + n <= b || b + n <= a;
}
static int apart(uint32_t a, uint32_t an, uint32_t b, uint32_t bn)
{
    a &= LANE_MASK, b &= LANE_MASK;
    return a + an <= b || b + bn <= a;
}

typedef uint64_t __attribute__((may_alias, aligned(2))) u64a;
typedef uint32_t __attribute__((may_alias, aligned(2))) u32a;

static const int16_t k_ones[4] __attribute__((aligned(8))) = {1, 1, 1, 1};
#if !ASP_HLE_RESAMPLE_PER_TAP
static const int32_t k_rnd15[2] __attribute__((aligned(8))) = {0x4000, 0x4000};
#endif

static inline void mmx_end(void) { __asm__ __volatile__("emms"); }

/* d[0..3] = sat16(d + ((s * g + 0x4000) >> 15)), gp = {g0, 0x4000, g1, 0x4000, g2, 0x4000, g3, 0x4000}:
 * pmaddwd of (s, 1) with (g, 0x4000) is the rounded product in 32 bits, d is
 * sign-extended and added in 32 bits, packssdw saturates once, as sat16 does. */
static inline void mmx_mix4(int16_t* d, const int16_t* s, const int16_t* gp)
{
    __asm__ __volatile__(
        "movq %[s], %%mm0\n\t"
        "movq %%mm0, %%mm1\n\t"
        "punpcklwd %[one], %%mm0\n\t"
        "punpckhwd %[one], %%mm1\n\t"
        "pmaddwd %[g01], %%mm0\n\t"
        "pmaddwd %[g23], %%mm1\n\t"
        "psrad $15, %%mm0\n\t"
        "psrad $15, %%mm1\n\t"
        "movq %[d], %%mm2\n\t"
        "movq %%mm2, %%mm3\n\t"
        "punpcklwd %%mm2, %%mm2\n\t"
        "punpckhwd %%mm3, %%mm3\n\t"
        "psrad $16, %%mm2\n\t"
        "psrad $16, %%mm3\n\t"
        "paddd %%mm2, %%mm0\n\t"
        "paddd %%mm3, %%mm1\n\t"
        "packssdw %%mm1, %%mm0\n\t"
        "movq %%mm0, %[d]\n\t"
        : [d] "+m"(*(u64a*)d)
        : [s] "m"(*(const u64a*)s), [g01] "m"(*(const u64a*)gp), [g23] "m"(*(const u64a*)(gp + 4)),
          [one] "m"(*(const u64a*)k_ones)
        : "mm0", "mm1", "mm2", "mm3");
}

static void gain_pairs(int16_t* gp, const int16_t* g, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) gp[2 * i] = g[i], gp[2 * i + 1] = 0x4000;
}
#endif

/* ---- A_LOADADPCM / A_ADPCM ---------------------------------------------- */

#if ASP_MMX
/* Per predictor, the 8x10 matrix taking v = (y[-2], y[-1], r0..r7) to the 8
 * accumulators, as pmaddwd quads: [kp][mp] = {M[2kp][2mp], M[2kp][2mp+1],
 * M[2kp+1][2mp], M[2kp+1][2mp+1]}. Built on first use after each A_LOADADPCM. */
static int16_t s_mtx[8][4][5][4] __attribute__((aligned(8)));

static void mtx_build(uint32_t pred)
{
    const int16_t* c0 = T.table + pred * 16;
    const int16_t* c1 = c0 + 8;
    /* M[k] = (c0[k], c1[k], t[7 + k], t[6 + k], ..., t[k]): the in-group part is
     * Toeplitz in t = (0 x 7, 2048, c1[0..7]) */
    int16_t t[16], row[8][10];
    int k, j, kp, mp;
    memset(t, 0, 7 * sizeof t[0]);
    t[7] = 2048;
    memcpy(t + 8, c1, 8 * sizeof t[0]);
    for (k = 0; k < 8; k++) {
        row[k][0] = c0[k], row[k][1] = c1[k];
        for (j = 0; j < 8; j++) row[k][2 + j] = t[7 + k - j];
    }
    for (kp = 0; kp < 4; kp++)
        for (mp = 0; mp < 5; mp++) {
            int16_t* q = s_mtx[pred][kp][mp];
            q[0] = row[2 * kp][2 * mp], q[1] = row[2 * kp][2 * mp + 1];
            q[2] = row[2 * kp + 1][2 * mp], q[3] = row[2 * kp + 1][2 * mp + 1];
        }
    T.mtx_built |= 1u << pred;
}

static const int16_t k_nibmul[4] __attribute__((aligned(8))) = {256, 4096, 256, 4096};
static const uint16_t k_nibmask[4] __attribute__((aligned(8))) = {0xF000, 0xF000, 0xF000, 0xF000};

/* One 9-byte frame at fr -> 16 samples at o, history y[-2], y[-1] read from
 * o[-2], o[-1] (the previous frame's tail, or the 16 history samples).
 * Residuals: the 4 bytes of a group zero-extended to words, duplicated, times
 * (256, 4096) puts the high / low nibble in bits 12..15, masked, then psraw by
 * 12 - scale = the sign-extended nibble << scale. Then per output pair the
 * five pmaddwd of the matrix quads with (y[-2], y[-1]), (r0, r1) .. (r6, r7),
 * psrad 11 (floor), packssdw (saturation). Group 1's history is group 0's
 * o[6], o[7], read back from o. */
static inline void mmx_adpcm_frame(int16_t* o, const uint8_t* fr, const int16_t* m, const uint64_t* cnt)
{
#define ACC(dst, base)                                                  \
    "movq %%mm0, %%" dst "\n\t" "pmaddwd " base "+0(%[m]), %%" dst "\n\t"  \
    "movq %%mm1, %%mm6\n\t" "pmaddwd " base "+8(%[m]), %%mm6\n\t" "paddd %%mm6, %%" dst "\n\t"  \
    "movq %%mm2, %%mm6\n\t" "pmaddwd " base "+16(%[m]), %%mm6\n\t" "paddd %%mm6, %%" dst "\n\t" \
    "movq %%mm3, %%mm6\n\t" "pmaddwd " base "+24(%[m]), %%mm6\n\t" "paddd %%mm6, %%" dst "\n\t" \
    "movq %%mm4, %%mm6\n\t" "pmaddwd " base "+32(%[m]), %%mm6\n\t" "paddd %%mm6, %%" dst "\n\t" \
    "psrad $11, %%" dst "\n\t"
#define GROUP(boff, hoff, ooff)                                                       \
    "movd " boff "(%[fr]), %%mm6\n\t"                                                 \
    "pxor %%mm7, %%mm7\n\t"                                                           \
    "punpcklbw %%mm7, %%mm6\n\t"                                                      \
    "movq %%mm6, %%mm5\n\t"                                                           \
    "punpcklwd %%mm6, %%mm6\n\t"                                                      \
    "punpckhwd %%mm5, %%mm5\n\t"                                                      \
    "pmullw %[mul], %%mm6\n\t" "pmullw %[mul], %%mm5\n\t"                             \
    "pand %[mask], %%mm6\n\t" "pand %[mask], %%mm5\n\t"                               \
    "psraw %[cnt], %%mm6\n\t" "psraw %[cnt], %%mm5\n\t"                               \
    "movd " hoff "(%[o]), %%mm0\n\t" "punpckldq %%mm0, %%mm0\n\t"                     \
    "movq %%mm6, %%mm1\n\t" "punpckldq %%mm1, %%mm1\n\t"                              \
    "movq %%mm6, %%mm2\n\t" "punpckhdq %%mm2, %%mm2\n\t"                              \
    "movq %%mm5, %%mm3\n\t" "punpckldq %%mm3, %%mm3\n\t"                              \
    "movq %%mm5, %%mm4\n\t" "punpckhdq %%mm4, %%mm4\n\t"                              \
    ACC("mm7", "0") ACC("mm5", "40") "packssdw %%mm5, %%mm7\n\t"                      \
    "movq %%mm7, " ooff "(%[o])\n\t"                                                  \
    ACC("mm7", "80") ACC("mm5", "120") "packssdw %%mm5, %%mm7\n\t"                    \
    "movq %%mm7, " ooff "+8(%[o])\n\t"
    __asm__ __volatile__(
        GROUP("1", "-4", "0")
        GROUP("5", "12", "16")
        :
        : [o] "r"(o), [fr] "r"(fr), [m] "r"(m), [cnt] "m"(*cnt), [mul] "m"(*(const u64a*)k_nibmul),
          [mask] "m"(*(const u64a*)k_nibmask)
        : "memory", "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7");
#undef GROUP
#undef ACC
}
#endif

static void cmd_loadadpcm(uint32_t w0, uint32_t w1)
{
    uint32_t n = w0 & 0xFFFFFFu, i;
    int32_t mx = 0;
    if (n > sizeof T.table) n = sizeof T.table;
    rd_load(resolve(w1), T.table, n & ~1u);
    for (i = 0; i < 128; i++) {
        int32_t a = T.table[i] < 0 ? -T.table[i] : T.table[i];
        if (a > mx) mx = a;
    }
    /* |sum| <= 32768 * (9 * max + 2048) < 2^31 */
    T.table_small = mx <= 7000;
    T.mtx_built = 0;
}

/* Frame header -> scale shift and predictor. */
static inline void adpcm_header(uint32_t hdr, int* sh, uint32_t* pred)
{
    *sh = (int)(hdr >> 4);
    *pred = hdr & 15;
    if (*sh > 12) *sh = 12;     /* the microcode caps the scale; VADPCM encoders never exceed 12. Birdman64: no cap */
    if (*pred > 7) *pred = 7;   /* DIVERGES: 8..15 index past the 256-byte table on the RSP. As Birdman64 */
}

/* One 9-byte frame -> 16 samples; h[0] = y[-2], h[1] = y[-1], updated. */
static void adpcm_frame(const uint8_t* fr, int16_t* h, int16_t* out)
{
    int sh, g, k, m;
    uint32_t pred;
    const int16_t *c0, *c1;
    adpcm_header(fr[0], &sh, &pred);
    c0 = T.table + pred * 16;
    c1 = c0 + 8;
    for (g = 0; g < 2; g++) {
        int16_t r[8], *o = out + 8 * g;
        for (m = 0; m < 8; m++) {
            int b = fr[1 + 4 * g + (m >> 1)], nib = (m & 1) ? (b & 15) : (b >> 4);
            r[m] = (int16_t)(((nib ^ 8) - 8) * (1 << sh));
        }
        if (T.table_small) {
            for (k = 0; k < 8; k++) {
                int32_t acc = c0[k] * h[0] + c1[k] * h[1] + r[k] * 2048;
                for (m = 0; m < k; m++) acc += c1[k - 1 - m] * r[m];
                o[k] = sat16(acc >> 11);
            }
        } else {
            for (k = 0; k < 8; k++) {
                int64_t acc = (int64_t)c0[k] * h[0] + (int64_t)c1[k] * h[1] + r[k] * 2048;
                for (m = 0; m < k; m++) acc += (int64_t)c1[k - 1 - m] * r[m];
                o[k] = sat16_64(acc >> 11);
            }
        }
        h[0] = o[6], h[1] = o[7];
    }
}

/* Writes the previous frame's 16 samples at out (alAdpcmPull skips them by
 * lastsam), then ceil(count / 32) frames from in (any byte offset: _decodeChunk
 * decodes from in + dramAlign). History: zeros (A_INIT), the A_SETLOOP state
 * (A_LOOP) or the state at w1, which receives the last frame. */
static void cmd_adpcm(uint32_t flags, uint32_t w1)
{
    int16_t last[16], h[2];
    uint32_t state = resolve(w1), lane = T.out >> 1, in = T.in, nf = (T.count + 31) >> 5, f, k;
    if (flags & A_INIT) memset(last, 0, sizeof last);
    else if (flags & A_LOOP) rd_load(T.loop, last, sizeof last);
    else rd_load(state, last, sizeof last);
    for (k = 0; k < 16; k++) DS(lane + k) = last[k];
#if ASP_MMX
    /* straight on DMEM when the input bytes and the output lanes neither wrap nor overlap */
    if (nf && T.table_small && (in & DMEM_MASK) + 9 * nf <= DMEM_SIZE && lanes_flat(lane, 16 + 16 * nf) &&
        ((in & DMEM_MASK) + 9 * nf <= (lane & LANE_MASK) * 2 || (lane & LANE_MASK) * 2 + 32 + 32 * nf <= (in & DMEM_MASK))) {
        const uint8_t* fr = s_dmem.b + (in & DMEM_MASK);
        int16_t* o = s_dmem.s + (lane & LANE_MASK) + 16;
        for (f = 0; f < nf; f++, fr += 9, o += 16) {
            int sh;
            uint32_t pred;
            uint64_t cnt;
            adpcm_header(fr[0], &sh, &pred);
            if (!(T.mtx_built & (1u << pred))) mtx_build(pred);
            cnt = (uint64_t)(12 - sh);
            mmx_adpcm_frame(o, fr, &s_mtx[pred][0][0][0], &cnt);
        }
        mmx_end();
        memcpy(last, o - 16, sizeof last);
        rd_store(state, last, sizeof last);
        return;
    }
#endif
    lane += 16;
    h[0] = last[14], h[1] = last[15];
    for (f = 0; f < nf; f++) {
        uint8_t fr[9];
        for (k = 0; k < 9; k++) fr[k] = DB(in + k);
        adpcm_frame(fr, h, last);
        for (k = 0; k < 16; k++) DS(lane + k) = last[k];
        lane += 16, in += 9;
    }
    rd_store(state, last, sizeof last);
}

/* ---- A_RESAMPLE ----------------------------------------------------------- */

static int lut_check(const int16_t* lut, int* small)
{
    int p, k, ok = 1;
    int32_t worst = 0;
    for (p = 0; p < 64; p++) {
        int32_t sum = 0, abs_sum = 0;
        for (k = 0; k < 4; k++) {
            int32_t t = lut[p * 4 + k];
            sum += t, abs_sum += t < 0 ? -t : t;
            if (t != lut[(63 - p) * 4 + 3 - k]) ok = 0;
        }
        if (sum < 0x7000 || sum >= 0x9000) ok = 0;
        if (abs_sum > worst) worst = abs_sum;
    }
    if (small) *small = worst < 65535;
    return ok;
}

/* DIVERGES (until the ROM's table is installed): linear interpolation between
 * taps 1 and 2, the real table's 3-sample delay (Birdman64 lut::linear). */
static void lut_linear(void)
{
    int p;
    memset(s_lut, 0, sizeof s_lut);
    for (p = 0; p < 64; p++) {
        int32_t f = (p * 0x8000 + 32) / 64;
        s_lut[p * 4 + 1] = (int16_t)(0x7FFF - f);
        s_lut[p * 4 + 2] = (int16_t)f;
    }
    lut_check(s_lut, &s_lut_small);
    s_stats.rom_lut = 0;
}

int asp_hle_resample_lut_valid(const int16_t lut[256]) { return lut_check(lut, 0); }

int asp_hle_set_resample_lut(const int16_t lut[256])
{
    int small;
    if (!s_inited) asp_hle_reset();
    if (!lut_check(lut, &small)) return 0;
    memcpy(s_lut, lut, sizeof s_lut);
    s_lut_small = small;
    s_stats.rom_lut = 1;
    return 1;
}

int asp_hle_set_resample_lut_be(const uint8_t be[512])
{
    int16_t lut[256];
    int i;
    for (i = 0; i < 256; i++) lut[i] = (int16_t)(be[2 * i] << 8 | be[2 * i + 1]);
    return asp_hle_set_resample_lut(lut);
}

static inline int16_t dot4(uint32_t pos, const int16_t* t)
{
#if ASP_HLE_RESAMPLE_PER_TAP
    return sat16(mulq15(DS(pos), t[0]) + mulq15(DS(pos + 1), t[1]) + mulq15(DS(pos + 2), t[2]) +
                 mulq15(DS(pos + 3), t[3]));
#else
    if (s_lut_small)
        return sat16((DS(pos) * t[0] + DS(pos + 1) * t[1] + DS(pos + 2) * t[2] + DS(pos + 3) * t[3] + 0x4000) >> 15);
    return sat16_64(((int64_t)DS(pos) * t[0] + (int64_t)DS(pos + 1) * t[1] + (int64_t)DS(pos + 2) * t[2] +
                     (int64_t)DS(pos + 3) * t[3] + 0x4000) >> 15);
#endif
}

/* 4-tap polyphase. Tap 0 of the first output is 4 samples before in: the
 * previous call's tail (state) or zeros (A_INIT) are written there. Outputs:
 * count bytes rounded up to 8 samples. State: the 4 samples at the final
 * position and the fraction. */
static void cmd_resample(uint32_t flags, uint32_t pitch, uint32_t w1)
{
    int16_t st[5];
    uint32_t state = resolve(w1), base = ((T.in - 8) & 0xFFFFu) >> 1, step = pitch << 1;
    uint32_t n = align_up(T.count, 16) >> 1, o = T.out >> 1, pos = base, frac, i;
    if (flags & A_INIT) memset(st, 0, sizeof st);
    else rd_load(state, st, sizeof st);
    for (i = 0; i < 4; i++) DS(base + i) = st[i];
    frac = (uint16_t)st[4];
    i = 0;
#if ASP_MMX && !ASP_HLE_RESAMPLE_PER_TAP
    if (s_lut_small && n) {
        /* lanes read: base .. base + last position + 3 */
        uint64_t span = (((uint64_t)frac + (uint64_t)(n - 1) * step) >> 16) + 4;
        if (span <= DMEM_SIZE / 2 && lanes_flat(base, (uint32_t)span) && lanes_flat(o, n) &&
            apart(base, (uint32_t)span, o, n)) {
            int16_t* in = s_dmem.s + (base & LANE_MASK);
            int16_t* out = s_dmem.s + (o & LANE_MASK);
            uint32_t p = 0;
            for (; i < n; i += 2) {
                uint32_t p0 = p, f0 = frac, p1, f1;
                frac += step, p += frac >> 16, frac &= 0xFFFF;
                p1 = p, f1 = frac;
                frac += step, p += frac >> 16, frac &= 0xFFFF;
                __asm__ __volatile__(
                    "movq %[s0], %%mm0\n\t"
                    "pmaddwd %[t0], %%mm0\n\t"
                    "movq %[s1], %%mm1\n\t"
                    "pmaddwd %[t1], %%mm1\n\t"
                    "movq %%mm0, %%mm2\n\t"
                    "punpckldq %%mm1, %%mm0\n\t"
                    "punpckhdq %%mm1, %%mm2\n\t"
                    "paddd %%mm2, %%mm0\n\t"
                    "paddd %[rnd], %%mm0\n\t"
                    "psrad $15, %%mm0\n\t"
                    "packssdw %%mm0, %%mm0\n\t"
                    "movd %%mm0, %[o]\n\t"
                    : [o] "=m"(*(u32a*)(out + i))
                    : [s0] "m"(*(const u64a*)(in + p0)), [t0] "m"(*(const u64a*)(s_lut + ((f0 >> 8) & 0xFC))),
                      [s1] "m"(*(const u64a*)(in + p1)), [t1] "m"(*(const u64a*)(s_lut + ((f1 >> 8) & 0xFC))),
                      [rnd] "m"(*(const u64a*)k_rnd15)
                    : "mm0", "mm1", "mm2");
            }
            mmx_end();
            pos = base + p;
        }
    }
#endif
    for (; i < n; i++) {
        DS(o + i) = dot4(pos, s_lut + ((frac >> 8) & 0xFC));
        frac += step;
        pos += frac >> 16;
        frac &= 0xFFFF;
    }
    for (i = 0; i < 4; i++) st[i] = DS(pos + i);
    st[4] = (int16_t)frac;
    rd_store(state, st, sizeof st);
}

/* ---- A_ENVMIXER ----------------------------------------------------------- */

typedef struct {
    int32_t value[2], target[2], rate[2], seq[2];
    int16_t dry, wet;
} EnvState;   /* 36 bytes, native; ENVMIX_STATE is 80 */

static void cmd_envmixer(uint32_t flags, uint32_t w1)
{
    EnvState e;
    uint32_t state = resolve(w1), n = align_up(T.count, 16) >> 1, x = T.in >> 1, b, i, k, c;
    uint32_t outs[4], nout = (flags & A_AUX) ? 4 : 2;
    int32_t step[2] = {0, 0};
    int16_t g[4][8];
    int simd = 0;
#if ASP_MMX
    int16_t gp[4][16] __attribute__((aligned(8)));
    int gp_ok = 0;
#endif
    if (flags & A_INIT) {
        for (c = 0; c < 2; c++) {
            e.value[c] = e.seq[c] = (int32_t)((uint32_t)(uint16_t)T.vol[c] << 16);
            e.target[c] = (int32_t)((uint32_t)(uint16_t)T.target[c] << 16);
            e.rate[c] = T.rate[c];
        }
        e.dry = T.dry, e.wet = T.wet;
    } else {
        rd_load(state, &e, sizeof e);
    }
    outs[0] = T.out >> 1, outs[1] = T.dry_right >> 1, outs[2] = T.wet_left >> 1, outs[3] = T.wet_right >> 1;
#if ASP_MMX
    simd = lanes_flat(x, n);
    for (k = 0; k < nout && simd; k++) {
        simd = lanes_flat(outs[k], n) && apart(x, n, outs[k], n);
        for (c = 0; c < k && simd; c++) simd = same_or_apart(outs[c], outs[k], n);
    }
#endif
    for (b = 0; b < n; b += 8) {
        int ramp = 0;
        for (c = 0; c < 2; c++) {
            if (e.value[c] == e.target[c]) {
                step[c] = 0;
            } else {
                int64_t next = ((int64_t)e.seq[c] * e.rate[c]) >> 16;
                e.seq[c] = next > INT32_MAX ? INT32_MAX : next < INT32_MIN ? INT32_MIN : (int32_t)next;
                int64_t d = (int64_t)e.seq[c] - e.value[c];
                step[c] = (int32_t)(d >= 0 ? d >> 3 : -((-d) >> 3));   /* d / 8, truncated, without __divdi3 */
            }
            ramp |= step[c] != 0;
        }
        if (ASP_HLE_ENV_FASTPATH && !ramp) {
            /* no ramp in this block: the 8 gains are the block's first */
            int16_t vl = (int16_t)(e.value[0] >> 16), vr = (int16_t)(e.value[1] >> 16);
            int16_t g0 = sat16(mulq15(vl, e.dry)), g1 = sat16(mulq15(vr, e.dry));
            int16_t g2 = sat16(mulq15(vl, e.wet)), g3 = sat16(mulq15(vr, e.wet));
#if ASP_MMX
            if (gp_ok && gp[0][0] == g0 && gp[1][0] == g1 && gp[2][0] == g2 && gp[3][0] == g3)
                ramp = -1;  /* gp[] already holds these 8 x 4 gains */
#endif
            for (i = 0; i < 8; i++) g[0][i] = g0, g[1][i] = g1, g[2][i] = g2, g[3][i] = g3;
        } else {
            for (i = 0; i < 8; i++) {
                int16_t v[2];
                for (c = 0; c < 2; c++) {
                    if (step[c]) {
                        int64_t nv = (int64_t)e.value[c] + step[c];
                        int reached = step[c] > 0 ? nv >= e.target[c] : nv <= e.target[c];
                        e.value[c] = reached ? e.target[c] : (int32_t)nv;
                        if (reached) step[c] = 0;
                    }
                    v[c] = (int16_t)(e.value[c] >> 16);
                }
                g[0][i] = sat16(mulq15(v[0], e.dry)), g[1][i] = sat16(mulq15(v[1], e.dry));
                g[2][i] = sat16(mulq15(v[0], e.wet)), g[3][i] = sat16(mulq15(v[1], e.wet));
            }
        }
#if ASP_MMX
        if (simd) {
            int16_t* xs = s_dmem.s + ((x + b) & LANE_MASK);
            if (ramp != -1) {
                for (k = 0; k < nout; k++) gain_pairs(gp[k], g[k], 8);
                gp_ok = !ramp;   /* constant across the block: reusable by the next steady block */
            }
            for (k = 0; k < nout; k++) {
                int16_t* d = s_dmem.s + ((outs[k] + b) & LANE_MASK);
                mmx_mix4(d, xs, gp[k]);
                mmx_mix4(d + 4, xs + 4, gp[k] + 8);
            }
            continue;
        }
#endif
        for (i = 0; i < 8; i++) {
            int32_t s = DS(x + b + i);
            for (k = 0; k < nout; k++) {
                int16_t* d = &DS(outs[k] + b + i);
                *d = sat16(*d + mulq15(s, g[k][i]));
            }
        }
    }
#if ASP_MMX
    if (simd && n) mmx_end();
#endif
    (void)simd;
    rd_store(state, &e, sizeof e);
}

/* ---- A_MIXER, A_INTERLEAVE, A_POLEF -------------------------------------- */

static void cmd_mixer(int16_t gain, uint32_t w1)
{
    uint32_t src = (w1 >> 16) >> 1, dst = (w1 & 0xFFFF) >> 1, n = align_up(T.count, 32) >> 1, i;
#if ASP_MMX
    if (n && lanes_flat(src, n) && lanes_flat(dst, n) && same_or_apart(src, dst, n)) {
        int16_t gp[8] __attribute__((aligned(8)));
        const int16_t* s = s_dmem.s + (src & LANE_MASK);
        int16_t* d = s_dmem.s + (dst & LANE_MASK);
        for (i = 0; i < 8; i += 2) gp[i] = gain, gp[i + 1] = 0x4000;
        for (i = 0; i < n; i += 4) mmx_mix4(d + i, s + i, gp);
        mmx_end();
        return;
    }
#endif
    for (i = 0; i < n; i++) DS(dst + i) = sat16(DS(dst + i) + mulq15(DS(src + i), gain));
}

/* DIVERGES: all input is read before the output is written (the RSP works in
 * chunks; the two differ only for overlapping ranges, which libultra avoids);
 * more than 1024 pairs (4 KB of output) is cut to 1024. */
static void cmd_interleave(uint32_t w1)
{
    static int16_t lr[2048];
    uint32_t l = (w1 >> 16) >> 1, r = (w1 & 0xFFFF) >> 1, o = T.out >> 1, n = T.count >> 1, i;
    if (n > 1024) n = 1024;
    for (i = 0; i < n; i++) lr[2 * i] = DS(l + i), lr[2 * i + 1] = DS(r + i);
    for (i = 0; i < 2 * n; i++) DS(o + i) = lr[i];
}

/* Order-2 IIR in the ADPCM matrix form, Q14. Table (A_LOADADPCM, 16 s16):
 * h1[8] weights y[-2], h2[8] weights y[-1] (_init_lpfilter: fc^(i+1)); per
 * group of 8, y[k] = (g x[k] + h1[k] y[-2] + h2[k] y[-1]
 * + sum_{j<k} ((h2[k-1-j] g) >> 14) x[j]) >> 14. State: the last two outputs. */
static void cmd_polef(uint32_t flags, int16_t gain, uint32_t w1)
{
    int16_t st[4] = {0, 0, 0, 0}, h2g[8], y2, y1;
    const int16_t *h1 = T.table, *h2 = T.table + 8;
    uint32_t state = resolve(w1), n = align_up(T.count, 16) >> 4, in = T.in >> 1, o = T.out >> 1, gi;
    int k, j;
    if (!(flags & A_INIT)) rd_load(state, st, sizeof st);
    y2 = st[2], y1 = st[3];
    for (k = 0; k < 8; k++) h2g[k] = (int16_t)((h2[k] * gain) >> 14);
    for (gi = 0; gi < n; gi++) {
        int16_t x[8], y[8];
        for (k = 0; k < 8; k++) x[k] = DS(in + k);
        for (k = 0; k < 8; k++) {
            int64_t acc = (int64_t)x[k] * gain + (int64_t)h1[k] * y2 + (int64_t)h2[k] * y1;
            for (j = 0; j < k; j++) acc += (int64_t)h2g[k - 1 - j] * x[j];
            y[k] = sat16_64(acc >> 14);
        }
        for (k = 0; k < 8; k++) DS(o + k) = y[k];
        y2 = y[6], y1 = y[7];
        in += 8, o += 8;
    }
    st[0] = st[1] = 0, st[2] = y2, st[3] = y1;
    rd_store(state, st, sizeof st);
}

/* ---- dispatch --------------------------------------------------------------- */

static void exec(uint32_t w0, uint32_t w1)
{
    uint32_t op = w0 >> 24, flags = (w0 >> 16) & 0xFF;
    s_stats.cmds[op < 16 ? op : 16]++;
    switch (op) {
    case A_SPNOOP:
        break;
    case A_ADPCM:
        cmd_adpcm(flags, w1);
        break;
    case A_CLEARBUFF:
        dmem_zero(w0 & 0xFFFF, cap_dmem(align_up(w1 & 0xFFFF, 16)));
        break;
    case A_ENVMIXER:
        cmd_envmixer(flags, w1);
        break;
    case A_LOADBUFF: {
        uint32_t n = cap_dmem(align_up(T.count, 8));
        uint8_t* p;
        if (!n) break;
        p = rdram_at(resolve(w1));
        if (p) dmem_from(T.in & ~7u, p, n);
        else dmem_zero(T.in & ~7u, n);
        break;
    }
    case A_RESAMPLE:
        cmd_resample(flags, w0 & 0xFFFF, w1);
        break;
    case A_SAVEBUFF: {
        uint32_t n = cap_dmem(align_up(T.count, 8));
        uint8_t* p;
        if (!n) break;
        p = rdram_at(resolve(w1));
        if (p) dmem_to(p, T.out & ~7u, n);
        break;
    }
    case A_SEGMENT:
        T.seg[(w1 >> 24) & 15] = w1 & 0xFFFFFFu;
        break;
    case A_SETBUFF:
        if (flags & A_AUX) T.dry_right = w0 & 0xFFFF, T.wet_left = w1 >> 16, T.wet_right = w1 & 0xFFFF;
        else T.in = w0 & 0xFFFF, T.out = w1 >> 16, T.count = w1 & 0xFFFF;
        break;
    case A_SETVOL: {
        /* aSetVolume(f, v, t, r): w0 = f << 16 | v, w1 = t << 16 | r */
        int16_t v = (int16_t)w0;
        if (flags & A_AUX) {
            T.dry = v, T.wet = (int16_t)w1;
        } else {
            int ch = (flags & A_LEFT) ? 0 : 1;
            if (flags & A_VOL) T.vol[ch] = v;
            else T.target[ch] = v, T.rate[ch] = (int32_t)w1;
        }
        break;
    }
    case A_DMEMMOVE: {
        static uint8_t tmp[DMEM_SIZE];
        uint32_t n = cap_dmem(align_up(w1 & 0xFFFF, 16));
        dmem_to(tmp, w0 & 0xFFFF, n);       /* DIVERGES: memmove semantics, see cmd_interleave */
        dmem_from(w1 >> 16, tmp, n);
        break;
    }
    case A_LOADADPCM:
        cmd_loadadpcm(w0, w1);
        break;
    case A_MIXER:
        cmd_mixer((int16_t)w0, w1);
        break;
    case A_INTERLEAVE:
        cmd_interleave(w1);
        break;
    case A_POLEF:
        cmd_polef(flags, (int16_t)w0, w1);
        break;
    case A_SETLOOP:
        T.loop = resolve(w1);
        break;
    default:
        break;   /* DIVERGES: the RSP would jump through whatever follows its 16-entry table */
    }
}

void asp_hle_run(const uint32_t* cmds, uint32_t n_cmds, asp_addr_fn addr, void* user)
{
    uint32_t i;
    if (!s_inited) asp_hle_reset();
    memset(&T, 0, sizeof T);
    s_addr = addr, s_user = user;
    s_stats.tasks++;
    for (i = 0; i < n_cmds; i++) exec(cmds[2 * i], cmds[2 * i + 1]);
}

void asp_hle_reset(void)
{
    s_inited = 1;
    memset(&s_dmem, 0, sizeof s_dmem);
    memset(&T, 0, sizeof T);
    memset(&s_stats, 0, sizeof s_stats);
    lut_linear();
}

const asp_hle_stats_t* asp_hle_stats(void) { return &s_stats; }

uint8_t* asp_hle_dmem(void)
{
    if (!s_inited) asp_hle_reset();
    return s_dmem.b;
}
