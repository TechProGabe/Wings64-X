/* test_rom_check.c - host test of the ROM validation's pure functions
 * (xbox/src/hw/xhw_rom.c, xhw_sha1.c; PLAN.md 3.1 step 8).
 *
 * Built by tools/xbox/test_rom_check.py with -DXROM_HOST_TEST. The image is
 * random bytes with a made-up header, never real game data; its SHA-1 can't
 * match the real table, so the checks run against a synthetic "known" entry
 * (xrom_verify_with_table). Exit code 0 when everything passed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xrom.h"

static int s_fail, s_checks;

#define CHECK(cond, ...) do { \
        s_checks++; \
        if (!(cond)) { s_fail++; printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); \
                       printf("\n"); } \
    } while (0)

static uint32_t s_rng = 12345;
static uint32_t rnd(void) { s_rng = s_rng * 1664525u + 1013904223u; return s_rng >> 8; }

/* a z64 image with a plausible header */
static void make_image(uint8_t* img, const char* code, uint8_t version) {
    uint32_t i;
    for (i = 0; i < XROM_SIZE; i++) img[i] = (uint8_t)rnd();
    img[0] = 0x80; img[1] = 0x37; img[2] = 0x12; img[3] = 0x40;
    memcpy(img + 0x20, "Pilot Wings64       ", 20);
    memcpy(img + 0x3B, code, 4);
    img[0x3F] = version;
}

/* z64 -> the other two orders (the inverse of normalise: both are involutions) */
static void to_order(uint8_t* img, uint32_t order) { xrom_normalise(img, XROM_SIZE, order); }

static void sha1_of(const void* d, uint32_t n, char hex[41]) {
    xsha1_ctx c;
    uint8_t out[20];
    xsha1_init(&c);
    xsha1_update(&c, d, n);
    xsha1_final(&c, out);
    xsha1_hex(out, hex);
}

static void test_sha1(void) {
    char hex[41];
    xsha1_ctx c;
    uint8_t out[20], *m;
    uint32_t i;
    sha1_of("abc", 3, hex);
    CHECK(strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0, "abc: %s", hex);
    sha1_of("", 0, hex);
    CHECK(strcmp(hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709") == 0, "empty: %s", hex);
    sha1_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, hex);
    CHECK(strcmp(hex, "84983e441c3bd26ebaae4aa1f95129e5e54670f1") == 0, "two-block: %s", hex);
    xsha1_init(&c);
    for (i = 0; i < 1000000; i++) xsha1_update(&c, "a", 1);
    xsha1_final(&c, out);
    xsha1_hex(out, hex);
    CHECK(strcmp(hex, "34aa973cd4c4daa4f61eeb2bdbad27316534016f") == 0, "million a: %s", hex);
    /* the same bytes in odd-sized pieces give the same digest */
    m = (uint8_t*)malloc(1000);
    for (i = 0; i < 1000; i++) m[i] = (uint8_t)(i * 7);
    sha1_of(m, 1000, hex);
    xsha1_init(&c);
    for (i = 0; i < 1000;) {
        uint32_t n = 1 + (i % 97);
        if (i + n > 1000) n = 1000 - i;
        xsha1_update(&c, m + i, n);
        i += n;
    }
    xsha1_final(&c, out);
    {
        char h2[41];
        xsha1_hex(out, h2);
        CHECK(strcmp(hex, h2) == 0, "pieces: %s vs %s", hex, h2);
    }
    free(m);
}

int main(void) {
    static uint8_t z[XROM_SIZE], work[XROM_SIZE], ref[XROM_SIZE];
    xrom_info info;
    xrom_known known[1];
    char hex[41], want[300];
    uint32_t order, verdict;
    static const char* const names[] = { "", "z64", "v64", "n64" };

    test_sha1();

    make_image(z, "NPWE", 0);
    memcpy(ref, z, XROM_SIZE);
    sha1_of(z, XROM_SIZE, hex);
    known[0].code = "NPWE"; known[0].version = 0; known[0].sha1 = hex; known[0].md5 = "-"; known[0].label = "Test v1.0";

    /* the three byte orders normalise to identical bytes and an identical SHA-1 */
    for (order = XROM_ORDER_Z64; order <= XROM_ORDER_N64; order++) {
        memcpy(work, z, XROM_SIZE);
        to_order(work, order);
        CHECK(order == XROM_ORDER_Z64 || memcmp(work, z, XROM_SIZE) != 0, "%s: conversion changed nothing", names[order]);
        CHECK(xrom_detect_order(work) == order, "%s: order detected as %u", names[order], (unsigned)xrom_detect_order(work));
        verdict = xrom_verify_with_table(work, XROM_SIZE, &info, known, 1);
        CHECK(verdict == XROM_OK, "%s: verdict %u: %s", names[order], (unsigned)verdict, info.message);
        CHECK(memcmp(work, ref, XROM_SIZE) == 0, "%s: normalised bytes differ", names[order]);
        CHECK(strcmp(info.sha1_hex, hex) == 0, "%s: sha1 %s", names[order], info.sha1_hex);
        CHECK(info.order == order, "%s: info.order %u", names[order], (unsigned)info.order);
        CHECK(strcmp(info.title, "Pilot Wings64") == 0, "title \"%s\"", info.title);
        CHECK(strcmp(info.code, "NPWE") == 0 && info.region == 'E' && info.version == 0, "code %s", info.code);
        CHECK(strcmp(info.label, "Test v1.0") == 0, "label %s", info.label);
        CHECK(info.crc1 == (((uint32_t)z[0x10] << 24) | (z[0x11] << 16) | (z[0x12] << 8) | z[0x13]), "crc1 %08x",
              (unsigned)info.crc1);
        CHECK(info.crc2 == (((uint32_t)z[0x14] << 24) | (z[0x15] << 16) | (z[0x16] << 8) | z[0x17]), "crc2 %08x",
              (unsigned)info.crc2);
    }

    /* wrong size: the message names the size, nothing is read */
    memcpy(work, z, XROM_SIZE);
    verdict = xrom_verify_with_table(work, XROM_SIZE - 1, &info, known, 1);
    CHECK(verdict == XROM_BAD_SIZE, "size-1: %u", (unsigned)verdict);
    CHECK(strcmp(info.message, "This is not a Pilotwings 64 ROM (size 8388607 bytes; expected 8 MB).") == 0, "%s",
          info.message);
    verdict = xrom_verify_with_table(work, XROM_SIZE * 2, &info, known, 1);
    CHECK(verdict == XROM_BAD_SIZE &&
              strcmp(info.message, "This is not a Pilotwings 64 ROM (size 16777216 bytes; expected 8 MB).") == 0,
          "overdump: %s", info.message);
    verdict = xrom_verify_with_table(work, 0, &info, known, 1);
    CHECK(verdict == XROM_BAD_SIZE, "empty file: %u", (unsigned)verdict);

    /* header magic */
    memcpy(work, z, XROM_SIZE);
    work[1] = 0x38;
    verdict = xrom_verify_with_table(work, XROM_SIZE, &info, known, 1);
    CHECK(verdict == XROM_BAD_HEADER && strcmp(info.message, "Not an N64 ROM (bad header).") == 0, "magic: %s",
          info.message);

    /* another game */
    memcpy(work, z, XROM_SIZE);
    memcpy(work + 0x3B, "NSMA", 4);
    verdict = xrom_verify_with_table(work, XROM_SIZE, &info, known, 1);
    CHECK(verdict == XROM_WRONG_GAME && strcmp(info.message, "This ROM is not Pilotwings 64 (game code NSMA).") == 0,
          "other game: %s", info.message);

    /* Japanese and European Pilotwings 64 */
    memcpy(work, z, XROM_SIZE);
    memcpy(work + 0x3B, "NPWJ", 4);
    verdict = xrom_verify_with_table(work, XROM_SIZE, &info, known, 1);
    CHECK(verdict == XROM_WRONG_REGION &&
              strcmp(info.message, "Japanese Pilotwings 64 is not supported; the port is built from the US release (NPWE).") == 0,
          "NPWJ: %s", info.message);
    memcpy(work, z, XROM_SIZE);
    memcpy(work + 0x3B, "NPWP", 4);
    verdict = xrom_verify_with_table(work, XROM_SIZE, &info, known, 1);
    CHECK(verdict == XROM_WRONG_REGION &&
              strcmp(info.message, "European Pilotwings 64 is not supported; the port is built from the US release (NPWE).") == 0,
          "NPWP: %s", info.message);

    /* one byte of content, in each byte order */
    for (order = XROM_ORDER_Z64; order <= XROM_ORDER_N64; order++) {
        char bad[41];
        memcpy(work, z, XROM_SIZE);
        work[0x123456] ^= 0x01;
        sha1_of(work, XROM_SIZE, bad);
        to_order(work, order);
        verdict = xrom_verify_with_table(work, XROM_SIZE, &info, known, 1);
        snprintf(want, sizeof want, "Bad or modified dump: SHA-1 is %s, expected %s. Re-dump your cartridge.", bad, hex);
        CHECK(verdict == XROM_BAD_HASH && strcmp(info.message, want) == 0, "%s corrupt: %s", names[order], info.message);
        CHECK(strcmp(info.sha1_hex, bad) == 0, "%s corrupt hash %s", names[order], info.sha1_hex);
    }

    /* right header, but a version the table doesn't list: still a hash failure, with the table's entry as expected */
    memcpy(work, z, XROM_SIZE);
    work[0x3F] = 1;
    verdict = xrom_verify_with_table(work, XROM_SIZE, &info, known, 1);
    CHECK(verdict == XROM_BAD_HASH, "version 1: %u", (unsigned)verdict);

    /* the real table never matches a random image */
    memcpy(work, z, XROM_SIZE);
    verdict = xrom_verify(work, XROM_SIZE, &info);
    CHECK(verdict == XROM_BAD_HASH && strstr(info.message, "ec771aedf54ee1b214c25404fb4ec51cfd43191a") != NULL,
          "real table: %s", info.message);

    /* header parse stops at the title's trailing blanks and shows non-ASCII as dots */
    memcpy(work, z, XROM_SIZE);
    memcpy(work + 0x20, "AB\x01", 3);
    memset(work + 0x23, ' ', 17);
    xrom_parse_header(work, &info);
    CHECK(strcmp(info.title, "AB.") == 0 || strcmp(info.title, "AB") == 0, "title \"%s\"", info.title);

    printf("%d checks, %d failed\n", s_checks, s_fail);
    return s_fail ? 1 : 0;
}
