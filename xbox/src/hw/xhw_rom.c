/* xhw_rom.c - find, load and validate the user's Pilotwings 64 ROM (PLAN.md 3.1).
 *
 * The port ships no game data: the ROM is the user's own, next to default.xbe.
 * Nothing runs on an image that has not passed the SHA-1 check against
 * xrom_known.h, so a bad dump, another game or a Japanese/European release
 * stops here with a card that says what was found and what is expected.
 *
 * Two halves. The pure functions (detect order, normalise, parse header,
 * check size/header/hash) call no kernel routines and build on a host with
 * -DXROM_HOST_TEST: tests/xbox/test_rom_check.c drives every verdict through
 * them. The Xbox half (find the file in D:\, read it in 256 KB pieces under
 * the splash's load bar, hash, report) is behind #ifndef XROM_HOST_TEST.
 *
 * The extension is only a hint to find the file; the first four bytes decide
 * the byte order (.z64 as is, .v64 swapped pairs, .n64 reversed words) and
 * the image is normalised in RAM to z64, which is what the game reads and
 * what the hash is of. */
#include <stdio.h>
#include <string.h>

#include "xrom.h"
#include "xrom_known.h"

#ifndef XROM_HOST_TEST
#include <stdarg.h>
#include <stdlib.h>
#include <windows.h>

#include "xhw.h"
#include "xhw_internal.h"
#endif

/* ---- pure part ---- */

uint32_t xrom_detect_order(const uint8_t b[4]) {
    if (b[0] == 0x80 && b[1] == 0x37 && b[2] == 0x12 && b[3] == 0x40) return XROM_ORDER_Z64;
    if (b[0] == 0x37 && b[1] == 0x80 && b[2] == 0x40 && b[3] == 0x12) return XROM_ORDER_V64;
    if (b[0] == 0x40 && b[1] == 0x12 && b[2] == 0x37 && b[3] == 0x80) return XROM_ORDER_N64;
    return XROM_ORDER_UNKNOWN;
}

void xrom_normalise(uint8_t* rom, uint32_t size, uint32_t order) {
    uint32_t i;
    uint8_t t;
    if (order == XROM_ORDER_V64) {
        for (i = 0; i + 1 < size; i += 2) {
            t = rom[i]; rom[i] = rom[i + 1]; rom[i + 1] = t;
        }
    } else if (order == XROM_ORDER_N64) {
        for (i = 0; i + 3 < size; i += 4) {
            t = rom[i]; rom[i] = rom[i + 3]; rom[i + 3] = t;
            t = rom[i + 1]; rom[i + 1] = rom[i + 2]; rom[i + 2] = t;
        }
    }
}

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* A byte for a message: printable ASCII as is, anything else a dot. */
static char printable(uint8_t c) { return c >= 0x20 && c < 0x7F ? (char)c : '.'; }

void xrom_parse_header(const uint8_t* rom, xrom_info* out) {
    int i, n;
    out->crc1 = be32(rom + 0x10);
    out->crc2 = be32(rom + 0x14);
    for (i = 0; i < 20; i++) out->title[i] = printable(rom[0x20 + i]);
    for (n = 20; n > 0 && (out->title[n - 1] == ' ' || out->title[n - 1] == '.'); n--) {}
    out->title[n] = '\0';
    for (i = 0; i < 4; i++) out->code[i] = printable(rom[0x3B + i]);
    out->code[4] = '\0';
    out->region = rom[0x3E];
    out->version = rom[0x3F];
}

const char* xrom_verdict_name(uint32_t v) {
    switch (v) {
    case XROM_OK: return "ok";
    case XROM_NO_ROM: return "no ROM";
    case XROM_BAD_SIZE: return "size";
    case XROM_BAD_HEADER: return "bad header";
    case XROM_WRONG_GAME: return "wrong game";
    case XROM_WRONG_REGION: return "region";
    case XROM_BAD_HASH: return "bad SHA-1";
    case XROM_UNREADABLE: return "unreadable";
    }
    return "unknown";
}

static uint32_t fail(xrom_info* info, uint32_t verdict) {
    info->verdict = verdict;
    return verdict;
}

uint32_t xrom_check_size(xrom_info* info, uint32_t size) {
    info->size = size;
    if (size == XROM_SIZE) return XROM_OK;
    snprintf(info->message, sizeof info->message,
             "This is not a Pilotwings 64 ROM (size %u bytes; expected 8 MB).", (unsigned)size);
    return fail(info, XROM_BAD_SIZE);
}

uint32_t xrom_check_header(xrom_info* info) {
    if (info->order == XROM_ORDER_UNKNOWN) {
        snprintf(info->message, sizeof info->message, "Not an N64 ROM (bad header).");
        return fail(info, XROM_BAD_HEADER);
    }
    if (strcmp(info->code, "NPWE") == 0) return XROM_OK;
    if (strcmp(info->code, "NPWJ") == 0 || strcmp(info->code, "NPWP") == 0) {
        snprintf(info->message, sizeof info->message,
                 "%s Pilotwings 64 is not supported; the port is built from the US release (NPWE).",
                 info->code[3] == 'J' ? "Japanese" : "European");
        return fail(info, XROM_WRONG_REGION);
    }
    snprintf(info->message, sizeof info->message, "This ROM is not Pilotwings 64 (game code %s).", info->code);
    return fail(info, XROM_WRONG_GAME);
}

uint32_t xrom_check_hash(xrom_info* info, const xrom_known* table, int count) {
    const xrom_known* want = NULL;
    int i;
    xsha1_hex(info->sha1, info->sha1_hex);
    for (i = 0; i < count; i++) {
        if (strcmp(table[i].sha1, info->sha1_hex) == 0) {
            snprintf(info->label, sizeof info->label, "%s", table[i].label);
            info->message[0] = '\0';
            info->verdict = XROM_OK;
            return XROM_OK;
        }
        if (!want && strcmp(table[i].code, info->code) == 0 && table[i].version == info->version) want = &table[i];
    }
    if (!want && count > 0) want = &table[0];
    snprintf(info->message, sizeof info->message,
             "Bad or modified dump: SHA-1 is %s, expected %s. Re-dump your cartridge.", info->sha1_hex,
             want ? want->sha1 : "(none)");
    return fail(info, XROM_BAD_HASH);
}

uint32_t xrom_verify_with_table(uint8_t* buf, uint32_t size, xrom_info* info, const xrom_known* table, int count) {
    xsha1_ctx c;
    memset(info, 0, sizeof *info);
    if (xrom_check_size(info, size) != XROM_OK) return info->verdict;
    info->order = xrom_detect_order(buf);
    if (info->order == XROM_ORDER_UNKNOWN) return xrom_check_header(info);
    xrom_normalise(buf, size, info->order);
    xrom_parse_header(buf, info);
    if (xrom_check_header(info) != XROM_OK) return info->verdict;
    xsha1_init(&c);
    xsha1_update(&c, buf, size);
    xsha1_final(&c, info->sha1);
    return xrom_check_hash(info, table, count);
}

uint32_t xrom_verify(uint8_t* buf, uint32_t size, xrom_info* info) {
    return xrom_verify_with_table(buf, size, info, xrom_known_table, XROM_KNOWN_COUNT);
}

/* ---- Xbox part ---- */
#ifndef XROM_HOST_TEST

#define CHUNK (256u * 1024u)
#define CARD_LINES 24
#define CARD_COLS 72   /* the debug screen is 80 columns wide; 4 are the indent */

static xrom_info s_info;

const xrom_info* xhw_rom_info(void) { return &s_info; }

/* Extension (after the last dot) is one of the accepted ones, any case. */
static int accepted_ext(const char* name) {
    static const char* const ext[] = { "z64", "n64", "v64", "rom", "bin" };
    const char* dot = strrchr(name, '.');
    size_t i;
    if (!dot) return 0;
    for (i = 0; i < sizeof ext / sizeof ext[0]; i++)
        if (_stricmp(dot + 1, ext[i]) == 0) return 1;
    return 0;
}

/* Files of ours that could carry an accepted extension but aren't a ROM */
static int ours(const char* name) {
    return _strnicmp(name, "default", 7) == 0 || _strnicmp(name, "settings", 8) == 0 ||
           _strnicmp(name, "boot", 4) == 0 || _strnicmp(name, "autopad", 7) == 0;
}

/* The card's text, wrapped to the screen */
typedef struct card {
    char text[CARD_LINES][CARD_COLS + 8];
    const char* ptr[CARD_LINES + 1];
    int n;
} card;

static void card_line(card* c, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void card_line(card* c, const char* fmt, ...) {
    va_list ap;
    if (c->n >= CARD_LINES) return;
    va_start(ap, fmt);
    vsnprintf(c->text[c->n], sizeof c->text[0], fmt, ap);
    va_end(ap);
    c->ptr[c->n] = c->text[c->n];
    c->n++;
    c->ptr[c->n] = NULL;
}

/* Words of `s` to lines of at most CARD_COLS columns; a word longer than that is cut. */
static void card_wrap(card* c, const char* s) {
    while (*s) {
        int len = (int)strlen(s), cut = len;
        if (len > CARD_COLS) {
            for (cut = CARD_COLS; cut > 0 && s[cut] != ' '; cut--) {}
            if (cut == 0) cut = CARD_COLS;
        }
        card_line(c, "%.*s", cut, s);
        s += cut;
        while (*s == ' ') s++;
    }
}

static const char* order_name(uint32_t o) {
    return o == XROM_ORDER_Z64 ? "z64" : o == XROM_ORDER_V64 ? "v64" : o == XROM_ORDER_N64 ? "n64" : "unknown";
}

static void __attribute__((noreturn)) no_rom_card(void) {
    card c;
    xhw_logf("[ROM] no ROM found in %s", xhw_game_dir());
    xhw_exit_reason("ROM check failed (no ROM)");
    xhw_logf("[ROM] lastexit.txt will say: ROM check failed (no ROM)");
    c.n = 0;
    card_line(&c, "This needs your own copy of Pilotwings 64 (US release, NPWE).");
    card_line(&c, "");
    card_line(&c, "Put the ROM in the same folder as default.xbe, e.g.");
    card_line(&c, "    E:\\Games\\Wings64-X\\default.xbe");
    card_line(&c, "    E:\\Games\\Wings64-X\\Pilotwings 64 (U).z64");
    card_line(&c, "");
    card_line(&c, "Any file name works; accepted extensions: .z64 .n64 .v64 .rom .bin");
    card_line(&c, "The ROM is an 8 MB file. Nothing is bundled with this game.");
    xhw_error_card("No ROM found", c.ptr, 30);
}

/* Log the failure, put the reason on the card and leave. */
static void __attribute__((noreturn)) fail_card(const xrom_info* info, const char* title) {
    card c;
    char reason[96];
    xhw_logf("[ROM] FAILED: %s", info->message);
    snprintf(reason, sizeof reason, "ROM check failed (%s)", xrom_verdict_name(info->verdict));
    xhw_exit_reason(reason);
    xhw_logf("[ROM] lastexit.txt will say: %s", reason);
    c.n = 0;
    card_wrap(&c, info->message);
    card_line(&c, "");
    card_line(&c, "File: D:\\%s", info->file);
    if (info->size) card_line(&c, "Size: %u bytes", (unsigned)info->size);
    if (info->order != XROM_ORDER_UNKNOWN || info->code[0]) {
        card_line(&c, "Byte order: %s", order_name(info->order));
        card_line(&c, "Title: \"%s\"  code %s  region %c  version %u", info->title, info->code,
                  info->region >= 0x20 && info->region < 0x7F ? (int)info->region : '?', (unsigned)info->version);
        card_line(&c, "CRC: %08x %08x", (unsigned)info->crc1, (unsigned)info->crc2);
    }
    if (info->verdict == XROM_BAD_HASH) {
        card_line(&c, "SHA-1 found:    %s", info->sha1_hex);
        card_line(&c, "SHA-1 expected: %s", xrom_known_table[0].sha1);
    }
    xhw_error_card(title, c.ptr, 30);
}

/* First file in D:\ with an accepted extension. 1 if found. */
static int find_rom(char* name, int cap) {
    xhw_dir_entry e;
    void* h;
    int ok;
#if XHW_TEST_BUILD
    {
        const char* forced = getenv("MX_ROM");   /* test builds: a file on D: by name */
        if (forced && *forced) {
            snprintf(name, (size_t)cap, "%s", forced);
            return 1;
        }
    }
#endif
    h = xhw_dir_first("D:\\*", &e);
    if (!h) return 0;
    for (ok = 1; ok;) {
        if (!e.is_dir && accepted_ext(e.name) && !ours(e.name)) {
            snprintf(name, (size_t)cap, "%s", e.name);
            FindClose((HANDLE)h);
            return 1;
        }
        ok = xhw_dir_next(h, &e);   /* closes the handle when the list ends */
    }
    return 0;
}

const uint8_t* xhw_rom_load_verified(xrom_info* out) {
    char path[96];
    xrom_info* info = &s_info;
    HANDLE h;
    DWORD hi = 0, lo;
    uint8_t* buf;
    uint32_t done = 0;
    uint64_t t0, t1, t2;
    xsha1_ctx sha;

    memset(info, 0, sizeof *info);
    if (!find_rom(info->file, (int)sizeof info->file)) no_rom_card();
    snprintf(path, sizeof path, "D:\\%s", info->file);
    xhw_logf("[ROM] file %s", path);

    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        snprintf(info->message, sizeof info->message, "The ROM file can't be opened (error %u).",
                 (unsigned)GetLastError());
        fail(info, XROM_UNREADABLE);
        fail_card(info, "ROM can't be read");
    }
    lo = GetFileSize(h, &hi);
    if (hi || xrom_check_size(info, lo) != XROM_OK) {
        CloseHandle(h);
        if (hi) xrom_check_size(info, 0xFFFFFFFFu);
        fail_card(info, "Not a Pilotwings 64 ROM");
    }

    buf = (uint8_t*)malloc(XROM_SIZE);
    if (!buf) xhw_fatal("Out of memory", "Can't allocate the 8 MB ROM buffer.");
    xhw_commit(buf, XROM_SIZE);
    t0 = xhw_time_us();
    xhw_splash_progress(0.1f);
    while (done < XROM_SIZE) {
        DWORD got = 0;
        if (!ReadFile(h, buf + done, CHUNK, &got, NULL) || got == 0) {
            snprintf(info->message, sizeof info->message, "Read error after %u bytes (error %u).", (unsigned)done,
                     (unsigned)GetLastError());
            CloseHandle(h);
            fail(info, XROM_UNREADABLE);
            fail_card(info, "ROM can't be read");
        }
        done += got;
        xhw_splash_progress(0.1f + 0.8f * (float)done / (float)XROM_SIZE);
    }
    CloseHandle(h);
    t1 = xhw_time_us();

    info->order = xrom_detect_order(buf);
    if (info->order == XROM_ORDER_UNKNOWN) {
        xrom_check_header(info);
        fail_card(info, "Not an N64 ROM");
    }
    xrom_normalise(buf, XROM_SIZE, info->order);
    xrom_parse_header(buf, info);
    xhw_logf("[ROM] title \"%s\" code %s region %c v%u crc 0x%08x 0x%08x order %s", info->title, info->code,
             (char)info->region, (unsigned)info->version, (unsigned)info->crc1, (unsigned)info->crc2,
             order_name(info->order));
    if (xrom_check_header(info) != XROM_OK)
        fail_card(info, info->verdict == XROM_WRONG_REGION ? "Unsupported ROM region" : "Not Pilotwings 64");

    xsha1_init(&sha);
    for (done = 0; done < XROM_SIZE; done += CHUNK) xsha1_update(&sha, buf + done, CHUNK);
    xsha1_final(&sha, info->sha1);
    t2 = xhw_time_us();
    xhw_splash_progress(1.0f);
    xhw_logf("[ROM] loaded %u KB in %u ms, SHA-1 in %u ms", XROM_SIZE >> 10, (unsigned)((t1 - t0) / 1000),
             (unsigned)((t2 - t1) / 1000));
    if (xrom_check_hash(info, xrom_known_table, XROM_KNOWN_COUNT) != XROM_OK) {
        xhw_logf("[ROM] SHA-1 %s", info->sha1_hex);
        fail_card(info, "Bad or modified ROM dump");
    }
    xhw_logf("[ROM] verified: %s SHA-1 ok", info->label);
    if (out) *out = *info;
    return buf;
}

#endif /* XROM_HOST_TEST */
