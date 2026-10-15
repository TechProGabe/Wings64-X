/* mio0.c - MIO0 decompressor in C (the N64 build uses an asm routine;
 * kernel/filesystem.c calls it for "GZIP" blocks). From wings64-dc's
 * src/dc/ultra/mio0.c, unchanged.
 *
 * Header: "MIO0", u32 out size, u32 compressed-stream offset, u32 raw-stream
 * offset (all big-endian), then a big-endian bitstream at +0x10: bit 1 = copy
 * one raw byte, bit 0 = read a u16 back-reference {len-3:4, dist-1:12} from
 * the compressed stream. Byte-wise reads only, so it is endian-neutral. */
#include "mio0.h"

static inline uint32_t rd_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

uint32_t mio0_out_size(const void* src) {
    const uint8_t* s = src;
    if (s[0] != 'M' || s[1] != 'I' || s[2] != 'O' || s[3] != '0') return 0;
    return rd_be32(s + 4);
}

void mio0_decompress(void* src, uint8_t* dst) {
    const uint8_t* s = src;
    uint32_t out_size = rd_be32(s + 4);
    const uint8_t* bits = s + 0x10;
    const uint8_t* comp = s + rd_be32(s + 8);
    const uint8_t* raw = s + rd_be32(s + 12);
    uint8_t *out = dst, *end = dst + out_size;
    uint32_t word = 0;
    int nbits = 0;

    while (out < end) {
        if (nbits == 0) {
            word = rd_be32(bits);
            bits += 4;
            nbits = 32;
        }
        if (word & 0x80000000u) {
            *out++ = *raw++;
        } else {
            uint32_t v = ((uint32_t)comp[0] << 8) | comp[1];
            uint32_t len = (v >> 12) + 3, dist = (v & 0xFFF) + 1;
            const uint8_t* from = out - dist;
            comp += 2;
            while (len-- && out < end) /* overlapping copy: byte at a time by design */
                *out++ = *from++;
        }
        word <<= 1;
        nbits--;
    }
}
