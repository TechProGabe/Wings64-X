/* pw64_swap.c - in-place big-endian -> host swap of raw-copied ROM structs
 * (PW64_SWAP, wings64_prelude.h; PLAN.md D2). From wings64-dc's
 * src/dc/rom/pw64_swap.c, a C port of Birdman64 crates/pw64-platform/src/swap.rs
 * (MIT); unchanged for the Xbox (little-endian like the SH-4).
 *
 * The layout string is a sequence of `[N]c` items (b = 1 byte kept, h =
 * 2-byte swap, w = 4-byte swap, d = 8-byte swap, spaces ignored); its total
 * must equal the element size, else pw64_fatal: a cheap check that the layout
 * matches the struct as the game triple lays it out. */
#include <stdint.h>

#define MAX_ITEMS 32

static unsigned parse(const char* layout, uint8_t size[MAX_ITEMS], uint16_t rep[MAX_ITEMS]) {
    unsigned n_items = 0, n = 0;
    int have_n = 0;
    for (const char* c = layout; *c; c++) {
        unsigned sz;
        if (*c >= '0' && *c <= '9') {
            n = n * 10 + (unsigned)(*c - '0');
            have_n = 1;
            continue;
        }
        switch (*c) {
            case 'b': sz = 1; break;
            case 'h': sz = 2; break;
            case 'w': sz = 4; break;
            case 'd': sz = 8; break;
            case ' ': continue;
            default: pw64_fatal("pw64_swap: bad layout char", (unsigned char)*c);
        }
        if (n_items == MAX_ITEMS) pw64_fatal("pw64_swap: layout too long", n_items);
        size[n_items] = (uint8_t)sz;
        rep[n_items++] = (uint16_t)(have_n ? n : 1);
        n = 0;
        have_n = 0;
    }
    if (have_n) pw64_fatal("pw64_swap: trailing count in layout", n);
    return n_items;
}

void pw64_swap(void* p, unsigned int count, unsigned int stride, const char* layout) {
    uint8_t size[MAX_ITEMS];
    uint16_t rep[MAX_ITEMS];
    unsigned items = parse(layout, size, rep), total = 0;
    uint8_t* e = p;
    for (unsigned i = 0; i < items; i++) total += size[i] * rep[i];
    if (total != stride) pw64_fatal("pw64_swap: layout size != element size", (total << 16) | stride);
    for (unsigned k = 0; k < count; k++, e += stride) {
        uint8_t* q = e;
        for (unsigned i = 0; i < items; i++) {
            for (unsigned r = 0; r < rep[i]; r++, q += size[i]) {
                for (unsigned a = 0, b = size[i] - 1u; a < b; a++, b--) {
                    uint8_t t = q[a];
                    q[a] = q[b];
                    q[b] = t;
                }
            }
        }
    }
}
