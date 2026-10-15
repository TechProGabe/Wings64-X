/* nv2a.c - the NV2A back end of the Fast3D interpreter (xgfx.h).
 *
 * The interpreter (xbox/src/gfx/f3d_interp.c) hands over screen-space
 * triangles and an XgfxState; per draw this sends:
 *   - a register-combiner program for the N64 colour combiner
 *     (nv2a_cc.c), cached by (cc0, cc1, cycle type, fog), with PRIM and ENV
 *     in every stage's two constants,
 *   - two texture units (TEXEL0, TEXEL1),
 *   - blend, alpha test, depth, stencil, polygon offset, fog, scissor,
 * each diffed against what the GPU already has, then the vertices through
 * a fixed pass-through vertex program into a write-combined ring.
 *
 * The pushbuffer, pools, frame pacing, flip, tile regions and GPU-fault
 * handling are Melee-X's (xbox/src/hw/nv2a.c, v4.1), whose hardware
 * knowledge is OpenCrossing-Xbox's: see their docs/renderer.md and
 * docs/traps.md for why each wait and register is there. What Melee-X
 * needed for GX (vertex-program generation, TEV, indirect texturing, EFB
 * copies drawn by the GPU, the Z16 remap) is not here. PLAN.md D13 lists
 * the performance machinery and its switches. */
#include <hal/video.h>
#include <pbkit/pbkit.h>
#include <pbkit/nv_regs.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "nv2a_cc.h"
#include "nv2a_fault.h"
#include "xgfx.h"
#include "xhw.h"
#include "xhw_internal.h"

extern unsigned int pb_DepthFmt;   /* settable: tools/xbox/patch_pbkit.py */
extern unsigned int pb_ColorFmt;   /* pbkit.c (pb_set_color_format) */
void ocx_pb_layout(unsigned* out);  /* tools/xbox/patch_pbkit.py */

/* ======================================================================
 * Switches (PLAN.md D13; docs/testing.md has the table)
 * ====================================================================== */
#ifndef XGFX_OVERLAP
#define XGFX_OVERLAP 1        /* the wait for the GPU is at the next frame's first use, not in the present */
#endif
#ifndef XGFX_POLY_SLOPE
#define XGFX_POLY_SLOPE 1.0f   /* polygon offset slope factor for biased (decal, tie-break) draws; 0: bias only */
#endif
#ifndef XGFX_FLIP_REPAIR
#define XGFX_FLIP_REPAIR 1    /* 0: a flip pbkit lost leaves the screen on an older buffer until the 1 s cap */
#endif
#ifndef XGFX_FLIP_WAIT
#define XGFX_FLIP_WAIT 1      /* 0: the old guard (4 vblanks at most, then draws on the shown buffer) */
#endif
#ifndef XGFX_PB_KICK
#define XGFX_PB_KICK 8192     /* pushbuffer words per kick */
#endif
#ifndef XGFX_VB_CACHE_BREAK
#define XGFX_VB_CACHE_BREAK 1 /* BREAK_VERTEX_BUFFER_CACHE at each batch start */
#endif
#ifndef XGFX_TILE
/* bit 4: tile region 0 as pbkit's base | 3 (Melee-X: +11 % at one enable bit);
 * bit 8: no Z compression on the depth buffer (Melee-X's XGX_TILE 8). The
 * default turns compression off: the stencil shadow volumes (pass 1 marks
 * the stencil) showed as solid dark columns on the console and not in xemu,
 * which models no compression (console run 1, 2026-10-15; the A/B is
 * XGFX_TILE=4). */
#define XGFX_TILE 12
#endif
#ifndef XGFX_Z24_16BPP
#define XGFX_Z24_16BPP 1      /* Z24S8 with 16-bit colour (720p/1080i); 0: Z16 */
#endif
#ifndef XGFX_ZETA_GUARD
#define XGFX_ZETA_GUARD 1     /* resend the zeta pitch around depth clears where it isn't the colour's */
#endif
#ifndef XGFX_STATS_EVERY
#define XGFX_STATS_EVERY 600  /* [NV2A] stats period in frames */
#endif

/* ======================================================================
 * Output geometry
 * ====================================================================== */
static int s_fbw = 640, s_fbh = 480, s_bpp = 32;
static float s_zmax = 16777215.0f;

static uint32_t zeta_pitch(void) {
    return pb_back_buffer_width() * (pb_DepthFmt == NV097_SET_SURFACE_FORMAT_ZETA_Z16 ? 2u : 4u);
}

void xgfx_output(XgfxOutput* out) {
    out->fb_w = s_fbw;
    out->fb_h = s_fbh;
    out->bpp = s_bpp;
    out->zmax = s_zmax;
}

/* ======================================================================
 * Contiguous memory: texture pool and vertex ring (Melee-X's pool)
 * ====================================================================== */
#ifdef XGFX_TEX_POOL_KB
#define TEX_POOL_480 ((uint32_t)XGFX_TEX_POOL_KB * 1024)
#define TEX_POOL_HD ((uint32_t)XGFX_TEX_POOL_KB * 1024)
#else
#define TEX_POOL_480 (8u * 1024 * 1024)
#define TEX_POOL_HD (6u * 1024 * 1024)
#endif
#define TEX_POOL_MIN (3u * 1024 * 1024)
#define RING_BYTES (1536u * 1024)
#define PB_BYTES (1024u * 1024)
_Static_assert((PB_BYTES & (PB_BYTES - 1)) == 0, "pb_size takes powers of two only");
#define PB_GUARD (PB_BYTES - 192 * 1024)
#define POOL_ALIGN 128
#define POOL_BIG (256 * 1024)
#define POOL_HASH 4096

typedef struct Blk { uint32_t off, size; int free; struct Blk *next, *prev, *fnext, *fprev, *hnext; } Blk;
typedef struct {
    uint8_t* base;
    uint32_t bytes, used;
    Blk* blocks;
    Blk* freelist;
    Blk* hash[POOL_HASH];
} Pool;
static Pool s_tp;

static void fl_add(Pool* pl, Blk* b) {
    b->fprev = NULL;
    b->fnext = pl->freelist;
    if (pl->freelist) pl->freelist->fprev = b;
    pl->freelist = b;
}

static void fl_del(Pool* pl, Blk* b) {
    if (b->fprev) b->fprev->fnext = b->fnext;
    else pl->freelist = b->fnext;
    if (b->fnext) b->fnext->fprev = b->fprev;
}

static unsigned hkey(uint32_t off) { return (off / POOL_ALIGN) & (POOL_HASH - 1); }

static void h_add(Pool* pl, Blk* b) {
    unsigned k = hkey(b->off);
    b->hnext = pl->hash[k];
    pl->hash[k] = b;
}

static Blk* h_take(Pool* pl, uint32_t off) {
    Blk** pp;
    for (pp = &pl->hash[hkey(off)]; *pp; pp = &(*pp)->hnext)
        if ((*pp)->off == off) {
            Blk* b = *pp;
            *pp = b->hnext;
            return b;
        }
    return NULL;
}

static int pool_init(Pool* pl, uint32_t bytes) {
    memset(pl, 0, sizeof *pl);
    pl->base = (uint8_t*)MmAllocateContiguousMemoryEx(bytes, 0, MAXRAM, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    pl->blocks = pl->base ? (Blk*)calloc(1, sizeof(Blk)) : NULL;
    if (!pl->blocks) {
        if (pl->base) MmFreeContiguousMemory(pl->base);
        memset(pl, 0, sizeof *pl);
        return 0;
    }
    pl->bytes = bytes;
    pl->blocks->size = bytes;
    pl->blocks->free = 1;
    fl_add(pl, pl->blocks);
    return 1;
}

static void pool_release(Pool* pl) {
    Blk* b = pl->blocks;
    while (b) {
        Blk* n = b->next;
        free(b);
        b = n;
    }
    if (pl->base) MmFreeContiguousMemory(pl->base);
    memset(pl, 0, sizeof *pl);
}

static void blk_insert_after(Blk* b, Blk* n) {
    n->prev = b;
    n->next = b->next;
    if (b->next) b->next->prev = n;
    b->next = n;
}

/* small requests take the lowest free block that fits, big ones the top of
 * the highest (keeping the two apart against fragmentation) */
static void* pool_alloc(Pool* pl, uint32_t size) {
    Blk *b, *pick = NULL;
    size = (size + POOL_ALIGN - 1) & ~(uint32_t)(POOL_ALIGN - 1);
    for (b = pl->freelist; b; b = b->fnext) {
        if (b->size < size) continue;
        if (!pick || (size < POOL_BIG ? b->off < pick->off : b->off > pick->off)) pick = b;
    }
    if (!pick) return NULL;
    b = pick;
    if (b->size > size) {
        Blk* n = (Blk*)calloc(1, sizeof(Blk));
        if (!n) return NULL;
        blk_insert_after(b, n);
        if (size < POOL_BIG) {
            n->off = b->off + size;
            n->size = b->size - size;
            n->free = 1;
            fl_add(pl, n);
            b->size = size;
        } else {
            n->off = b->off + b->size - size;
            n->size = size;
            b->size -= size;
            h_add(pl, n);
            pl->used += size;
            return pl->base + n->off;
        }
    }
    b->free = 0;
    fl_del(pl, b);
    h_add(pl, b);
    pl->used += size;
    return pl->base + b->off;
}

static void pool_free(Pool* pl, void* p) {
    Blk *b, *n;
    if (!p) return;
    b = h_take(pl, (uint32_t)((uint8_t*)p - pl->base));
    if (!b) return;
    b->free = 1;
    pl->used -= b->size;
    if ((n = b->next) && n->free) {
        fl_del(pl, n);
        b->size += n->size;
        b->next = n->next;
        if (n->next) n->next->prev = b;
        free(n);
    }
    if ((n = b->prev) && n->free) {
        n->size += b->size;
        n->next = b->next;
        if (b->next) b->next->prev = n;
        free(b);
    } else {
        fl_add(pl, b);
    }
}

uint32_t xgfx_tex_pool_free_kb(void) { return (s_tp.bytes - s_tp.used) / 1024; }

static uint8_t* s_ring;
static uint32_t s_ring_pos;

/* ======================================================================
 * Pushbuffer
 * ====================================================================== */
static uint32_t* P;
static int s_pb_open;
static uint32_t* s_pb_mark;
static uint32_t* s_pb_base;

static inline void put1(uint32_t m, uint32_t v) {
    P[0] = (1u << 18) | m;
    P[1] = v;
    P += 2;
}
static inline void putf(uint32_t m, float v) {
    union { float f; uint32_t u; } c;
    c.f = v;
    put1(m, c.u);
}

static void pb_open(void) {
    if (s_pb_open) return;
    P = pb_begin();
    s_pb_mark = P;
    s_pb_open = 1;
    /* The NV2A caches vertex data by address and reads ahead of a draw's
     * last vertex: a kicked batch can leave stale read-ahead for vertices
     * written right behind it in the ring (Melee-X, console only) */
    if (XGFX_VB_CACHE_BREAK) put1(NV097_BREAK_VERTEX_BUFFER_CACHE, 0);
}

static void pb_close(void) {
    if (!s_pb_open) return;
    pb_end(P);
    s_pb_open = 0;
}

/* per-interval counters for the [NV2A] line */
static uint32_t s_st_flip_repairs, s_st_flip_waits, s_st_waits, s_st_tex_kb, s_st_verts, s_st_tex_fail, s_st_pb_peak, s_st_pb_resets, s_st_draws;
static uint32_t s_frame, s_draws;

static void report_gpu_stall(void) {
    uint32_t get = *(volatile uint32_t*)(0xFD000000u + 0x3244), put = *(volatile uint32_t*)(0xFD000000u + 0x3240);
    xhw_logf("[NV2A] GPU stalled: get %08x put %08x dma_state %08x pgraph %08x, faults %u, frame %u draw %u", get, put,
             *(volatile uint32_t*)(0xFD000000u + 0x3228), *(volatile uint32_t*)(0xFD000000u + 0x400700),
             (unsigned)nv2a_fault_count(), s_frame, s_draws);
    nv2a_fault_log_first((uint32_t)(uintptr_t)s_pb_base & 0x03FFFFFFu);
}

/* pb_busy misses methods already in PFIFO's CACHE1: idle also means CACHE1
 * empty and the pusher stopped, seen twice (Melee-X) */
static int gpu_quiet(void) {
    volatile const uint32_t* r = (volatile const uint32_t*)0xFD000000u;
    return !pb_busy() && (r[0x3214 / 4] & 0x10) && !(r[0x3220 / 4] & 0x10) && !r[0x400700 / 4];
}

static int gpu_busy(void) { return !gpu_quiet() || !gpu_quiet(); }

static void wait_idle(void) {
    uint64_t t0 = 0, w0 = xhw_time_us();
    int reported = 0;
    s_st_waits++;
    pb_close();
    while (gpu_busy()) {
        nv2a_irq_recover();
        if (!t0) t0 = xhw_time_ns();
        else if (!reported && xhw_time_ns() - t0 > 2000000000ull) {
            report_gpu_stall();
            reported = 1;
        }
    }
    if (t0) xhw_perf_add(XHW_PERF_WAIT, xhw_time_us() - w0);
}

static uint32_t pb_used(void) {
    const uint32_t* p = s_pb_open ? P : pb_begin();
    return (uint32_t)((const uint8_t*)p - (const uint8_t*)s_pb_base);
}

/* ======================================================================
 * Textures
 * ====================================================================== */
#define MAX_TEX 4096
typedef struct {
    int used;
    uint16_t w, h;
    uint32_t bytes;
    void* mem;
    uint32_t gen;       /* a new texture at a freed one's slot differs */
} Tex;
static Tex s_tex[MAX_TEX];
static int s_tex_next = 1;
static void* s_deferred[4096];
static int s_ndeferred;

static void release_deferred(void) {
    int i;
    for (i = 0; i < s_ndeferred; i++) pool_free(&s_tp, s_deferred[i]);
    s_ndeferred = 0;
}

static void defer_free(void* p) {
    if (s_ndeferred == (int)(sizeof s_deferred / sizeof s_deferred[0])) {
        wait_idle();
        release_deferred();
    }
    s_deferred[s_ndeferred++] = p;
}

static uint32_t swz_x[1024], swz_y[1024];
static int swz_w, swz_h;

static void swz_tables(int w, int h) {
    uint32_t xm = 0, ym = 0, bit = 1, mbit = 1;
    int done, i;
    if (w == swz_w && h == swz_h) return;
    do {
        done = 1;
        if (bit < (uint32_t)w) { xm |= mbit; mbit <<= 1; done = 0; }
        if (bit < (uint32_t)h) { ym |= mbit; mbit <<= 1; done = 0; }
        bit <<= 1;
    } while (!done);
    for (i = 0; i < w; i++) {
        uint32_t v = 0, m = 1, mask = xm;
        while (mask) { uint32_t low = mask & -mask; if ((uint32_t)i & m) v |= low; m <<= 1; mask &= mask - 1; }
        swz_x[i] = v;
    }
    for (i = 0; i < h; i++) {
        uint32_t v = 0, m = 1, mask = ym;
        while (mask) { uint32_t low = mask & -mask; if ((uint32_t)i & m) v |= low; m <<= 1; mask &= mask - 1; }
        swz_y[i] = v;
    }
    swz_w = w;
    swz_h = h;
}

static int log2i(int v) { int l = 0; while ((1 << l) < v) l++; return l; }
static int is_pot(uint32_t v) { return v && !(v & (v - 1)); }

/* Swizzled (Morton) order into a cached scratch buffer, then one in-order
 * copy to the write-combined pool: swizzled stores land all over the
 * destination and would defeat write combining (Melee-X). */
static void write_swizzled(uint32_t* dst, const uint32_t* src, int w, int h) {
    static uint32_t scratch[1024 * 1024 / 4];
    uint32_t n = (uint32_t)(w * h);
    uint32_t* out = n <= sizeof scratch / 4 ? scratch : dst;
    int x, y;
    swz_tables(w, h);
    for (y = 0; y < h; y++) {
        const uint32_t* row = src + y * w;
        uint32_t yo = swz_y[y];
        for (x = 0; x < w; x++) out[yo | swz_x[x]] = row[x];
    }
    if (out == scratch) memcpy(dst, scratch, n * 4);
}

static int alloc_handle(void) {
    int k;
    for (k = 0; k < MAX_TEX - 1; k++) {
        int c = s_tex_next + k;
        if (c >= MAX_TEX) c = 1 + (c % (MAX_TEX - 1));
        if (!s_tex[c].used) {
            s_tex_next = c + 1;
            return c;
        }
    }
    return 0;
}

static void* tex_pool_alloc(uint32_t bytes) {
    void* base = pool_alloc(&s_tp, bytes);
    if (!base && s_ndeferred) {
        wait_idle();
        release_deferred();
        base = pool_alloc(&s_tp, bytes);
    }
    if (!base) s_st_tex_fail++;
    return base;
}

static uint32_t s_tex_epoch;

uint32_t xgfx_tex_create(uint32_t w, uint32_t h, const uint32_t* argb) {
    static uint32_t s_gen;
    uint32_t bytes = w * h * 4;
    void* mem;
    int id;
    if (!is_pot(w) || !is_pot(h) || w > 1024 || h > 1024 || !argb) return 0;
    id = alloc_handle();
    if (!id) return 0;
    mem = tex_pool_alloc(bytes);
    if (!mem) return 0;
    s_st_tex_kb += bytes / 1024;
    write_swizzled((uint32_t*)mem, argb, (int)w, (int)h);
    s_tex[id].used = 1;
    s_tex[id].w = (uint16_t)w;
    s_tex[id].h = (uint16_t)h;
    s_tex[id].bytes = bytes;
    s_tex[id].mem = mem;
    s_tex[id].gen = ++s_gen;
    s_tex_epoch++;
    return (uint32_t)id;
}

void xgfx_tex_destroy(uint32_t tex) {
    if (!tex || tex >= MAX_TEX || !s_tex[tex].used) return;
    defer_free(s_tex[tex].mem);
    s_tex[tex].used = 0;
    s_tex[tex].mem = NULL;
    s_tex_epoch++;
}

/* ======================================================================
 * Pass-through vertex program
 *
 * The interpreter has done the transform: oPos = (x, y, z, w) as given
 * (screen pixels, depth units, clip w for perspective-correct
 * interpolation; the same convention as Melee-X's generated programs), the
 * diffuse colour, two texture coordinates, the fog amount, and oD1 from
 * constant row 0 (nv2a_cc.h: the combiner's extra inputs). Encoding: Melee-X's
 * nv2a_vp.c encoder (checked there against nv2a-vsh bit for bit), MOVs only.
 * ====================================================================== */
enum { VPI_POS = 0, VPI_DIFFUSE = 3, VPI_FOG = 5, VPI_TEX0 = 9, VPI_TEX1 = 10 };
enum { O_POS = 0, O_D0 = 3, O_D1 = 4, O_FOG = 5, O_T0 = 9, O_T1 = 10 };
enum { MUX_R = 1, MUX_V = 2, MUX_C = 3 };
#define MAC_MOV 1
#define XYZW 0xE4

static uint32_t swz_fields(uint32_t swz, int sx, int sy, int sz, int sw) {
    return (swz & 3) << sx | ((swz >> 2) & 3) << sy | ((swz >> 4) & 3) << sz | ((swz >> 6) & 3) << sw;
}

/* MOV o[out].mask, (v[idx] or c[idx]).swz */
static void vp_mov(uint32_t* w, int src_mux, int src_idx, uint32_t swz, int out, int omask) {
    uint32_t w1 = 0, w2 = 0, w3 = 0;
    uint32_t hm = (uint32_t)((omask & 1) << 3 | (omask & 2) << 1 | (omask & 4) >> 1 | (omask & 8) >> 3);
    w1 |= swz_fields(XYZW, 6, 4, 2, 0);
    w2 |= swz_fields(XYZW, 23, 21, 19, 17) | swz_fields(XYZW, 8, 6, 4, 2);
    w2 |= (uint32_t)MUX_V << 26 | (uint32_t)MUX_V << 11;
    w3 |= (uint32_t)MUX_V << 28;
    w3 |= 0xFFu << 3 | 1u << 11 | 7u << 20;
    w1 |= (uint32_t)MAC_MOV << 21;
    if (src_mux == MUX_C) w1 = (w1 & ~(0xFFu << 13)) | (uint32_t)src_idx << 13;
    else w1 = (w1 & ~(0xFu << 9)) | (uint32_t)(src_idx & 15) << 9;
    w1 = (w1 & ~0xFFu) | swz_fields(swz, 6, 4, 2, 0);
    w2 = (w2 & ~(3u << 26)) | (uint32_t)src_mux << 26;
    w3 = (w3 & ~(0xFFu << 3)) | (uint32_t)out << 3;
    w3 |= hm << 12;
    w[0] = 0;
    w[1] = w1;
    w[2] = w2;
    w[3] = w3;
}

static void vp_load(void) {
    uint32_t prog[6 * 4];
    int i, n = 6;
    vp_mov(prog + 0, MUX_V, VPI_POS, XYZW, O_POS, 15);
    vp_mov(prog + 4, MUX_V, VPI_DIFFUSE, XYZW, O_D0, 15);
    vp_mov(prog + 8, MUX_C, 0, XYZW, O_D1, 15);
    vp_mov(prog + 12, MUX_V, VPI_TEX0, XYZW, O_T0, 15);
    vp_mov(prog + 16, MUX_V, VPI_TEX1, XYZW, O_T1, 15);
    vp_mov(prog + 20, MUX_V, VPI_FOG, 0x00 /* xxxx */, O_FOG, 1);
    prog[(n - 1) * 4 + 3] |= 1u;   /* FINAL */
    pb_open();
    put1(NV097_SET_TRANSFORM_PROGRAM_LOAD, 0);
    for (i = 0; i < n; i += 8) {
        int k = n - i < 8 ? n - i : 8;
        pb_push(P++, NV097_SET_TRANSFORM_PROGRAM, (DWORD)(k * 4));
        memcpy(P, &prog[i * 4], (size_t)k * 16);
        P += k * 4;
    }
    put1(NV097_SET_TRANSFORM_PROGRAM_START, 0);
}

/* constant row 0 -> oD1 */
static float s_d1[4] = { -1, -1, -1, -1 };
static void vp_const_d1(const float v[4]) {
    if (!memcmp(v, s_d1, sizeof s_d1)) return;
    memcpy(s_d1, v, sizeof s_d1);
    put1(NV097_SET_TRANSFORM_CONSTANT_LOAD, 0);
    pb_push(P++, NV097_SET_TRANSFORM_CONSTANT, 4);
    memcpy(P, v, 16);
    P += 4;
}

/* the vertex arrays: XgfxVtx in the ring; others disabled */
static void vtx_arrays(uint32_t base_phys) {
    static const struct { int slot, off, type, size; } k_attr[] = {
        { VPI_POS, offsetof(XgfxVtx, x), NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 4 },
        { VPI_DIFFUSE, offsetof(XgfxVtx, rgba), NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL, 4 },
        { VPI_FOG, offsetof(XgfxVtx, fog), NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 1 },
        { VPI_TEX0, offsetof(XgfxVtx, s0), NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 2 },
        { VPI_TEX1, offsetof(XgfxVtx, s1), NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 2 },
    };
    static uint32_t s_base = 0xFFFFFFFFu;
    size_t i;
    if (base_phys == s_base) return;
    s_base = base_phys;
    for (i = 0; i < 16; i++) {
        size_t k;
        int found = -1;
        for (k = 0; k < sizeof k_attr / sizeof k_attr[0]; k++)
            if (k_attr[k].slot == (int)i) found = (int)k;
        if (found < 0) {
            put1(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + (uint32_t)i * 4, 2);   /* type F, size 0: disabled */
            continue;
        }
        put1(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + (uint32_t)i * 4,
             (uint32_t)k_attr[found].type | (uint32_t)k_attr[found].size << 4 | (uint32_t)sizeof(XgfxVtx) << 8);
        put1(NV097_SET_VERTEX_DATA_ARRAY_OFFSET + (uint32_t)i * 4, base_phys + (uint32_t)k_attr[found].off);
    }
}

/* ======================================================================
 * Frame
 * ====================================================================== */
#define CONTROL0 NV097_SET_CONTROL0_TEXTURE_PERSPECTIVE_ENABLE   /* a z-buffer, not pbkit's w-buffer (Melee-X) */
#define PCRTC_START_REG (*(volatile uint32_t*)0xFD600800)
static int s_frame_open;
static uint32_t s_depth_phys;
static int s_state_valid;   /* the GPU's state is what our shadows say */

static int s_zguard = -1;
static void zeta_guard(void) {
    uint32_t want, v;
    if (s_zguard < 0) s_zguard = XGFX_ZETA_GUARD && zeta_pitch() != pb_back_buffer_pitch();
    if (!s_zguard) return;
    want = zeta_pitch();
    wait_idle();
    v = *(volatile const uint32_t*)0xFD40085Cu;
    if (v == want) return;
    {
        uint32_t* p = pb_begin();
        p = pb_push1(p, NV097_SET_SURFACE_PITCH, pb_back_buffer_pitch() | want << 16);
        pb_end(p);
    }
}

static void clear_fb(int x, int y, int w, int h, uint32_t argb, int color, int depth, uint32_t z24) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s_fbw) w = s_fbw - x;
    if (y + h > s_fbh) h = s_fbh - y;
    if (w <= 0 || h <= 0) return;
    pb_close();
    if (depth) zeta_guard();
    if (color) pb_fill(x, y, w, h, argb);   /* converts to the surface's format itself */
    if (depth) {
        uint32_t* p = pb_begin();
        uint32_t zv = pb_DepthFmt != NV097_SET_SURFACE_FORMAT_ZETA_Z16 ? z24 << 8 : z24 >> 8;
        p = pb_push1(p, NV097_SET_CLEAR_RECT_HORIZONTAL, (uint32_t)((x + w - 1) << 16) | (uint32_t)x);
        p = pb_push1(p, NV097_SET_CLEAR_RECT_VERTICAL, (uint32_t)((y + h - 1) << 16) | (uint32_t)y);
        p = pb_push1(p, NV097_SET_ZSTENCIL_CLEAR_VALUE, zv);
        p = pb_push1(p, NV097_CLEAR_SURFACE, NV097_CLEAR_SURFACE_Z | NV097_CLEAR_SURFACE_STENCIL);
        pb_end(p);
        zeta_guard();
    }
    {   /* the next draws wait for the clear (Melee-X: the console ran them overlapped) */
        uint32_t* p = pb_begin();
        p = pb_push1(p, NV097_WAIT_FOR_IDLE, 0);
        pb_end(p);
    }
}

/* the screen as the render target through DMA object 3 with physical
 * offsets: no DMA object is ever switched (Melee-X XGX_ONE_DMA) */
static uint32_t* push_screen_target(uint32_t* p) {
    p = pb_push1(p, NV097_SET_CONTEXT_DMA_COLOR, 3);
    p = pb_push1(p, NV097_SET_CONTEXT_DMA_ZETA, 3);
    p = pb_push1(p, NV097_SET_SURFACE_FORMAT, NV097_SET_SURFACE_FORMAT_TYPE_PITCH << 8 | pb_DepthFmt << 4 | pb_ColorFmt);
    p = pb_push1(p, NV097_SET_SURFACE_PITCH, pb_back_buffer_pitch() | zeta_pitch() << 16);
    p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL, (uint32_t)s_fbw << 16);
    p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL, (uint32_t)s_fbh << 16);
    p = pb_push1(p, NV097_SET_SURFACE_COLOR_OFFSET, (uint32_t)pb_back_buffer() & 0x03FFFFFFu);
    p = pb_push1(p, NV097_SET_SURFACE_ZETA_OFFSET, s_depth_phys);
    return p;
}

void xgfx_frame_begin(void) {
    uint32_t* p;
    if (s_frame_open) return;
    if (XGFX_OVERLAP) {   /* the last frame may still be drawing */
        if (gpu_busy()) wait_idle();
        release_deferred();
    }
    pb_reset();
    s_pb_base = pb_begin();
    p = pb_begin();
    p = push_screen_target(p);
    p = pb_push1(p, NV097_SET_CONTROL0, CONTROL0);
    pb_end(p);
    s_ring_pos = 0;
    s_frame_open = 1;
    nv2a_fault_frame = s_frame;
    clear_fb(0, 0, s_fbw, s_fbh, 0xFF000000u, 1, 1, 0xFFFFFF);
}

/* restart at the pushbuffer head when a frame gets close to its end:
 * pbkit's pushbuffer has no overflow check (OpenCrossing traps.md) */
static void pb_budget(void) {
    uint32_t used = pb_used();
    if (used > s_st_pb_peak) s_st_pb_peak = used;
    if (used < PB_GUARD) return;
    s_st_pb_resets++;
    wait_idle();
    pb_reset();
    s_pb_base = pb_begin();
    s_ring_pos = 0;
}

void xgfx_clear(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t argb, int color, int depth, float z01) {
    uint32_t z24;
    if (z01 < 0.0f) z01 = 0.0f;
    if (z01 > 1.0f) z01 = 1.0f;
    /* in double: in float, 1.0 * 16777215 + 0.5 rounds to 2^24 and the clear wrapped to 0 */
    z24 = (uint32_t)((double)z01 * 16777215.0 + 0.5);
    if (z24 > 0xFFFFFFu) z24 = 0xFFFFFFu;
    xgfx_frame_begin();
    clear_fb(x0, y0, x1 - x0, y1 - y0, argb, color, depth, z24);
}

/* ---- the CPU's view of a framebuffer (tile region 0: through the aperture) ---- */
static uint8_t* s_fb_ap;
static uint32_t s_fb_ap_base, s_fb_ap_end;

static void* fb_cpu(void* fb) {
    uint32_t phys = (uint32_t)(uintptr_t)fb & 0x03FFFFFFu;
    if (!s_fb_ap || phys < s_fb_ap_base || phys >= s_fb_ap_end) return fb;
    return s_fb_ap + (phys - s_fb_ap_base);
}

/* ---- frame-rate counter: GPU fills of a 5x7 font (Melee-X) ---- */
static int s_fps_on;
static uint32_t s_fps_val, s_fps_frames;
static uint64_t s_fps_t0;
void xgfx_set_fps_overlay(int on) { s_fps_on = on; }

static void fps_overlay(void) {
    static const uint8_t font[10][7] = {
        { 14, 17, 19, 21, 25, 17, 14 }, { 4, 12, 4, 4, 4, 4, 14 },   { 14, 17, 1, 2, 4, 8, 31 },
        { 31, 2, 4, 2, 1, 17, 14 },     { 2, 6, 10, 18, 31, 2, 2 },  { 31, 16, 30, 1, 1, 17, 14 },
        { 6, 8, 16, 30, 17, 17, 14 },   { 31, 1, 2, 4, 8, 8, 8 },    { 14, 17, 17, 14, 17, 17, 14 },
        { 14, 17, 17, 15, 1, 2, 12 },
    };
    uint64_t now = xhw_time_ns();
    uint32_t v, digits[3], nd = 0, d, cy;
    int x0 = s_fbw / 16, y0 = s_fbh / 16, w, h;
    s_fps_frames++;
    if (!s_fps_t0 || now - s_fps_t0 > 2000000000ull) {
        s_fps_t0 = now;
        s_fps_frames = 0;
    } else if (now - s_fps_t0 >= 500000000ull) {
        s_fps_val = (uint32_t)((s_fps_frames * 1000000000ull + (now - s_fps_t0) / 2) / (now - s_fps_t0));
        s_fps_t0 = now;
        s_fps_frames = 0;
    }
    v = s_fps_val > 999 ? 999 : s_fps_val;
    do {
        digits[nd++] = v % 10;
        v /= 10;
    } while (v && nd < 3);
    w = (int)nd * 12 + 4;
    h = 18;
    pb_close();
    pb_fill(x0, y0, w, h, 0xFF000000u);
    for (d = 0; d < nd; d++)
        for (cy = 0; cy < 7; cy++) {
            uint32_t bits = font[digits[nd - 1 - d]][cy], cx = 0;
            while (cx < 5) {
                uint32_t run = 0;
                while (cx + run < 5 && (bits >> (4 - (cx + run)) & 1)) run++;
                if (run) pb_fill(x0 + 2 + (int)(d * 12 + cx * 2), y0 + 2 + (int)cy * 2, (int)run * 2, 2, 0xFFFFFF00u);
                cx += run ? run : 1;
            }
        }
}

#ifndef XHW_FBDUMP_EVERY
#define XHW_FBDUMP_EVERY 0
#endif

static void* s_last_fb;   /* the colour buffer presented last (xgfx_capture_last) */

/* display-order probe (test builds): which presented frame each vblank shows */
static volatile uint32_t s_buf_addr[3], s_buf_frame[3];
static volatile int s_flip_waiting;   /* the present is waiting for the flip: nothing is drawn */

static void probe_note_present(uint32_t addr, uint32_t frame) {
    int i, slot = -1;
    for (i = 0; i < 3 && slot < 0; i++)
        if (s_buf_addr[i] == addr) slot = i;
    for (i = 0; i < 3 && slot < 0; i++)
        if (!s_buf_addr[i]) slot = i;
    if (slot < 0) return;
    s_buf_addr[slot] = addr;
    s_buf_frame[slot] = frame;
}

void nv2a_vbl_probe(void) {
    static uint32_t s_shown, s_logged;
    uint32_t a = PCRTC_START_REG & 0x03FFFFFFu;
    int i;
    if (!XHW_TEST_BUILD) return;
    if (s_frame_open && !s_flip_waiting && a == ((uint32_t)pb_back_buffer() & 0x03FFFFFFu) && s_logged < 60) {
        s_logged++;
        xhw_logf("[NV2A] vblank shows the buffer being drawn (frame %u)", s_frame);
    }
    for (i = 0; i < 3; i++)
        if (s_buf_addr[i] == a) {
            uint32_t f = s_buf_frame[i];
            if (f < s_shown && s_logged < 60) {
                s_logged++;
                xhw_logf("[NV2A] display went back: frame %u shown after frame %u (now drawing %u)", f, s_shown, s_frame);
            }
            s_shown = f;
            return;
        }
}

void xgfx_present(void) {
    int shot;
    xgfx_frame_begin();
    pb_budget();
    if (s_fps_on) fps_overlay();
    shot = xhw_shot_take();
    s_last_fb = pb_back_buffer();
    if (!XGFX_OVERLAP || shot || (XHW_FBDUMP_EVERY && (s_frame + 1) % XHW_FBDUMP_EVERY == 0)) wait_idle();
    if (shot == 2 || (XHW_FBDUMP_EVERY && (s_frame + 1) % XHW_FBDUMP_EVERY == 0))
        xhw_fbdump(fb_cpu(pb_back_buffer()), s_fbw, s_fbh, s_bpp, (int)pb_back_buffer_pitch());
    else if (shot == 1)
        xhw_fbdump_file(fb_cpu(pb_back_buffer()), s_fbw, s_fbh, s_bpp, (int)pb_back_buffer_pitch());
    if (!XGFX_OVERLAP) release_deferred();
    nv2a_fault_log_first((uint32_t)(uintptr_t)s_pb_base & 0x03FFFFFFu);
    nv2a_irq_recover();
    {
        /* never draw into the buffer being scanned out (OpenCrossing traps.md);
         * the waits depend on pbkit's vblank DPC, so they are timed */
        int guard = 4, warned = 0, idle_vbls;
        uint64_t flip_w0;
        uint64_t t0 = 0;
        pb_close();   /* pb_finished pushes the flip at pb_Put: close this frame's tail first (Melee-X v1 hang) */
        probe_note_present((uint32_t)pb_back_buffer() & 0x03FFFFFFu, s_frame + 1);
        while (pb_finished()) {
            nv2a_irq_recover();
            if (!t0) t0 = xhw_time_ns();
            else if (!warned && xhw_time_ns() - t0 > 1000000000ull) {
                xhw_logf("[NV2A] flip stalled: no back buffer free for 1 s, vblank %u, faults %u",
                         (unsigned)pb_get_vbl_counter(), (unsigned)nv2a_fault_count());
                warned = 1;
            }
        }
        /* The next back buffer stays on screen until the GPU has reached the
         * flips queued before it, which can take several vblanks. The guard
         * gave up after 4 tries, and the next frame's clear then drew on the
         * picture being shown: whole-screen flashes in flight (xemu, display
         * probe, 2026-10-15). It now waits until that buffer is really off
         * screen; after 1 s it logs and goes on. */
        t0 = 0;
        warned = 0;
        idle_vbls = 0;
        s_flip_waiting = 1;
        flip_w0 = xhw_time_us();
        while ((PCRTC_START_REG & 0x03FFFFFF) == ((uint32_t)pb_back_buffer() & 0x03FFFFFF)) {
            DWORD vbl = pb_get_vbl_counter();
            int ms;
            if (!XGFX_FLIP_WAIT && guard-- <= 0) break;
            nv2a_irq_recover();
            /* A lost flip: pbkit's DPC can retire a queued flip at a vblank
             * its ISR did not see (the DPC ran late, with the vblank pending
             * behind INTA_DISABLED), so PCRTC never moves: the GPU is idle,
             * nothing is queued, and the screen keeps an older buffer, the
             * one this frame would draw into (xemu, 2026-10-15: every 2-3 s).
             * After two quiet vblanks the newest finished frame is put on
             * screen here, right after a vblank. */
            if (XGFX_FLIP_REPAIR && !gpu_busy() && ++idle_vbls > 2 && s_last_fb) {
                PCRTC_START_REG = (uint32_t)(uintptr_t)s_last_fb & 0x03FFFFFFu;
                s_st_flip_repairs++;
                break;
            }
            if (!t0) t0 = xhw_time_ns();
            else if (xhw_time_ns() - t0 > 1000000000ull) {
                volatile const uint32_t* r = (volatile const uint32_t*)0xFD000000u;
                xhw_logf("[NV2A] the back buffer stayed on screen for 1 s (vblank %u): busy %d quiet %d, crtc %08x back "
                         "%08x, PMC intr %08x en %08x, PGRAPH intr %08x, PCRTC intr %08x en %08x",
                         (unsigned)vbl, (int)gpu_busy(), gpu_quiet(), (unsigned)(PCRTC_START_REG & 0x03FFFFFF),
                         (unsigned)((uint32_t)pb_back_buffer() & 0x03FFFFFF), (unsigned)r[0x100 / 4],
                         (unsigned)r[0x140 / 4], (unsigned)r[0x400100 / 4], (unsigned)r[0x600100 / 4],
                         (unsigned)r[0x600140 / 4]);
                report_gpu_stall();
                break;
            }
            for (ms = 0; ms < 50 && pb_get_vbl_counter() == vbl; ms++) xhw_sleep_ms(1);
            s_st_flip_waits++;
        }
        if (t0) xhw_perf_add(XHW_PERF_WAIT, xhw_time_us() - flip_w0);
    }
    s_flip_waiting = 0;
    xhw_note_present();
    xhw_perf_frame();
    {   /* frames that drew almost nothing: a black frame on screen */
        static uint32_t s_thin, s_thin_logged;
        if (s_draws < 3) s_thin++;
        if (XHW_TEST_BUILD && s_thin != s_thin_logged && s_frame % 120 == 0) {
            xhw_logf("[NV2A] %u frames with fewer than 3 draws so far (frame %u)", (unsigned)s_thin, s_frame);
            s_thin_logged = s_thin;
        }
    }
    s_frame++;
    if (s_frame % XGFX_STATS_EVERY == 0) {
        xhw_logf("[NV2A] frame %u: %u draws, tex pool %u KB free | per %u: %u idle waits, %u flip waits (%u repaired), %u KB textures, %u pool "
                 "allocations failed, %u verts, %u draws, pushbuffer peak %u of %u KB (%u restarts), combiners "
                 "approximated %u",
                 s_frame, s_draws, xgfx_tex_pool_free_kb(), XGFX_STATS_EVERY, s_st_waits, s_st_flip_waits, s_st_flip_repairs, s_st_tex_kb,
                 s_st_tex_fail,
                 s_st_verts, s_st_draws, s_st_pb_peak / 1024, PB_BYTES / 1024, s_st_pb_resets,
                 (unsigned)xgfx_cc_approximated());
        nv2a_fault_log_ctx();
        s_st_flip_repairs = s_st_flip_waits = s_st_waits = s_st_tex_kb = s_st_verts = s_st_tex_fail = s_st_pb_peak = s_st_pb_resets = s_st_draws = 0;
    }
    s_draws = 0;
    s_frame_open = 0;
    if (!XGFX_OVERLAP) xgfx_frame_begin();
}

/* ======================================================================
 * Draw state
 * ====================================================================== */
#define CC_CACHE 128
typedef struct { uint32_t cc0, cc1, flags; CcProg prog; } CcEntry;
static CcEntry s_cc[CC_CACHE];
static int s_cc_n;
static uint32_t s_cc_approx;
uint32_t xgfx_cc_approximated(void) { return s_cc_approx; }

static const CcProg* cc_lookup(uint32_t cc0, uint32_t cc1, int two, int fog) {
    static int s_last = -1;
    uint32_t flags = 0x80000000u | (two ? 1u : 0u) | (fog ? 2u : 0u);
    int k;
    if (s_last >= 0 && s_cc[s_last].cc0 == cc0 && s_cc[s_last].cc1 == cc1 && s_cc[s_last].flags == flags)
        return &s_cc[s_last].prog;
    for (k = 0; k < s_cc_n; k++)
        if (s_cc[k].cc0 == cc0 && s_cc[k].cc1 == cc1 && s_cc[k].flags == flags) {
            s_last = k;
            return &s_cc[k].prog;
        }
    k = s_cc_n < CC_CACHE ? s_cc_n++ : (int)(s_frame % CC_CACHE);
    s_cc[k].cc0 = cc0;
    s_cc[k].cc1 = cc1;
    s_cc[k].flags = flags;
    cc_compile(cc0, cc1, two, fog, &s_cc[k].prog);
    if (s_cc[k].prog.approximated) {
        s_cc_approx++;
        if (s_cc_approx <= 32)
            xhw_logf("[NV2A] approximated combiner %06x:%08x cycle %d fog %d", (unsigned)cc0, (unsigned)cc1, two ? 2 : 1,
                     fog);
    }
    s_last = k;
    return &s_cc[k].prog;
}

/* shadow registers: a method is sent only when its value changes */
enum {
    F_DEPTH_EN, F_DEPTH_FUNC, F_DEPTH_MASK, F_BLEND_EN, F_BLEND_S, F_BLEND_D, F_ALPHA_EN, F_ALPHA_FUNC, F_ALPHA_REF,
    F_COLOR_MASK, F_STENCIL_EN, F_STENCIL_FUNC, F_STENCIL_REF, F_SOP_FAIL, F_SOP_ZFAIL, F_SOP_ZPASS, F_POLY_EN,
    F_POLY_BIAS, F_FOG_EN, F_FOG_COLOR, F_CLIP_H, F_CLIP_V, F_COMB_PROG, F_COMB_C0, F_COMB_C1, F_TEX_PROG, F_N
};
static uint32_t s_fx[F_N];
static const CcProg* s_cc_sent;
static uint32_t s_unit_key[2][6];

#define SETF(i, method, value)                                                                                      \
    do {                                                                                                            \
        uint32_t v_ = (uint32_t)(value);                                                                            \
        if (s_fx[i] != v_) { put1(method, v_); s_fx[i] = v_; }                                                      \
    } while (0)

static void state_reset(void) {
    memset(s_fx, 0xFF, sizeof s_fx);
    memset(s_unit_key, 0xFF, sizeof s_unit_key);
    memset(s_d1, 0xFF, sizeof s_d1);
    s_cc_sent = NULL;
}

static uint32_t pack_rgba(const uint8_t c[4]) {
    return (uint32_t)c[3] << 24 | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
}

static void emit_combiners(const XgfxState* st) {
    const CcProg* cp = cc_lookup(st->cc0, st->cc1, (int)st->two_cycle, (int)st->fog);
    uint32_t c0 = pack_rgba(st->prim), c1 = pack_rgba(st->env);
    int i;
    if (cp != s_cc_sent) {
        put1(NV097_SET_COMBINER_CONTROL, CC_COMBINER_CONTROL(cp));
        for (i = 0; i < cp->nstages; i++) {
            put1(NV097_SET_COMBINER_COLOR_ICW + (uint32_t)i * 4, cp->cicw[i]);
            put1(NV097_SET_COMBINER_COLOR_OCW + (uint32_t)i * 4, cp->cocw[i]);
            put1(NV097_SET_COMBINER_ALPHA_ICW + (uint32_t)i * 4, cp->aicw[i]);
            put1(NV097_SET_COMBINER_ALPHA_OCW + (uint32_t)i * 4, cp->aocw[i]);
        }
        put1(NV097_SET_COMBINER_SPECULAR_FOG_CW0, cp->cw0);
        put1(NV097_SET_COMBINER_SPECULAR_FOG_CW1, cp->cw1);
        s_cc_sent = cp;
        s_fx[F_COMB_C0] = s_fx[F_COMB_C1] = 0xA5A5A5A5u;
        s_fx[F_COMB_PROG] = (uint32_t)cp->nstages;
    }
    /* PRIM and ENV in every stage's two constants (nv2a_cc.h) */
    if (s_fx[F_COMB_C0] != c0 || s_fx[F_COMB_C1] != c1) {
        for (i = 0; i < 8; i++) {
            put1(NV097_SET_COMBINER_FACTOR0 + (uint32_t)i * 4, c0);
            put1(NV097_SET_COMBINER_FACTOR1 + (uint32_t)i * 4, c1);
        }
        s_fx[F_COMB_C0] = c0;
        s_fx[F_COMB_C1] = c1;
    }
    {   /* oD1 = (LOD_FRACTION x3, PRIM_LOD_FRAC) / 255 (nv2a_cc.h) */
        float d1[4];
        d1[0] = d1[1] = d1[2] = st->lod_frac * (1.0f / 255.0f);
        d1[3] = st->prim_lod_frac * (1.0f / 255.0f);
        vp_const_d1(d1);
    }
}

static uint32_t wrap_mode(uint32_t w) { return w == XGFX_WRAP_MIRROR ? 2u : w == XGFX_WRAP_CLAMP ? 3u : 1u; }

static void emit_textures(const XgfxState* st) {
    uint32_t prog = 0;
    int u;
    for (u = 0; u < 2; u++) {
        const Tex* t = st->tex[u] && st->tex[u] < MAX_TEX && s_tex[st->tex[u]].used ? &s_tex[st->tex[u]] : NULL;
        uint32_t key[6];
        uint32_t b = (uint32_t)u * 64;
        key[0] = t ? st->tex[u] : 0;
        key[1] = t ? t->gen : 0;
        key[2] = st->wrap_s[u];
        key[3] = st->wrap_t[u];
        key[4] = st->filter[u];
        key[5] = s_tex_epoch;
        if (t) prog |= 1u << (u * 5);   /* 2D_PROJECTIVE */
        if (!memcmp(key, s_unit_key[u], sizeof key) || (!t && !s_unit_key[u][0] && s_unit_key[u][1] == 0)) continue;
        memcpy(s_unit_key[u], key, sizeof key);
        if (!t) {
            put1(NV097_SET_TEXTURE_CONTROL0 + b, 0);
            continue;
        }
        put1(NV097_SET_TEXTURE_OFFSET + b, (uint32_t)(uintptr_t)t->mem & 0x03FFFFFF);
        put1(NV097_SET_TEXTURE_FORMAT + b, 1 | (1u << 3) | (2u << 4) |
                                               ((uint32_t)NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8 << 8) |
                                               (1u << 16) | ((uint32_t)log2i(t->w) << 20) | ((uint32_t)log2i(t->h) << 24));
        put1(NV097_SET_TEXTURE_ADDRESS + b, wrap_mode(st->wrap_s[u]) | (wrap_mode(st->wrap_t[u]) << 8) | (3u << 16));
        put1(NV097_SET_TEXTURE_CONTROL0 + b, 0x4003FFC0u);
        {
            uint32_t f = st->filter[u] ? 2u : 1u;   /* LINEAR / NEAREST */
            put1(NV097_SET_TEXTURE_FILTER + b, (f << 16) | (f << 24) | 0x2000u);
        }
    }
    SETF(F_TEX_PROG, NV097_SET_SHADER_STAGE_PROGRAM, prog);
}

static void emit_fixed(const XgfxState* st) {
    int x0 = st->scissor[0], y0 = st->scissor[1], x1 = st->scissor[2], y1 = st->scissor[3];
    SETF(F_DEPTH_EN, NV097_SET_DEPTH_TEST_ENABLE, st->depth_func != XGFX_DEPTH_OFF);
    if (st->depth_func != XGFX_DEPTH_OFF)
        SETF(F_DEPTH_FUNC, NV097_SET_DEPTH_FUNC,
             st->depth_func == XGFX_DEPTH_LESS ? 0x201 : st->depth_func == XGFX_DEPTH_ALWAYS ? 0x207 : 0x203);
    SETF(F_DEPTH_MASK, NV097_SET_DEPTH_MASK, st->depth_write ? 1 : 0);
    SETF(F_BLEND_EN, NV097_SET_BLEND_ENABLE, st->blend != XGFX_BLEND_OFF);
    if (st->blend != XGFX_BLEND_OFF) {
        SETF(F_BLEND_S, NV097_SET_BLEND_FUNC_SFACTOR, 0x302);   /* SRC_ALPHA */
        SETF(F_BLEND_D, NV097_SET_BLEND_FUNC_DFACTOR, st->blend == XGFX_BLEND_ADD ? 1 : 0x303);   /* ONE / 1-SRC_ALPHA */
    }
    SETF(F_ALPHA_EN, NV097_SET_ALPHA_TEST_ENABLE, st->alpha_ref != 0);
    if (st->alpha_ref) {
        SETF(F_ALPHA_FUNC, NV097_SET_ALPHA_FUNC, NV097_SET_ALPHA_FUNC_V_GEQUAL);
        SETF(F_ALPHA_REF, NV097_SET_ALPHA_REF, st->alpha_ref);
    }
    SETF(F_COLOR_MASK, NV097_SET_COLOR_MASK,
         st->color_write ? NV097_SET_COLOR_MASK_RED_WRITE_ENABLE | NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE |
                               NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE | NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE
                         : 0);
    /* shadow volumes (PLAN.md D4): pass 1 marks where the volume's back faces
     * pass the depth test, pass 2 draws where nothing was marked and clears
     * the mark as it goes, so the next volume starts from zero */
    SETF(F_STENCIL_EN, NV097_SET_STENCIL_TEST_ENABLE, st->stencil != XGFX_STENCIL_OFF);
    if (st->stencil == XGFX_STENCIL_MARK) {
        SETF(F_STENCIL_FUNC, NV097_SET_STENCIL_FUNC, 0x207);   /* ALWAYS */
        SETF(F_STENCIL_REF, NV097_SET_STENCIL_FUNC_REF, 1);
        SETF(F_SOP_FAIL, NV097_SET_STENCIL_OP_FAIL, NV097_SET_STENCIL_OP_V_KEEP);
        SETF(F_SOP_ZFAIL, NV097_SET_STENCIL_OP_ZFAIL, NV097_SET_STENCIL_OP_V_KEEP);
        SETF(F_SOP_ZPASS, NV097_SET_STENCIL_OP_ZPASS, NV097_SET_STENCIL_OP_V_REPLACE);
    } else if (st->stencil == XGFX_STENCIL_TEST) {
        SETF(F_STENCIL_FUNC, NV097_SET_STENCIL_FUNC, 0x202);   /* EQUAL */
        SETF(F_STENCIL_REF, NV097_SET_STENCIL_FUNC_REF, 0);
        SETF(F_SOP_FAIL, NV097_SET_STENCIL_OP_FAIL, NV097_SET_STENCIL_OP_V_ZERO);
        SETF(F_SOP_ZFAIL, NV097_SET_STENCIL_OP_ZFAIL, NV097_SET_STENCIL_OP_V_ZERO);
        SETF(F_SOP_ZPASS, NV097_SET_STENCIL_OP_ZPASS, NV097_SET_STENCIL_OP_V_ZERO);
    }
    SETF(F_POLY_EN, NV097_SET_POLY_OFFSET_FILL_ENABLE, st->depth_bias != 0);
    if (st->depth_bias) {
        union { float f; uint32_t u; } bias;
        bias.f = -(float)st->depth_bias;   /* toward the eye */
        if (s_fx[F_POLY_BIAS] != bias.u) {
            /* a slope term too: a constant bias in Z24 steps is next to
             * nothing where a decal is seen edge-on (the runway's lines
             * blinked through on the console, not in xemu; run 1) */
            putf(NV097_SET_POLYGON_OFFSET_SCALE_FACTOR, -XGFX_POLY_SLOPE);
            put1(NV097_SET_POLYGON_OFFSET_BIAS, bias.u);
            s_fx[F_POLY_BIAS] = bias.u;
        }
    }
    SETF(F_FOG_EN, NV097_SET_FOG_ENABLE, st->fog ? 1 : 0);
    if (st->fog)   /* ABGR, red in the low byte; the FOG register's alpha is the factor */
        SETF(F_FOG_COLOR, NV097_SET_FOG_COLOR,
             (uint32_t)st->fog_color[0] | (uint32_t)st->fog_color[1] << 8 | (uint32_t)st->fog_color[2] << 16);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s_fbw) x1 = s_fbw;
    if (y1 > s_fbh) y1 = s_fbh;
    if (x1 <= x0 || y1 <= y0) { x0 = y0 = 0; x1 = y1 = 1; }
    SETF(F_CLIP_H, NV097_SET_WINDOW_CLIP_HORIZONTAL, (uint32_t)x0 | ((uint32_t)(x1 - 1) << 16));
    SETF(F_CLIP_V, NV097_SET_WINDOW_CLIP_VERTICAL, (uint32_t)y0 | ((uint32_t)(y1 - 1) << 16));
}

static void setup_state(void) {
    pb_open();
    put1(NV097_SET_CONTROL0, CONTROL0);
    put1(NV097_SET_LIGHTING_ENABLE, 0);
    /* oD1 carries the combiner's extra inputs; without SPECULAR_ENABLE the
     * NV2A replaces it with (0,0,0,1) (OpenCrossing traps.md) */
    put1(NV097_SET_SPECULAR_ENABLE, 1);
    put1(NV097_SET_LIGHT_CONTROL,
         NV097_SET_LIGHT_CONTROL_V_SEPARATE_SPECULAR | NV097_SET_LIGHT_CONTROL_V_ALPHA_FROM_MATERIAL_SPECULAR);
    put1(NV097_SET_SKIN_MODE, NV097_SET_SKIN_MODE_OFF);
    put1(NV097_SET_SHADER_OTHER_STAGE_INPUT, 0);
    put1(NV097_SET_CULL_FACE_ENABLE, 0);   /* the interpreter culls (it clips first) */
    put1(NV097_SET_SHADE_MODEL, NV097_SET_SHADE_MODEL_SMOOTH);
    put1(NV097_SET_WINDOW_CLIP_TYPE, 0);
    put1(NV097_SET_ZMIN_MAX_CONTROL, NV097_SET_ZMIN_MAX_CONTROL_ZCLAMP_CLAMP);
    put1(NV097_SET_SHADER_CLIP_PLANE_MODE, 0);
    put1(NV097_SET_TRANSFORM_EXECUTION_MODE,
         NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_PROGRAM | (NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE_PRIV << 2));
    put1(NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN, 0);
    put1(NV097_SET_CLIP_MIN, 0);
    putf(NV097_SET_CLIP_MAX, s_zmax);
    put1(NV097_SET_STENCIL_MASK, 0xFF);
    put1(NV097_SET_STENCIL_FUNC_MASK, 0xFF);
    /* fog: LINEAR with params (1, 1, 0) makes the factor oFog.x itself, the
     * vertex's fog amount; the final combiner blends toward the fog colour
     * by it (Melee-X nv2a.c emit_fog) */
    put1(NV097_SET_FOG_MODE, NV097_SET_FOG_MODE_V_LINEAR);
    put1(NV097_SET_FOG_GEN_MODE, NV097_SET_FOG_GEN_MODE_V_SPEC_ALPHA);
    putf(NV097_SET_FOG_PARAMS, 1.0f);
    putf(NV097_SET_FOG_PARAMS + 4, 1.0f);
    putf(NV097_SET_FOG_PARAMS + 8, 0.0f);
    if (s_bpp == 16) put1(NV097_SET_DITHER_ENABLE, 1);
    pb_close();
    vp_load();
    pb_close();
    state_reset();
    s_state_valid = 1;
}

/* the vertex ring: 32768-vertex windows, so a DRAW_ARRAYS start stays
 * below 0x10000 (the console raises a PGRAPH data error past it) */
#define VTX_WINDOW 0x8000u

void xgfx_draw(const XgfxState* st, const XgfxVtx* v, uint32_t ntris) {
    uint32_t count = ntris * 3, bytes = count * (uint32_t)sizeof(XgfxVtx), first, idx, win;
    uint8_t* dst;
    if (!ntris) return;
    xgfx_frame_begin();
    pb_budget();
    pb_open();
    if (!s_state_valid) setup_state(), pb_open();
    /* vertices: at a multiple of the stride from the ring's start */
    if (bytes > RING_BYTES / 2) return;
    s_ring_pos = (s_ring_pos + sizeof(XgfxVtx) - 1) / sizeof(XgfxVtx) * sizeof(XgfxVtx);
    if (s_ring_pos + bytes > RING_BYTES) {
        wait_idle();   /* the GPU still reads the older part */
        pb_open();
        s_ring_pos = 0;
    }
    dst = s_ring + s_ring_pos;
    memcpy(dst, v, bytes);   /* in order: the ring is write-combined */
    idx = s_ring_pos / (uint32_t)sizeof(XgfxVtx);
    s_ring_pos += bytes;
    win = idx & ~(VTX_WINDOW - 1);
    first = idx - win;
    emit_fixed(st);
    emit_textures(st);
    emit_combiners(st);
    vtx_arrays(((uint32_t)(uintptr_t)s_ring & 0x03FFFFFFu) + win * (uint32_t)sizeof(XgfxVtx));
    put1(NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_TRIANGLES);
    while (count > 0) {
        uint32_t batch = count > 256 * 64 ? 256 * 64 : count, k, words = 0;
        uint32_t* hdr = P++;
        for (k = 0; k < batch; k += 256) {
            uint32_t n = batch - k > 256 ? 256 : batch - k;
            *P++ = ((n - 1) << 24) | (first + k);
            words++;
        }
        *hdr = words << 18 | NV2A_SUPPRESS_COMMAND_INCREMENT(NV097_DRAW_ARRAYS);
        first += batch;
        count -= batch;
    }
    put1(NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
    if (P - s_pb_mark >= XGFX_PB_KICK) pb_close();
    s_st_verts += ntris * 3;
    s_draws++;
    s_st_draws++;
    nv2a_fault_draw = s_draws;
}

/* debug: the back buffer as it is now, as [FBDUMP] (waits for the GPU) */
void xgfx_debug_dump(void) {
    xgfx_frame_begin();
    wait_idle();
    pb_open();
    xhw_fbdump(fb_cpu(pb_back_buffer()), s_fbw, s_fbh, s_bpp, (int)pb_back_buffer_pitch());
}

/* ======================================================================
 * Readback (photo album, persistent frame): waits for the GPU
 * ====================================================================== */
void xgfx_read_rect(int32_t x0, int32_t y0, int32_t w, int32_t h, uint32_t* argb) {
    const uint8_t* fb;
    uint32_t pitch;
    int32_t x, y;
    xgfx_frame_begin();
    wait_idle();
    pb_open();
    fb = (const uint8_t*)fb_cpu(pb_back_buffer());
    pitch = pb_back_buffer_pitch();
    for (y = 0; y < h; y++) {
        int32_t fy = y0 + y < 0 ? 0 : y0 + y >= s_fbh ? s_fbh - 1 : y0 + y;
        const uint8_t* row = fb + (size_t)fy * pitch;
        uint32_t* out = argb + y * w;
        for (x = 0; x < w; x++) {
            int32_t fx = x0 + x < 0 ? 0 : x0 + x >= s_fbw ? s_fbw - 1 : x0 + x;
            if (s_bpp == 16) {
                uint32_t c = ((const uint16_t*)row)[fx], r = c >> 11, g = (c >> 5) & 63, b = c & 31;
                out[x] = 0xFF000000u | (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
            } else {
                out[x] = ((const uint32_t*)row)[fx] | 0xFF000000u;
            }
        }
    }
}

uint32_t xgfx_capture_last(float* u_max, float* v_max) {
    const uint8_t* fb;
    uint32_t pitch, tw = 1, th = 1, tex, *buf;
    int32_t step, cw, ch, x, y;
    if (!s_last_fb) return 0;
    xgfx_frame_begin();
    wait_idle();
    pb_open();
    step = (s_fbw + 1023) / 1024;   /* 720p and 1080i: every other pixel (the texture limit is 1024) */
    cw = s_fbw / step;
    ch = s_fbh / step;
    while (tw < (uint32_t)cw) tw <<= 1;
    while (th < (uint32_t)ch) th <<= 1;
    buf = (uint32_t*)calloc((size_t)tw * th, 4);
    if (!buf) return 0;
    fb = (const uint8_t*)fb_cpu(s_last_fb);
    pitch = pb_back_buffer_pitch();
    for (y = 0; y < ch; y++) {
        const uint8_t* row = fb + (size_t)(y * step) * pitch;
        uint32_t* out = buf + (size_t)y * tw;
        for (x = 0; x < cw; x++) {
            if (s_bpp == 16) {
                uint32_t c = ((const uint16_t*)row)[x * step], r = c >> 11, g = (c >> 5) & 63, b = c & 31;
                out[x] = 0xFF000000u | (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
            } else {
                out[x] = ((const uint32_t*)row)[x * step] | 0xFF000000u;
            }
        }
    }
    tex = xgfx_tex_create(tw, th, buf);
    free(buf);
    *u_max = (float)cw / (float)tw;
    *v_max = (float)ch / (float)th;
    return tex;
}

/* ======================================================================
 * Init
 * ====================================================================== */
#define ZCOMP_VALID 0x80000000u
#define ZCOMP_Z32 0x04000000u

static void tile_write(uint32_t pfb, uint32_t pgraph, uint32_t rdi, uint32_t v) {
    unsigned n = 0;
    do {
        while (VIDEOREG(NV_PGRAPH_STATUS) && ++n < 4000000u) {}
        VIDEOREG(pfb) = v;
        VIDEOREG(pgraph) = v;
        VIDEOREG(NV_PGRAPH_RDI_INDEX) = (rdi & NV_PGRAPH_RDI_INDEX_ADDRESS) | ((0xEAu << 16) & NV_PGRAPH_RDI_INDEX_SELECT);
        VIDEOREG(NV_PGRAPH_RDI_DATA) = v;
    } while (VIDEOREG(pfb) != VIDEOREG(pgraph) && ++n < 4000000u);
}

/* Melee-X's tile regions (its docs/renderer.md "Tile regions"): tile 0
 * (the framebuffers) with both low bits set; the CPU then reads the
 * framebuffers through the NV2A aperture (fb_cpu) */
static int s_tile = XGFX_TILE;

static void tiles_setup(void) {
    int fb_ok = VIDEOREG(NV_PFB_TILE + 8) == pb_back_buffer_pitch();
    int z_ok = VIDEOREG(NV_PFB_TILE + 16 + 8) == zeta_pitch();
    if (s_tile) {
        uint32_t t0 = VIDEOREG(NV_PFB_TILE), z1 = VIDEOREG(NV_PFB_ZCOMP + 4), old;
        if (fb_ok && (s_tile & 6)) t0 = (t0 & ~3u) | (s_tile & 4 ? 3u : 1u);
        if (z_ok && (s_tile & 8)) z1 = 0;
        wait_idle();
        old = pb_wait_until_tiles_not_busy();
        if (t0 != VIDEOREG(NV_PFB_TILE)) tile_write(NV_PFB_TILE, NV_PGRAPH_TILE_XBOX, 0x10, t0);
        if (z1 != VIDEOREG(NV_PFB_ZCOMP + 4)) tile_write(NV_PFB_ZCOMP + 4, NV_PGRAPH_ZCOMP_XBOX + 4, 0x90 + 4, z1);
        VIDEOREG(NV_PFIFO_CACHE1_DMA_PUSH) = old;
    }
    if (VIDEOREG(NV_PFB_TILE) & 1) {
        uint32_t base = VIDEOREG(NV_PFB_TILE) & 0x03FFC000u, end = (VIDEOREG(NV_PFB_TLIMIT) | 0x3FFFu) + 1;
        s_fb_ap = (uint8_t*)MmMapIoSpace(0xF0000000u + base, end - base, PAGE_READWRITE | PAGE_WRITECOMBINE);
        s_fb_ap_base = base;
        s_fb_ap_end = end;
    }
    xhw_logf("[NV2A] tiles: XGFX_TILE %d%s%s, tile 0 %08x, tile 1 %08x zcomp %08x, CPU framebuffer access %s",
             XGFX_TILE, fb_ok ? "" : " (fb pitch differs: untouched)", z_ok ? "" : " (depth pitch differs: untouched)",
             (unsigned)VIDEOREG(NV_PFB_TILE), (unsigned)VIDEOREG(NV_PFB_TILE + 16), (unsigned)VIDEOREG(NV_PFB_ZCOMP + 4),
             s_fb_ap ? "through the aperture" : "direct");
}

int xgfx_init(void) {
    const xhw_video_mode* vm;
    uint32_t tex_pool_bytes;
    int err, z24_16 = XGFX_Z24_16BPP;
    static int done;
    if (done) return 1;
    done = 1;
    for (;;) {
        vm = xhw_video();
        if (vm->bpp == 16) {
            pb_set_color_format(NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5, false);
            pb_DepthFmt = z24_16 ? NV097_SET_SURFACE_FORMAT_ZETA_Z24S8 : NV097_SET_SURFACE_FORMAT_ZETA_Z16;
            s_zmax = z24_16 ? 16777215.0f : 65535.0f;
            tex_pool_bytes = TEX_POOL_HD;
        } else {
            pb_set_color_format(NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8, false);
            pb_DepthFmt = NV097_SET_SURFACE_FORMAT_ZETA_Z24S8;
            s_zmax = 16777215.0f;
            tex_pool_bytes = TEX_POOL_480;
        }
        pb_size(PB_BYTES);
        err = pb_init();
        if (!err) {
            s_ring = (uint8_t*)MmAllocateContiguousMemoryEx(RING_BYTES, 0, MAXRAM, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
            while (s_ring && !pool_init(&s_tp, tex_pool_bytes) && tex_pool_bytes > TEX_POOL_MIN)
                tex_pool_bytes -= 1024u * 1024;
            if (s_tp.base && s_ring) break;
            pool_release(&s_tp);
            if (s_ring) MmFreeContiguousMemory(s_ring);
            s_ring = NULL;
            pb_kill();
        }
        xhw_logf("[NV2A] %dx%d start failed (pb_init %d)", vm->width, vm->height, err);
        if (vm->bpp == 16 && z24_16) {   /* Z16 before giving the mode up */
            z24_16 = 0;
            continue;
        }
        if (vm->height == 480) xhw_fatal("Graphics init failed", "The NV2A could not be started.");
        xhw_video_fallback_480();
    }
    s_fbw = (int)pb_back_buffer_width();
    s_fbh = (int)pb_back_buffer_height();
    s_bpp = vm->bpp;
    {
        unsigned l[8];
        ocx_pb_layout(l);
        s_depth_phys = l[6] & 0x03FFFFFFu;
    }
    tiles_setup();
    pb_show_front_screen();
    xhw_video_pb_up();
    xgfx_frame_begin();
    setup_state();
    xhw_logf("[NV2A] up: %dx%d %d-bit, tex pool %u KB, depth %s", s_fbw, s_fbh, s_bpp, s_tp.bytes / 1024,
             pb_DepthFmt == NV097_SET_SURFACE_FORMAT_ZETA_Z16 ? "Z16" : "Z24S8");
    xhw_mem_log("after nv2a");
    return 1;
}
