/* xhw_sha1.c - SHA-1 (FIPS 180-4), incremental.
 *
 * The ROM check hashes the normalised 8 MB image and compares it with the
 * known-good table (xrom_known.h). Plain portable C, written for this
 * project, public domain: no platform calls, so the host test builds it
 * unchanged (tests/xbox/test_rom_check.c). Not for anything secret; SHA-1 is
 * only an identity check here. */
#include <string.h>

#include "xrom.h"

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void block(uint32_t st[5], const uint8_t* p) {
    uint32_t w[80], a, b, c, d, e, t;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) |
               (uint32_t)p[4 * i + 3];
    for (; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = st[0]; b = st[1]; c = st[2]; d = st[3]; e = st[4];
    for (i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
        t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d; st[4] += e;
}

void xsha1_init(xsha1_ctx* c) {
    c->state[0] = 0x67452301u;
    c->state[1] = 0xEFCDAB89u;
    c->state[2] = 0x98BADCFEu;
    c->state[3] = 0x10325476u;
    c->state[4] = 0xC3D2E1F0u;
    c->count_lo = c->count_hi = 0;
    c->buflen = 0;
}

void xsha1_update(xsha1_ctx* c, const void* data, uint32_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t lo = c->count_lo + len;
    if (lo < c->count_lo) c->count_hi++;
    c->count_lo = lo;
    if (c->buflen) {
        uint32_t n = 64 - c->buflen;
        if (n > len) n = len;
        memcpy(c->buf + c->buflen, p, n);
        c->buflen += n;
        p += n;
        len -= n;
        if (c->buflen < 64) return;
        block(c->state, c->buf);
        c->buflen = 0;
    }
    for (; len >= 64; p += 64, len -= 64) block(c->state, p);
    if (len) {
        memcpy(c->buf, p, len);
        c->buflen = len;
    }
}

void xsha1_final(xsha1_ctx* c, uint8_t out[20]) {
    uint8_t pad[72];
    uint32_t n, bits_hi = (c->count_hi << 3) | (c->count_lo >> 29), bits_lo = c->count_lo << 3;
    int i;
    /* 0x80, zeros up to 56 mod 64, then the 64-bit bit count, big-endian */
    n = (c->buflen < 56 ? 56 : 120) - c->buflen;
    memset(pad, 0, sizeof pad);
    pad[0] = 0x80;
    xsha1_update(c, pad, n);
    for (i = 0; i < 4; i++) pad[i] = (uint8_t)(bits_hi >> (24 - 8 * i));
    for (i = 0; i < 4; i++) pad[4 + i] = (uint8_t)(bits_lo >> (24 - 8 * i));
    xsha1_update(c, pad, 8);
    for (i = 0; i < 20; i++) out[i] = (uint8_t)(c->state[i >> 2] >> (24 - 8 * (i & 3)));
}

void xsha1_hex(const uint8_t digest[20], char out[41]) {
    static const char hex[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 20; i++) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 15];
    }
    out[40] = '\0';
}
