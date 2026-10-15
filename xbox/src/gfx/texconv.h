/* texconv.h - N64 texture -> A8R8G8B8 conversion for the Xbox back end. Pure C, no platform calls.
   Origin: wings64-dc's gfx_texconv.c (same game, Dreamcast port, same maintainer); the TMEM deswizzle
   routines are kept, the PVR 16-bit targets and twiddling are replaced by one 32-bit target. */
#ifndef TEXCONV_H
#define TEXCONV_H

#include <stdint.h>

typedef enum { N64FMT_RGBA = 0, N64FMT_YUV = 1, N64FMT_CI = 2, N64FMT_IA = 3, N64FMT_I = 4 } n64_fmt_t; /* G_IM_FMT_* */
typedef enum { N64SIZ_4b = 0, N64SIZ_8b = 1, N64SIZ_16b = 2, N64SIZ_32b = 3 } n64_siz_t;            /* G_IM_SIZ_* */

/* TMEM line size in bytes for a row of `width` texels: width*bpp/8 rounded up to a multiple of 8. */
unsigned texconv_line_bytes(unsigned width, n64_siz_t siz);

/* Undo the TMEM swizzle in place: on odd rows, swap the two 32-bit halves of each 8-byte group.
   Self-inverse. `line_bytes` must be a multiple of 8. */
void texconv_deswizzle(uint8_t* data, unsigned line_bytes, unsigned rows);

/* General LOADBLOCK odd-line swap for any dxt (see texconv.c). dxt 0 equals texconv_deswizzle;
   a dxt matching the tile line (2048 / words per line) changes nothing. */
void texconv_deswizzle_dxt(uint8_t* data, unsigned line_bytes, unsigned rows, unsigned dxt);

/* Next power of two >= n, clamped to [1, 1024]. The NV2A takes any pow2 down to 1, so unlike the DC
   (minimum 8) tiny textures are not inflated. n == 0 gives 1. */
unsigned texconv_pow2(unsigned n);

/* Convert w x h texels from linear, already-deswizzled big-endian N64 data (row stride line_bytes) into
   dst, a linear dst_w x dst_h array of 0xAARRGGBB. Columns beyond w and rows beyond h REPLICATE the last
   column/row (clamp-mode sampling at the edge stays right; the caller scales s,t by w/dst_w, h/dst_h).
   Formats: RGBA16, RGBA32, IA4, IA8, IA16, I4, I8 (I: rgb = alpha = intensity, as the RDP).
   CI and YUV are unsupported (PW64 uses neither; a TLUT would be a new argument). Bit expansion:
   5 -> 8 bits (v<<3)|(v>>2); 4 -> 8 (v<<4)|v; 3 -> 8 (v<<5)|(v<<2)|(v>>1); 1-bit alpha -> 0 or 255.
   Returns nonzero on success; 0 for an unsupported format, zero size, dst_w < w, dst_h < h, null
   pointers, or line_bytes too short for w texels. */
int texconv_to_argb8(const uint8_t* src, unsigned line_bytes, unsigned w, unsigned h, n64_fmt_t fmt, n64_siz_t siz,
                     uint32_t* dst, unsigned dst_w, unsigned dst_h);

#endif /* TEXCONV_H */
