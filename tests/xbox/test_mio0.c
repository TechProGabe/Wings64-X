/* test_mio0.c - host test for xbox/src/game/ultra/mio0.c (synthetic streams only, no ROM).
   The test carries its own reference MIO0 writer (greedy LZ, window 4096, length 3..18) and an op-list
   assembler, so streams are built both from data and by hand. Covered: a hand-written byte image, all
   literals, long back-reference runs, overlapping copies (distance 1 and 2), the maximum distance 4096,
   sizes that are not a multiple of 8 or 32, a truncated declared size, an empty stream, the raw stream
   placed before the compressed stream, the big-endian size reader, and random round trips.
   Build and run: tools/xbox/test_mio0.py. Exit status 0 = pass. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mio0.h"

#define GUARD 16
#define WIN 4096
#define MAXLEN 18

static int failures;
static int checks;

static void check(int ok, const char* what, int line)
{
    checks++;
    if (!ok) {
        printf("FAIL line %d: %s\n", line, what);
        failures++;
    }
}
#define CHECK(ok, what) check((ok), (what), __LINE__)

static uint32_t rng_state = 0x2545F491u;
static unsigned rng_next(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

/* ---------- op list and assembler (reference MIO0 writer) ---------- */

typedef struct {
    int is_ref;
    uint8_t lit;
    unsigned len, dist;
} op_t;

#define LIT(b) ((op_t){0, (uint8_t)(b), 0, 0})
#define REF(l, d) ((op_t){1, 0, (unsigned)(l), (unsigned)(d)})

static void put_be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Serialise ops as a MIO0 image. Layout bits are MSB-first in big-endian 32-bit words (bit 1 = literal),
   padded to a whole word. raw_first places the literal stream before the back-reference stream. */
static uint8_t* assemble(const op_t* ops, size_t nops, uint32_t declared, int raw_first, size_t* out_len)
{
    size_t bits_len = ((nops + 31) / 32) * 4, n_comp = 0, n_raw = 0;
    for (size_t i = 0; i < nops; i++) {
        if (ops[i].is_ref) {
            CHECK(ops[i].len >= 3 && ops[i].len <= MAXLEN, "assemble: ref length out of 3..18");
            CHECK(ops[i].dist >= 1 && ops[i].dist <= WIN, "assemble: ref distance out of 1..4096");
            n_comp += 2;
        } else {
            n_raw++;
        }
    }
    size_t comp_off = 0x10 + bits_len, raw_off;
    if (raw_first) {
        raw_off = comp_off;
        comp_off = raw_off + n_raw;
    } else {
        raw_off = comp_off + n_comp;
    }
    size_t total = 0x10 + bits_len + n_comp + n_raw;
    uint8_t* buf = calloc(total, 1);
    memcpy(buf, "MIO0", 4);
    put_be32(buf + 4, declared);
    put_be32(buf + 8, (uint32_t)comp_off);
    put_be32(buf + 12, (uint32_t)raw_off);
    size_t ci = comp_off, ri = raw_off;
    for (size_t i = 0; i < nops; i++) {
        if (!ops[i].is_ref) {
            buf[0x10 + i / 8] |= (uint8_t)(0x80 >> (i % 8));
            buf[ri++] = ops[i].lit;
        } else {
            uint32_t v = ((uint32_t)(ops[i].len - 3) << 12) | (ops[i].dist - 1);
            buf[ci++] = (uint8_t)(v >> 8);
            buf[ci++] = (uint8_t)v;
        }
    }
    *out_len = total;
    return buf;
}

/* Greedy reference encoder: longest match among the last 4096 bytes (nearest wins ties), length 3..18.
   allow_refs = 0 gives a literal-only stream. Returns the op count. */
static size_t greedy_ops(const uint8_t* in, size_t n, op_t* ops, int allow_refs)
{
    size_t k = 0, i = 0;
    while (i < n) {
        unsigned best = 0, bestd = 0;
        size_t maxl = n - i < MAXLEN ? n - i : MAXLEN;
        if (allow_refs) {
            for (size_t d = 1; d <= WIN && d <= i; d++) {
                size_t l = 0;
                while (l < maxl && in[i + l] == in[i - d + l]) l++;
                if (l >= 3 && l > best) {
                    best = (unsigned)l;
                    bestd = (unsigned)d;
                }
            }
        }
        if (best >= 3) {
            ops[k++] = REF(best, bestd);
            i += best;
        } else {
            ops[k++] = LIT(in[i]);
            i++;
        }
    }
    return k;
}

static size_t ops_bytes(const op_t* ops, size_t nops)
{
    size_t n = 0;
    for (size_t i = 0; i < nops; i++) n += ops[i].is_ref ? ops[i].len : 1;
    return n;
}

/* ---------- decode and compare ---------- */

/* Decodes img (copied, since the decoder takes a non-const source) into a buffer with guard bands and
   compares the first want_len bytes with want. The guard bands must be untouched. */
static void decode_check(const char* what, const uint8_t* img, size_t img_len, const uint8_t* want, size_t want_len)
{
    uint8_t* src = malloc(img_len);
    memcpy(src, img, img_len);
    uint8_t* mem = malloc(want_len + 2 * GUARD);
    memset(mem, 0xA5, want_len + 2 * GUARD);
    uint8_t* dst = mem + GUARD;
    mio0_decompress(src, dst);
    CHECK(memcmp(dst, want, want_len) == 0, what);
    int guard_ok = 1;
    for (size_t i = 0; i < GUARD; i++) {
        if (mem[i] != 0xA5 || dst[want_len + i] != 0xA5) guard_ok = 0;
    }
    CHECK(guard_ok, "decoder wrote outside the output buffer");
    free(src);
    free(mem);
}

/* Assembles ops, checks the header size, decodes, and compares with the output the ops describe. */
static void ops_case(const char* what, const op_t* ops, size_t nops, int raw_first)
{
    size_t n = ops_bytes(ops, nops), img_len = 0;
    uint8_t* want = malloc(n ? n : 1);
    size_t w = 0;
    for (size_t i = 0; i < nops; i++) {
        if (!ops[i].is_ref) {
            want[w++] = ops[i].lit;
        } else {
            for (unsigned k = 0; k < ops[i].len; k++, w++) want[w] = want[w - ops[i].dist];
        }
    }
    uint8_t* img = assemble(ops, nops, (uint32_t)n, raw_first, &img_len);
    CHECK(mio0_out_size(img) == n, what);
    decode_check(what, img, img_len, want, n);
    free(img);
    free(want);
}

/* Greedy-encodes data and round-trips it. */
static void roundtrip(const char* what, const uint8_t* data, size_t n, int raw_first)
{
    op_t* ops = malloc((n ? n : 1) * sizeof(op_t));
    size_t k = greedy_ops(data, n, ops, 1);
    size_t img_len = 0;
    uint8_t* img = assemble(ops, k, (uint32_t)n, raw_first, &img_len);
    decode_check(what, img, img_len, data, n);
    free(img);
    free(ops);
}

static void fill_rand(uint8_t* p, size_t n, unsigned alphabet)
{
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(rng_next() % alphabet);
}

int main(void)
{
    /* 1. Hand-written image bytes, no assembler: "ABCABCABC" = L A, L B, L C, copy 6 from distance 3. */
    {
        static const uint8_t img[] = {
            'M', 'I', 'O', '0', 0, 0, 0, 9,     /* out size 9 */
            0, 0, 0, 0x14,                       /* compressed stream at 0x14 */
            0, 0, 0, 0x16,                       /* raw stream at 0x16 */
            0xE0, 0, 0, 0,                       /* bits 1 1 1 0: literal, literal, literal, reference */
            0x30, 0x02,                          /* reference: len-3 = 3, dist-1 = 2 */
            'A', 'B', 'C'
        };
        static const uint8_t want[] = "ABCABCABC";
        CHECK(mio0_out_size(img) == 9, "hand image: out size");
        decode_check("hand image ABCABCABC", img, sizeof img, want, 9);
    }

    /* 2. Overlapping copy, distance 1: 'x' then 18 copies at distance 1 gives 19 'x' (19 is not a multiple of 8). */
    {
        op_t ops[] = {LIT('x'), REF(18, 1)};
        ops_case("overlap distance 1, 19 bytes", ops, 2, 0);
    }

    /* 3. Overlapping copy, distance 2: "ab" then 7 bytes at distance 2 gives "ababababa". */
    {
        op_t ops[] = {LIT('a'), LIT('b'), REF(7, 2)};
        ops_case("overlap distance 2", ops, 3, 0);
    }

    /* 4. Maximum distance 4096 with length 18, then length 3, both from a 4096-byte literal prefix. */
    {
        enum { FILL = 4096 };
        static op_t ops[FILL + 2];
        static uint8_t fill[FILL];
        fill_rand(fill, FILL, 256);
        for (size_t i = 0; i < FILL; i++) ops[i] = LIT(fill[i]);
        ops[FILL] = REF(MAXLEN, WIN);
        ops[FILL + 1] = REF(3, WIN);
        ops_case("max distance 4096, length 18 then 3", ops, FILL + 2, 0);
        ops_case("max distance 4096, raw stream placed first", ops, FILL + 2, 1);
    }

    /* 5. All literals: every layout bit is set and the output equals the input. */
    {
        enum { N = 1000 };
        uint8_t data[N];
        fill_rand(data, N, 256);
        op_t ops[N];
        size_t k = greedy_ops(data, N, ops, 0);
        CHECK(k == N, "literal-only encoder emitted a reference");
        size_t img_len = 0;
        uint8_t* img = assemble(ops, k, N, 0, &img_len);
        int all_literal = 1;
        for (size_t i = 0; i < N; i++)
            if (!(img[0x10 + i / 8] & (0x80 >> (i % 8)))) all_literal = 0;
        CHECK(all_literal, "all-literal stream has a clear layout bit");
        decode_check("all literals, 1000 bytes", img, img_len, data, N);
        free(img);
    }

    /* 6. Long back-reference runs: 64 KB of zeros is one literal then distance-1 copies of 18. */
    {
        enum { N = 65536 };
        uint8_t* data = calloc(N, 1);
        roundtrip("64 KB of zeros", data, N, 0);
        free(data);
    }

    /* 7. Periodic data (distances 2, 3, 17) and a block repeated beyond the window (greedy must fall back). */
    {
        enum { N = 9000 };
        uint8_t* data = malloc(N);
        for (size_t i = 0; i < N; i++) data[i] = (uint8_t)"ab"[i % 2];
        roundtrip("period 2", data, N, 0);
        for (size_t i = 0; i < N; i++) data[i] = (uint8_t)"abc"[i % 3];
        roundtrip("period 3, raw first", data, N, 1);
        for (size_t i = 0; i < N; i++) data[i] = (uint8_t)(i % 17);
        roundtrip("period 17", data, N, 0);
        for (size_t i = 0; i < 5000; i++) data[i] = (uint8_t)rng_next();
        memcpy(data + 5000, data, 4000); /* copy at distance 5000: outside the 4096 window */
        roundtrip("repeat beyond window", data, N, 0);
        free(data);
    }

    /* 8. Sizes 1..70 (not multiples of 8 or 32) over a small alphabet, so most sizes contain matches. */
    {
        uint8_t data[80];
        for (int round = 0; round < 3; round++) {
            fill_rand(data, sizeof data, 3);
            for (size_t n = 1; n <= 70; n++) roundtrip("sizes 1..70", data, n, round & 1);
        }
    }

    /* 9. Declared size shorter than the stream: exactly the declared bytes are written, nothing past them. */
    {
        op_t ops[] = {LIT('q'), REF(18, 1)};
        size_t img_len = 0;
        uint8_t* img = assemble(ops, 2, 7, 0, &img_len);
        static const uint8_t want[] = "qqqqqqq";
        decode_check("declared size 7 of 19", img, img_len, want, 7);
        free(img);
    }

    /* 10. Empty stream: zero-size output, header only. */
    {
        size_t img_len = 0;
        uint8_t* img = assemble(NULL, 0, 0, 0, &img_len);
        CHECK(img_len == 0x10, "empty image is the 16-byte header");
        decode_check("zero size", img, img_len, (const uint8_t*)"", 0);
        free(img);
    }

    /* 11. Header reader: big-endian size; a wrong magic gives 0. */
    {
        static const uint8_t good[] = {'M', 'I', 'O', '0', 0x01, 0x02, 0x03, 0x04};
        static const uint8_t bad[] = {'M', 'I', 'O', '1', 0x01, 0x02, 0x03, 0x04};
        CHECK(mio0_out_size(good) == 0x01020304u, "mio0_out_size is big-endian");
        CHECK(mio0_out_size(bad) == 0, "mio0_out_size rejects a wrong magic");
    }

    /* 12. Random round trips: alphabets 2, 4, 16 and 256, sizes 1..16384, both stream orders. */
    {
        static const unsigned alphabets[] = {2, 4, 16, 256};
        for (int t = 0; t < 300; t++) {
            size_t n = 1 + rng_next() % 16384;
            uint8_t* data = malloc(n);
            fill_rand(data, n, alphabets[t % 4]);
            roundtrip("random round trip", data, n, t & 1);
            free(data);
        }
    }

    printf("%s: %d checks, %d failures\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
