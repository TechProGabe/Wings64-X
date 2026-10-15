/* test_swap.c - host test for xbox/src/game/rom/pw64_swap.c (synthetic buffers only, no ROM).
   pw64_swap turns big-endian ROM structs into the host (little-endian) layout in place, described by
   a layout string: `[N]c` items, c = b keep / h 2-byte / w 4-byte / d 8-byte, spaces ignored. Covered:
   each letter alone, a hand-built big-endian record checked against host values, counts and strides,
   repeat counts, spaces, independent reference reversal over random records, involution, count 0,
   and every fatal path (total != stride, bad character, trailing count, more than 32 items).
   "Host value" means the little-endian reading the Xbox (and this host) uses. The fatal hook is a
   longjmp stub. Build and run: tools/xbox/test_swap.py. Exit status 0 = pass. */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The prelude's declarations (xbox/include/game/wings64_prelude.h). */
__attribute__((noreturn)) void pw64_fatal(const char* msg, unsigned int value);
void pw64_swap(void* p, unsigned int count, unsigned int stride, const char* layout);

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

/* Fatal stub: count the hit and jump back to the test. */
static jmp_buf fatal_jb;
static int fatal_hits;
static unsigned fatal_value;

void pw64_fatal(const char* msg, unsigned int value)
{
    (void)msg;
    fatal_hits++;
    fatal_value = value;
    longjmp(fatal_jb, 1);
}

#define EXPECT_FATAL(stmt, value_want, what)                                   \
    do {                                                                       \
        int hit = 0;                                                           \
        fatal_hits = 0;                                                        \
        fatal_value = 0;                                                       \
        if (setjmp(fatal_jb) == 0) {                                           \
            stmt;                                                              \
        } else {                                                               \
            hit = 1;                                                           \
        }                                                                      \
        CHECK(hit && fatal_hits == 1 && fatal_value == (unsigned)(value_want), what); \
    } while (0)

static uint32_t rng_state = 0x0BADF00Du;
static unsigned rng_next(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

static void put_be(uint8_t* p, uint64_t v, unsigned n)
{
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * (n - 1 - i)));
}

static uint64_t get_le(const uint8_t* p, unsigned n)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < n; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

static void reverse(uint8_t* p, unsigned n)
{
    for (unsigned a = 0, b = n - 1; a < b; a++, b--) {
        uint8_t t = p[a];
        p[a] = p[b];
        p[b] = t;
    }
}

/* The record used by most cases: "4b 6w h 2b", 32 bytes. Offsets: tag 0, w[6] 4..27, h 28, tail 30. */
enum { REC = 32 };

/* Independent reference for one record: reverse each w and the h, keep the tag bytes. */
static void ref_record(uint8_t* r)
{
    for (unsigned i = 0; i < 6; i++) reverse(r + 4 + 4 * i, 4);
    reverse(r + 28, 2);
}

static void test_letters(void)
{
    uint8_t w[4] = {0x11, 0x22, 0x33, 0x44};
    pw64_swap(w, 1, 4, "w");
    CHECK(memcmp(w, (const uint8_t[]){0x44, 0x33, 0x22, 0x11}, 4) == 0, "w swaps 4 bytes");
    CHECK(get_le(w, 4) == 0x11223344u, "w host value is 0x11223344");

    uint8_t h[2] = {0x11, 0x22};
    pw64_swap(h, 1, 2, "h");
    CHECK(get_le(h, 2) == 0x1122u, "h host value is 0x1122");

    uint8_t d[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    pw64_swap(d, 1, 8, "d");
    CHECK(get_le(d, 8) == 0x0102030405060708ull, "d host value is 0x0102030405060708");

    uint8_t b[3] = {0xA1, 0xB2, 0xC3};
    pw64_swap(b, 1, 3, "3b");
    CHECK(b[0] == 0xA1 && b[1] == 0xB2 && b[2] == 0xC3, "b items are kept as they are");

    /* "1w" and "w" are the same layout */
    uint8_t w1[4] = {0x01, 0x02, 0x03, 0x04}, w2[4] = {0x01, 0x02, 0x03, 0x04};
    pw64_swap(w1, 1, 4, "1w");
    pw64_swap(w2, 1, 4, "w");
    CHECK(memcmp(w1, w2, 4) == 0, "1w and w agree");
}

static void test_hand_record(void)
{
    static const uint32_t w_host[6] = {0x00000001u, 0x12345678u, 0xDEADBEEFu, 0x80000000u, 0x0000FFFFu, 0xCAFEBABEu};
    uint8_t r[REC];
    memcpy(r, "WING", 4);
    for (unsigned i = 0; i < 6; i++) put_be(r + 4 + 4 * i, w_host[i], 4);
    put_be(r + 28, 0xBEEFu, 2);
    r[30] = 'A';
    r[31] = 'Z';

    pw64_swap(r, 1, REC, "4b 6w h 2b");

    CHECK(memcmp(r, "WING", 4) == 0, "record: tag bytes kept");
    for (unsigned i = 0; i < 6; i++) {
        char what[64];
        snprintf(what, sizeof what, "record: w[%u] host value", i);
        CHECK(get_le(r + 4 + 4 * i, 4) == w_host[i], what);
    }
    CHECK(get_le(r + 28, 2) == 0xBEEFu, "record: h host value");
    CHECK(r[30] == 'A' && r[31] == 'Z', "record: tail bytes kept");
}

static void test_spaces_ignored(void)
{
    uint8_t a[REC], b[REC];
    for (unsigned i = 0; i < REC; i++) a[i] = b[i] = (uint8_t)rng_next();
    pw64_swap(a, 1, REC, "4b 6w h 2b");
    pw64_swap(b, 1, REC, "4b6wh2b");
    CHECK(memcmp(a, b, REC) == 0, "spaces in the layout change nothing");
}

static void test_random_records(void)
{
    enum { N = 200 };
    static uint8_t buf[N * REC], orig[N * REC], ref[N * REC];
    for (size_t i = 0; i < sizeof buf; i++) buf[i] = orig[i] = (uint8_t)rng_next();
    memcpy(ref, orig, sizeof ref);
    for (unsigned k = 0; k < N; k++) ref_record(ref + k * REC);

    pw64_swap(buf, N, REC, "4b 6w h 2b");
    CHECK(memcmp(buf, ref, sizeof buf) == 0, "200 random records match the reference reversal");

    pw64_swap(buf, N, REC, "4b 6w h 2b");
    CHECK(memcmp(buf, orig, sizeof buf) == 0, "swapping twice restores the records");
}

static void test_count_and_stride(void)
{
    /* "2h 1w" is 8 bytes: three elements, each reversed field by field. */
    enum { N = 3, S = 8 };
    uint8_t buf[N * S], ref[N * S];
    for (size_t i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)rng_next();
    memcpy(ref, buf, sizeof ref);
    for (unsigned k = 0; k < N; k++) {
        reverse(ref + k * S, 2);
        reverse(ref + k * S + 2, 2);
        reverse(ref + k * S + 4, 4);
    }
    pw64_swap(buf, N, S, "2h 1w");
    CHECK(memcmp(buf, ref, sizeof buf) == 0, "\"2h 1w\" over 3 elements of stride 8");

    /* "2d" is 16 bytes: the d items reverse 8 bytes each. */
    uint8_t d[16];
    for (unsigned i = 0; i < 16; i++) d[i] = (uint8_t)(i + 1);
    pw64_swap(d, 1, 16, "2d");
    CHECK(get_le(d, 8) == 0x0102030405060708ull, "2d: first d host value");
    CHECK(get_le(d + 8, 8) == 0x090A0B0C0D0E0F10ull, "2d: second d host value");

    /* "3w" repeat count: three w fields in 12 bytes. */
    uint8_t t[12];
    put_be(t, 0x01020304u, 4);
    put_be(t + 4, 0x05060708u, 4);
    put_be(t + 8, 0x090A0B0Cu, 4);
    pw64_swap(t, 1, 12, "3w");
    CHECK(get_le(t, 4) == 0x01020304u && get_le(t + 4, 4) == 0x05060708u && get_le(t + 8, 4) == 0x090A0B0Cu,
          "3w repeat count reverses three words");
}

static void test_count_zero(void)
{
    uint8_t buf[REC], orig[REC];
    for (unsigned i = 0; i < REC; i++) buf[i] = orig[i] = (uint8_t)rng_next();
    pw64_swap(buf, 0, REC, "4b 6w h 2b");
    CHECK(memcmp(buf, orig, REC) == 0, "count 0 touches nothing");
}

static void test_fatal_paths(void)
{
    uint8_t buf[REC * 2], orig[REC * 2];
    for (unsigned i = 0; i < sizeof buf; i++) buf[i] = orig[i] = (uint8_t)rng_next();

    /* total 32 against stride 31: fatal, value is (total << 16) | stride, and nothing was swapped. */
    EXPECT_FATAL(pw64_swap(buf, 1, 31, "4b 6w h 2b"), (32u << 16) | 31u, "stride 31 for a 32-byte layout is fatal");
    CHECK(memcmp(buf, orig, sizeof buf) == 0, "fatal on mismatch leaves the data alone");

    /* stride 33 */
    EXPECT_FATAL(pw64_swap(buf, 1, 33, "4b 6w h 2b"), (32u << 16) | 33u, "stride 33 for a 32-byte layout is fatal");

    /* "3w" is 12 bytes against stride 8 */
    EXPECT_FATAL(pw64_swap(buf, 1, 8, "3w"), (12u << 16) | 8u, "\"3w\" against stride 8 is fatal");

    /* unknown character */
    EXPECT_FATAL(pw64_swap(buf, 1, 4, "4x"), 'x', "unknown layout character is fatal");

    /* a count with no item after it */
    EXPECT_FATAL(pw64_swap(buf, 1, 5, "4b 3"), 3u, "trailing count is fatal");

    /* 33 items: one more than MAX_ITEMS (32) */
    char many[34 * 2 + 1];
    for (unsigned i = 0; i < 33; i++) {
        many[2 * i] = 'b';
        many[2 * i + 1] = ' ';
    }
    many[66] = 0;
    EXPECT_FATAL(pw64_swap(buf, 1, 33, many), 32u, "33 layout items is fatal");

    /* exactly 32 items is still legal */
    char max32[32 * 2 + 1];
    for (unsigned i = 0; i < 32; i++) {
        max32[2 * i] = 'b';
        max32[2 * i + 1] = ' ';
    }
    max32[64] = 0;
    fatal_hits = 0;
    pw64_swap(buf, 1, 32, max32);
    CHECK(fatal_hits == 0, "32 layout items is not fatal");
}

int main(void)
{
    test_letters();
    test_hand_record();
    test_spaces_ignored();
    test_random_records();
    test_count_and_stride();
    test_count_zero();
    test_fatal_paths();

    printf("%s: %d checks, %d failures\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
