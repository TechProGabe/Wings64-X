/* texconv.c - N64 texture -> A8R8G8B8 conversion for the Xbox back end. Pure C, no platform calls, so it
   builds with the game triple and on the host for tests/xbox/test_texconv.c.

   Origin: wings64-dc's src/dc/gfx/gfx_texconv.c (the Dreamcast port of the same game). The line size and
   the two deswizzles are carried over; the DC 16-bit PVR targets and twiddle are gone because the NV2A
   takes linear A8R8G8B8 rows (xgfx_tex_create).

   Input is TMEM contents as the interpreter copied them: big-endian, one row per `line_bytes`, odd rows
   half-swapped (undone by texconv_deswizzle*). Conversion is one function per format that handles a whole
   row with no per-texel divide or branch on format; the padding to the power-of-two destination
   replicates the last column and row. Caching and upload belong to the interpreter / back end. */
#include <string.h>
#include "texconv.h"

static unsigned bits_per_texel(n64_siz_t siz)
{
    switch (siz) {
    case N64SIZ_4b:  return 4;
    case N64SIZ_8b:  return 8;
    case N64SIZ_16b: return 16;
    case N64SIZ_32b: return 32;
    }
    return 0;
}

unsigned texconv_line_bytes(unsigned width, n64_siz_t siz)
{
    unsigned bytes = (width * bits_per_texel(siz) + 7) / 8;
    return (bytes + 7) & ~7u;
}

void texconv_deswizzle(uint8_t* data, unsigned line_bytes, unsigned rows)
{
    for (unsigned y = 1; y < rows; y += 2) {
        uint8_t* row = data + (size_t)y * line_bytes;
        for (unsigned g = 0; g + 8 <= line_bytes; g += 8) {
            for (unsigned i = 0; i < 4; i++) {
                uint8_t t = row[g + i];
                row[g + i] = row[g + 4 + i];
                row[g + 4 + i] = t;
            }
        }
    }
}

/* LOADBLOCK writes 64-bit word i to TMEM with its 32-bit halves swapped when the RDP's line counter
   (i * dxt, 11-bit fraction) is on an odd line; sampling swaps back on odd tile rows. So RAM word i shows
   up swapped exactly when those two parities differ. dxt 0 (pre-swizzled data) is texconv_deswizzle;
   a dxt matching the tile line needs nothing; any other dxt (a load sized for a different width) used to
   come out striped on the DC. */
void texconv_deswizzle_dxt(uint8_t* data, unsigned line_bytes, unsigned rows, unsigned dxt)
{
    unsigned words = line_bytes / 8;
    if (!words) return;
    for (unsigned y = 0; y < rows; y++) {
        uint8_t* row = data + (size_t)y * line_bytes;
        for (unsigned w = 0; w < words; w++) {
            uint32_t i = (uint32_t)y * words + w;
            if ((((i * dxt) >> 11) & 1) == (y & 1)) continue;
            for (unsigned b = 0; b < 4; b++) {
                uint8_t t = row[w * 8 + b];
                row[w * 8 + b] = row[w * 8 + 4 + b];
                row[w * 8 + 4 + b] = t;
            }
        }
    }
}

unsigned texconv_pow2(unsigned n)
{
    unsigned p = 1;
    while (p < n && p < 1024)
        p <<= 1;
    return p;
}

#define ARGB(a, r, g, b) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* 4-bit, 3-bit and 5-bit channel -> 8-bit. */
static uint8_t exp4[16], exp3[8], exp5[32];
static int tabs_ready;

static void init_tabs(void)
{
    for (unsigned v = 0; v < 16; v++) exp4[v] = (uint8_t)((v << 4) | v);
    for (unsigned v = 0; v < 8; v++) exp3[v] = (uint8_t)((v << 5) | (v << 2) | (v >> 1));
    for (unsigned v = 0; v < 32; v++) exp5[v] = (uint8_t)((v << 3) | (v >> 2));
    tabs_ready = 1;
}

/* Row converters: each writes exactly w texels of one source row. */
typedef void (*row_fn)(const uint8_t* row, uint32_t* d, unsigned w);

static void row_rgba16(const uint8_t* row, uint32_t* d, unsigned w)
{
    for (unsigned x = 0; x < w; x++) {
        unsigned v = ((unsigned)row[2 * x] << 8) | row[2 * x + 1];
        d[x] = ARGB((v & 1) ? 255 : 0, exp5[v >> 11], exp5[(v >> 6) & 31], exp5[(v >> 1) & 31]);
    }
}

static void row_rgba32(const uint8_t* row, uint32_t* d, unsigned w)
{
    for (unsigned x = 0; x < w; x++) {
        const uint8_t* p = row + 4 * x;
        d[x] = ARGB(p[3], p[0], p[1], p[2]);
    }
}

static uint32_t ia4_texel(unsigned n)
{
    unsigned i = exp3[n >> 1];
    return ARGB((n & 1) ? 255 : 0, i, i, i);
}

static void row_ia4(const uint8_t* row, uint32_t* d, unsigned w)
{
    unsigned x = 0;
    for (; x + 2 <= w; x += 2) {
        unsigned b = row[x >> 1];
        d[x] = ia4_texel(b >> 4);
        d[x + 1] = ia4_texel(b & 15);
    }
    if (x < w) d[x] = ia4_texel(row[x >> 1] >> 4);
}

static void row_ia8(const uint8_t* row, uint32_t* d, unsigned w)
{
    for (unsigned x = 0; x < w; x++) {
        unsigned i = exp4[row[x] >> 4];
        d[x] = ARGB(exp4[row[x] & 15], i, i, i);
    }
}

static void row_ia16(const uint8_t* row, uint32_t* d, unsigned w)
{
    for (unsigned x = 0; x < w; x++) {
        unsigned i = row[2 * x];
        d[x] = ARGB(row[2 * x + 1], i, i, i);
    }
}

static uint32_t i_texel(unsigned i)
{
    return ARGB(i, i, i, i);
}

static void row_i4(const uint8_t* row, uint32_t* d, unsigned w)
{
    unsigned x = 0;
    for (; x + 2 <= w; x += 2) {
        unsigned b = row[x >> 1];
        d[x] = i_texel(exp4[b >> 4]);
        d[x + 1] = i_texel(exp4[b & 15]);
    }
    if (x < w) d[x] = i_texel(exp4[row[x >> 1] >> 4]);
}

static void row_i8(const uint8_t* row, uint32_t* d, unsigned w)
{
    for (unsigned x = 0; x < w; x++)
        d[x] = i_texel(row[x]);
}

int texconv_to_argb8(const uint8_t* src, unsigned line_bytes, unsigned w, unsigned h, n64_fmt_t fmt, n64_siz_t siz,
                     uint32_t* dst, unsigned dst_w, unsigned dst_h)
{
    row_fn fn = 0;
    if (!src || !dst || w == 0 || h == 0 || dst_w < w || dst_h < h)
        return 0;
    if ((w * bits_per_texel(siz) + 7) / 8 > line_bytes)
        return 0;

    if (fmt == N64FMT_RGBA) {
        if (siz == N64SIZ_16b) fn = row_rgba16;
        else if (siz == N64SIZ_32b) fn = row_rgba32;
    } else if (fmt == N64FMT_IA) {
        if (siz == N64SIZ_4b) fn = row_ia4;
        else if (siz == N64SIZ_8b) fn = row_ia8;
        else if (siz == N64SIZ_16b) fn = row_ia16;
    } else if (fmt == N64FMT_I) {
        if (siz == N64SIZ_4b) fn = row_i4;
        else if (siz == N64SIZ_8b) fn = row_i8;
    }
    if (!fn)
        return 0;
    if (!tabs_ready)
        init_tabs();

    for (unsigned y = 0; y < h; y++) {
        uint32_t* d = dst + (size_t)y * dst_w;
        fn(src + (size_t)y * line_bytes, d, w);
        for (unsigned x = w; x < dst_w; x++)
            d[x] = d[w - 1];
    }
    for (unsigned y = h; y < dst_h; y++)
        memcpy(dst + (size_t)y * dst_w, dst + (size_t)(h - 1) * dst_w, dst_w * sizeof(uint32_t));
    return 1;
}
