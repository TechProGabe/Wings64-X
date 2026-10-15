/* xrom.h - ROM validation (PLAN.md section 3.1): the result of looking at the
 * user's Pilotwings 64 image, and the pure functions that produce it.
 *
 * Read by both compile triples (the hw side fills it in, the game side shows
 * it), so it holds only 32-bit scalars and byte arrays: no bit-fields, no
 * 64-bit members, no enums as fields. The functions marked "pure" call
 * nothing but libc string routines and compile on a host with plain C
 * (tests/xbox/test_rom_check.c builds them with -DXROM_HOST_TEST). */
#ifndef XROM_H
#define XROM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XROM_SIZE 8388608u   /* the US cartridge: 64 Mbit */

/* byte order of the file as found (the bytes decide, not the extension) */
enum { XROM_ORDER_UNKNOWN = 0, XROM_ORDER_Z64, XROM_ORDER_V64, XROM_ORDER_N64 };

/* how far the check got and why it stopped */
enum {
    XROM_OK = 0,
    XROM_NO_ROM,       /* no file with an accepted extension */
    XROM_BAD_SIZE,     /* not exactly 8 MB */
    XROM_BAD_HEADER,   /* first word is none of the three byte orders */
    XROM_WRONG_GAME,   /* a valid N64 header, another game */
    XROM_WRONG_REGION, /* NPWJ / NPWP */
    XROM_BAD_HASH,     /* Pilotwings 64 (NPWE) but not the known dump */
    XROM_UNREADABLE    /* the file can't be opened or read */
};

typedef struct xrom_info {
    uint32_t order;       /* XROM_ORDER_* */
    uint32_t verdict;     /* XROM_* */
    uint32_t size;        /* file size in bytes */
    uint32_t region;      /* byte 0x3E: 'E', 'J', 'P' */
    uint32_t version;     /* byte 0x3F */
    uint32_t crc1;        /* header 0x10 */
    uint32_t crc2;        /* header 0x14 */
    char title[24];       /* header 0x20, 20 chars, NUL-terminated, trailing blanks cut */
    char code[8];         /* header 0x3B..0x3E, 4 chars + NUL */
    uint8_t sha1[20];     /* of the normalised image */
    char sha1_hex[44];    /* the same, 40 lowercase hex + NUL */
    char label[24];       /* the known entry that matched, e.g. "US v1.0" */
    char file[64];        /* the file name on D: */
    char message[256];    /* the user-facing reason for a failed verdict */
} xrom_info;

/* ---- SHA-1 (xhw_sha1.c), incremental ---- */
typedef struct xsha1_ctx {
    uint32_t state[5];
    uint32_t count_lo, count_hi;   /* bytes hashed so far */
    uint32_t buflen;
    uint8_t buf[64];
} xsha1_ctx;
void xsha1_init(xsha1_ctx* c);
void xsha1_update(xsha1_ctx* c, const void* data, uint32_t len);
void xsha1_final(xsha1_ctx* c, uint8_t out[20]);
void xsha1_hex(const uint8_t digest[20], char out[41]);

/* ---- known-good table (xrom_known.h holds the real one) ---- */
typedef struct xrom_known {
    const char* code;      /* game code, "NPWE" */
    uint32_t version;
    const char* sha1;      /* 40 lowercase hex */
    const char* md5;       /* for the docs; not checked */
    const char* label;     /* "US v1.0" */
} xrom_known;

/* ---- pure functions (xhw_rom.c) ---- */
/* From the first four bytes of the file. */
uint32_t xrom_detect_order(const uint8_t first4[4]);
/* In place: v64 swaps every byte pair, n64 reverses every 4 bytes; z64 is left alone. */
void xrom_normalise(uint8_t* rom, uint32_t size, uint32_t order);
/* Title, code, region, version, CRCs from a normalised image of at least 0x40 bytes. */
void xrom_parse_header(const uint8_t* rom, xrom_info* out);
/* Step 2: sets verdict/message when size isn't XROM_SIZE. Returns the verdict. */
uint32_t xrom_check_size(xrom_info* info, uint32_t size);
/* Steps 4 and 5 on a parsed header (info->order set by detect). Returns the verdict. */
uint32_t xrom_check_header(xrom_info* info);
/* Step 6: info->sha1 against a table. Returns the verdict. */
uint32_t xrom_check_hash(xrom_info* info, const xrom_known* table, int count);
/* The whole thing on a file image held in `buf` (normalised in place). */
uint32_t xrom_verify_with_table(uint8_t* buf, uint32_t size, xrom_info* info, const xrom_known* table, int count);
uint32_t xrom_verify(uint8_t* buf, uint32_t size, xrom_info* info);   /* with xrom_known.h's table */
/* "size", "bad header", ... for lastexit.txt */
const char* xrom_verdict_name(uint32_t verdict);

#ifndef XROM_HOST_TEST
/* ---- the Xbox part (xhw_rom.c): find, load, check ---- */
/* The normalised 8 MB image in RAM (z64 byte order), or never returns: a card,
 * then the dashboard. */
const uint8_t* xhw_rom_load_verified(xrom_info* out);
const xrom_info* xhw_rom_info(void);   /* what the last successful load found */
#endif

#ifdef __cplusplus
}
#endif
#endif
