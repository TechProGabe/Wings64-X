/* test_texconv.c - host test for xbox/src/gfx/texconv.c (synthetic data only, no ROM).
   Hand-computed texel vectors for every format, deswizzle (self-inverse, vs a TMEM simulation, dxt rules),
   texconv_pow2/line_bytes, padding replication, odd sizes, and a randomized comparison against a separate
   per-texel reference converter. Build: see tools/xbox/test_texconv.py. Exit status 0 = pass. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../xbox/src/gfx/texconv.h"

static int failures;
static int checks;

static void expect_u(const char* what, unsigned long got, unsigned long want)
{
    checks++;
    if (got != want) {
        printf("FAIL %s: got 0x%lx want 0x%lx\n", what, got, want);
        failures++;
    }
}

static void expect_mem(const char* what, const void* got, const void* want, size_t n)
{
    checks++;
    if (memcmp(got, want, n)) {
        printf("FAIL %s: memory differs\n", what);
        failures++;
    }
}

static uint32_t rng_state = 0x1234567;
static unsigned rng_next(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}
static void fill_rand(uint8_t* p, size_t n)
{
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)rng_next();
}

/* ---------- independent reference: per texel, straightforward ---------- */

static unsigned ref_e5(unsigned v) { return (v << 3) | (v >> 2); }
static unsigned ref_e4(unsigned v) { return v * 17; }
static unsigned ref_e3(unsigned v) { return (v << 5) | (v << 2) | (v >> 1); }

static uint32_t pack(unsigned a, unsigned r, unsigned g, unsigned b)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* One texel at (x) of a row; bit position computed from the bit offset, big-endian. */
static unsigned ref_bits(const uint8_t* row, unsigned bitpos, unsigned nbits)
{
    unsigned v = 0;
    for (unsigned i = 0; i < nbits; i++) {
        unsigned bp = bitpos + i;
        v = (v << 1) | ((row[bp / 8] >> (7 - bp % 8)) & 1);
    }
    return v;
}

static int ref_texel(const uint8_t* row, unsigned x, n64_fmt_t fmt, n64_siz_t siz, uint32_t* out)
{
    if (fmt == N64FMT_RGBA && siz == N64SIZ_16b) {
        unsigned v = ref_bits(row, x * 16, 16);
        *out = pack(((v >> 0) & 1) * 255, ref_e5((v >> 11) & 31), ref_e5((v >> 6) & 31), ref_e5((v >> 1) & 31));
    } else if (fmt == N64FMT_RGBA && siz == N64SIZ_32b) {
        *out = pack(ref_bits(row, x * 32 + 24, 8), ref_bits(row, x * 32, 8), ref_bits(row, x * 32 + 8, 8),
                    ref_bits(row, x * 32 + 16, 8));
    } else if (fmt == N64FMT_IA && siz == N64SIZ_4b) {
        unsigned i = ref_e3(ref_bits(row, x * 4, 3));
        *out = pack(ref_bits(row, x * 4 + 3, 1) * 255, i, i, i);
    } else if (fmt == N64FMT_IA && siz == N64SIZ_8b) {
        unsigned i = ref_e4(ref_bits(row, x * 8, 4));
        *out = pack(ref_e4(ref_bits(row, x * 8 + 4, 4)), i, i, i);
    } else if (fmt == N64FMT_IA && siz == N64SIZ_16b) {
        unsigned i = ref_bits(row, x * 16, 8);
        *out = pack(ref_bits(row, x * 16 + 8, 8), i, i, i);
    } else if (fmt == N64FMT_I && siz == N64SIZ_4b) {
        unsigned i = ref_e4(ref_bits(row, x * 4, 4));
        *out = pack(i, i, i, i);
    } else if (fmt == N64FMT_I && siz == N64SIZ_8b) {
        unsigned i = ref_bits(row, x * 8, 8);
        *out = pack(i, i, i, i);
    } else {
        return 0;
    }
    return 1;
}

static int ref_convert(const uint8_t* src, unsigned lb, unsigned w, unsigned h, n64_fmt_t fmt, n64_siz_t siz,
                       uint32_t* dst, unsigned dw, unsigned dh)
{
    for (unsigned y = 0; y < dh; y++) {
        for (unsigned x = 0; x < dw; x++) {
            unsigned sx = x < w ? x : w - 1, sy = y < h ? y : h - 1;
            if (!ref_texel(src + (size_t)sy * lb, sx, fmt, siz, &dst[(size_t)y * dw + x])) return 0;
        }
    }
    return 1;
}

/* ---------- tests ---------- */

static void test_vectors(void)
{
    uint32_t d[8];
    static const uint8_t rgba16[] = { 0xF8, 0x01, 0x07, 0xC1, 0x00, 0x3E, 0x84, 0x21 };
    static const uint32_t rgba16_want[] = { 0xFFFF0000u, 0xFF00FF00u, 0x000000FFu, 0xFF848484u };
    expect_u("rgba16 rc", texconv_to_argb8(rgba16, 8, 4, 1, N64FMT_RGBA, N64SIZ_16b, d, 4, 1), 1);
    expect_mem("rgba16", d, rgba16_want, 16);

    static const uint8_t rgba32[] = { 0x11, 0x22, 0x33, 0x44, 0xFF, 0x00, 0x80, 0x01 };
    static const uint32_t rgba32_want[] = { 0x44112233u, 0x01FF0080u };
    expect_u("rgba32 rc", texconv_to_argb8(rgba32, 8, 2, 1, N64FMT_RGBA, N64SIZ_32b, d, 2, 1), 1);
    expect_mem("rgba32", d, rgba32_want, 8);

    /* IA4: nibbles F (i3=7,a=1) 2 (i3=1,a=0) 5 (i3=2,a=1) 0 */
    static const uint8_t ia4[] = { 0xF2, 0x50, 0, 0, 0, 0, 0, 0 };
    static const uint32_t ia4_want[] = { 0xFFFFFFFFu, 0x00242424u, 0xFF494949u, 0x00000000u };
    expect_u("ia4 rc", texconv_to_argb8(ia4, 8, 4, 1, N64FMT_IA, N64SIZ_4b, d, 4, 1), 1);
    expect_mem("ia4", d, ia4_want, 16);

    static const uint8_t ia8[] = { 0x8F, 0x30, 0xFF };
    static const uint32_t ia8_want[] = { 0xFF888888u, 0x00333333u, 0xFFFFFFFFu };
    expect_u("ia8 rc", texconv_to_argb8(ia8, 8, 3, 1, N64FMT_IA, N64SIZ_8b, d, 3, 1), 1);
    expect_mem("ia8", d, ia8_want, 12);

    static const uint8_t ia16[] = { 0x12, 0x34, 0xFF, 0x00 };
    static const uint32_t ia16_want[] = { 0x34121212u, 0x00FFFFFFu };
    expect_u("ia16 rc", texconv_to_argb8(ia16, 8, 2, 1, N64FMT_IA, N64SIZ_16b, d, 2, 1), 1);
    expect_mem("ia16", d, ia16_want, 8);

    static const uint8_t i4[] = { 0x1A, 0x80, 0, 0, 0, 0, 0, 0 };
    static const uint32_t i4_want[] = { 0x11111111u, 0xAAAAAAAAu, 0x88888888u };
    expect_u("i4 rc", texconv_to_argb8(i4, 8, 3, 1, N64FMT_I, N64SIZ_4b, d, 3, 1), 1);
    expect_mem("i4", d, i4_want, 12);

    static const uint8_t i8[] = { 0x7F, 0x00, 0xFF };
    static const uint32_t i8_want[] = { 0x7F7F7F7Fu, 0x00000000u, 0xFFFFFFFFu };
    expect_u("i8 rc", texconv_to_argb8(i8, 8, 3, 1, N64FMT_I, N64SIZ_8b, d, 3, 1), 1);
    expect_mem("i8", d, i8_want, 12);
}

static void test_rejects(void)
{
    uint8_t s[64] = { 0 };
    uint32_t d[64];
    expect_u("ci4", texconv_to_argb8(s, 8, 4, 1, N64FMT_CI, N64SIZ_4b, d, 4, 1), 0);
    expect_u("ci8", texconv_to_argb8(s, 8, 4, 1, N64FMT_CI, N64SIZ_8b, d, 4, 1), 0);
    expect_u("yuv", texconv_to_argb8(s, 8, 4, 1, N64FMT_YUV, N64SIZ_16b, d, 4, 1), 0);
    expect_u("rgba8", texconv_to_argb8(s, 8, 4, 1, N64FMT_RGBA, N64SIZ_8b, d, 4, 1), 0);
    expect_u("ia32", texconv_to_argb8(s, 8, 4, 1, N64FMT_IA, N64SIZ_32b, d, 4, 1), 0);
    expect_u("zero w", texconv_to_argb8(s, 8, 0, 1, N64FMT_I, N64SIZ_8b, d, 4, 1), 0);
    expect_u("zero h", texconv_to_argb8(s, 8, 4, 0, N64FMT_I, N64SIZ_8b, d, 4, 1), 0);
    expect_u("dst narrow", texconv_to_argb8(s, 8, 4, 1, N64FMT_I, N64SIZ_8b, d, 3, 1), 0);
    expect_u("dst short", texconv_to_argb8(s, 8, 4, 2, N64FMT_I, N64SIZ_8b, d, 4, 1), 0);
    expect_u("line short", texconv_to_argb8(s, 8, 9, 1, N64FMT_I, N64SIZ_8b, d, 16, 1), 0);
    expect_u("null src", texconv_to_argb8(0, 8, 4, 1, N64FMT_I, N64SIZ_8b, d, 4, 1), 0);
    expect_u("null dst", texconv_to_argb8(s, 8, 4, 1, N64FMT_I, N64SIZ_8b, 0, 4, 1), 0);
}

static void test_line_pow2(void)
{
    expect_u("lb 1x4b", texconv_line_bytes(1, N64SIZ_4b), 8);
    expect_u("lb 16x4b", texconv_line_bytes(16, N64SIZ_4b), 8);
    expect_u("lb 17x4b", texconv_line_bytes(17, N64SIZ_4b), 16);
    expect_u("lb 6x16b", texconv_line_bytes(6, N64SIZ_16b), 16);
    expect_u("lb 32x16b", texconv_line_bytes(32, N64SIZ_16b), 64);
    expect_u("lb 5x32b", texconv_line_bytes(5, N64SIZ_32b), 24);
    expect_u("lb 8x8b", texconv_line_bytes(8, N64SIZ_8b), 8);
    expect_u("lb 9x8b", texconv_line_bytes(9, N64SIZ_8b), 16);

    expect_u("pow2 0", texconv_pow2(0), 1);
    expect_u("pow2 1", texconv_pow2(1), 1);
    expect_u("pow2 2", texconv_pow2(2), 2);
    expect_u("pow2 3", texconv_pow2(3), 4);
    expect_u("pow2 5", texconv_pow2(5), 8);
    expect_u("pow2 6", texconv_pow2(6), 8);
    expect_u("pow2 33", texconv_pow2(33), 64);
    expect_u("pow2 64", texconv_pow2(64), 64);
    expect_u("pow2 1000", texconv_pow2(1000), 1024);
    expect_u("pow2 1024", texconv_pow2(1024), 1024);
    expect_u("pow2 5000", texconv_pow2(5000), 1024);
}

static void test_deswizzle(void)
{
    uint8_t buf[16], want[16];
    for (int i = 0; i < 16; i++) buf[i] = (uint8_t)i;
    memcpy(want, buf, 8);
    for (int i = 0; i < 4; i++) { want[8 + i] = buf[12 + i]; want[12 + i] = buf[8 + i]; }
    texconv_deswizzle(buf, 8, 2);
    expect_mem("deswizzle 2 rows", buf, want, 16);

    /* self-inverse, multi-word lines, odd row count, vs reference */
    for (unsigned rows = 1; rows <= 7; rows++) {
        for (unsigned words = 1; words <= 5; words++) {
            unsigned lb = words * 8;
            uint8_t a[7 * 40], orig[7 * 40], ref[7 * 40];
            fill_rand(a, rows * lb);
            memcpy(orig, a, rows * lb);
            for (unsigned y = 0; y < rows; y++)
                for (unsigned b = 0; b < lb; b++) {
                    unsigned w = b / 8, k = b % 8;
                    ref[y * lb + b] = (y & 1) ? orig[y * lb + w * 8 + (k + 4) % 8] : orig[y * lb + b];
                }
            texconv_deswizzle(a, lb, rows);
            expect_mem("deswizzle vs ref", a, ref, rows * lb);
            texconv_deswizzle(a, lb, rows);
            expect_mem("deswizzle self-inverse", a, orig, rows * lb);
        }
    }
}

/* Straightforward RDP LOADBLOCK + sample simulation: store word i swapped if ((i*dxt)>>11) is odd;
   read tile row y word w from TMEM word y*words+w, swapped again if y is odd. */
static void ref_dxt(const uint8_t* ram, uint8_t* out, unsigned words, unsigned rows, unsigned dxt)
{
    unsigned n = words * rows;
    uint8_t* tmem = malloc(n * 8);
    for (unsigned i = 0; i < n; i++) {
        unsigned line = (unsigned)(((unsigned long long)i * dxt) >> 11);
        for (unsigned k = 0; k < 8; k++) tmem[i * 8 + k] = ram[i * 8 + ((line & 1) ? (k + 4) % 8 : k)];
    }
    for (unsigned y = 0; y < rows; y++)
        for (unsigned w = 0; w < words; w++)
            for (unsigned k = 0; k < 8; k++)
                out[(y * words + w) * 8 + k] = tmem[(y * words + w) * 8 + ((y & 1) ? (k + 4) % 8 : k)];
    free(tmem);
}

static void test_deswizzle_dxt(void)
{
    static const unsigned dxts[] = { 0, 1, 7, 64, 100, 256, 512, 683, 1024, 2048, 3000, 4095 };
    for (unsigned words = 1; words <= 9; words++) {
        for (unsigned rows = 1; rows <= 8; rows++) {
            for (unsigned di = 0; di < sizeof dxts / sizeof dxts[0]; di++) {
                uint8_t ram[9 * 8 * 8], a[9 * 8 * 8], ref[9 * 8 * 8];
                unsigned n = words * rows * 8;
                fill_rand(ram, n);
                memcpy(a, ram, n);
                ref_dxt(ram, ref, words, rows, dxts[di]);
                texconv_deswizzle_dxt(a, words * 8, rows, dxts[di]);
                expect_mem("dxt vs ref", a, ref, n);
            }
            /* dxt 0 == plain deswizzle */
            uint8_t a[9 * 8 * 8], b[9 * 8 * 8];
            fill_rand(a, words * rows * 8);
            memcpy(b, a, words * rows * 8);
            texconv_deswizzle_dxt(a, words * 8, rows, 0);
            texconv_deswizzle(b, words * 8, rows);
            expect_mem("dxt0 == deswizzle", a, b, words * rows * 8);
            /* matching dxt (the load sized for this line) changes nothing; words a power of two so 2048/words exact */
            if ((words & (words - 1)) == 0) {
                fill_rand(a, words * rows * 8);
                memcpy(b, a, words * rows * 8);
                texconv_deswizzle_dxt(a, words * 8, rows, 2048 / words);
                expect_mem("dxt matching line is a no-op", a, b, words * rows * 8);
            }
        }
    }
}

static void test_padding(void)
{
    /* 3x2 I8 into 8x4 */
    static const uint8_t src[16] = { 1, 2, 3, 0, 0, 0, 0, 0, 4, 5, 6, 0, 0, 0, 0, 0 };
    uint32_t d[8 * 4];
    expect_u("pad rc", texconv_to_argb8(src, 8, 3, 2, N64FMT_I, N64SIZ_8b, d, 8, 4), 1);
    for (unsigned y = 0; y < 4; y++)
        for (unsigned x = 0; x < 8; x++) {
            unsigned sx = x < 3 ? x : 2, sy = y < 2 ? y : 1;
            unsigned v = src[sy * 8 + sx];
            expect_u("pad texel", d[y * 8 + x], v * 0x01010101u);
        }

    /* the padding alpha must replicate too (RGBA16 with a=0 edge) */
    static const uint8_t px[8] = { 0xF8, 0x01, 0x00, 0x3E, 0, 0, 0, 0 };
    uint32_t e[4 * 2];
    expect_u("pad rgba rc", texconv_to_argb8(px, 8, 2, 1, N64FMT_RGBA, N64SIZ_16b, e, 4, 2), 1);
    for (unsigned i = 0; i < 4; i++) {
        expect_u("pad rgba row0", e[i], i == 0 ? 0xFFFF0000u : 0x000000FFu);
        expect_u("pad rgba row1", e[4 + i], e[i]);
    }
}

static void test_random(void)
{
    static const struct { n64_fmt_t f; n64_siz_t s; } fmts[] = {
        { N64FMT_RGBA, N64SIZ_16b }, { N64FMT_RGBA, N64SIZ_32b }, { N64FMT_IA, N64SIZ_4b }, { N64FMT_IA, N64SIZ_8b },
        { N64FMT_IA, N64SIZ_16b },   { N64FMT_I, N64SIZ_4b },     { N64FMT_I, N64SIZ_8b },
    };
    static const unsigned sizes[] = { 1, 2, 3, 5, 6, 7, 8, 9, 12, 15, 16, 20, 31, 32, 33, 40, 64, 100 };
    unsigned nsz = sizeof sizes / sizeof sizes[0];
    for (unsigned f = 0; f < sizeof fmts / sizeof fmts[0]; f++) {
        for (unsigned iter = 0; iter < 300; iter++) {
            unsigned w = sizes[rng_next() % nsz], h = sizes[rng_next() % nsz];
            unsigned dw = (iter & 1) ? texconv_pow2(w) : w + rng_next() % 5;
            unsigned dh = (iter & 2) ? texconv_pow2(h) : h + rng_next() % 5;
            unsigned lb = texconv_line_bytes(w, fmts[f].s) + ((iter & 4) ? 8 : 0);
            uint8_t* src = malloc((size_t)lb * h);
            uint32_t* got = malloc((size_t)dw * dh * 4);
            uint32_t* want = malloc((size_t)dw * dh * 4);
            fill_rand(src, (size_t)lb * h);
            int rc = texconv_to_argb8(src, lb, w, h, fmts[f].f, fmts[f].s, got, dw, dh);
            int rr = ref_convert(src, lb, w, h, fmts[f].f, fmts[f].s, want, dw, dh);
            expect_u("random rc", rc, 1);
            expect_u("random ref rc", rr, 1);
            checks++;
            if (memcmp(got, want, (size_t)dw * dh * 4)) {
                printf("FAIL random fmt %d siz %d %ux%u -> %ux%u\n", fmts[f].f, fmts[f].s, w, h, dw, dh);
                failures++;
            }
            free(src); free(got); free(want);
        }
    }
}

int main(void)
{
    test_vectors();
    test_rejects();
    test_line_pow2();
    test_deswizzle();
    test_deswizzle_dxt();
    test_padding();
    test_random();
    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
