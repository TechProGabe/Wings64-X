/* f3d_interp.c - Fast3D (F3D GBI) display-list interpreter onto the NV2A
 * (PLAN.md D4 phase A), from wings64-dc's src/dc/gfx/gfx_f3d.c.
 *
 * Runs synchronously on the thread that calls osSpTaskStartGo for a
 * graphics task (xbox/src/game/ultra/os_shim.c); one task is one frame,
 * presented at its end. The CPU does the RSP's work: the display-list
 * walk, the 16-entry vertex cache, the matrix stack, lighting and texgen,
 * the fog factor, clipping (near, far, guard band) and culling. The NV2A
 * does the RDP's: the colour combiner (the G_SETCOMBINE words go to the
 * back end, nv2a_cc.c compiles them to register combiners), blending,
 * depth, alpha test, fog, two texture units. xgfx.h is the contract.
 *
 * Kept from the Dreamcast port, quirk by quirk (wings64-dc kb/traps.md):
 * the texrect encoding (E4 + B3 + B2), UVBT tile rows clamped to the loaded
 * bytes, RDP rectangles without shade, Mtx halfword order on a
 * little-endian host, far-plane clipping, shadow volumes, the SUBSCREEN
 * overlay stretch, the title fit, small scissors and viewports honoured,
 * TMEM deswizzle and the LOADBLOCK dxt rule. Dropped because the NV2A has
 * what the PVR lacked: the per-vertex combiner fit (register combiners do
 * it per pixel), painter-depth layers (the NV2A draws in submission
 * order), the baked sea product (two texture units), modifier volumes
 * (stencil).
 *
 * Conventions: N64 matrices are row-vector (v' = v M), stored here
 * row-major as float m[4][4]; "MUL" pre-multiplies (M = param x M). Output:
 * N64 320x240 viewport pixels mapped onto the framebuffer's content rect;
 * depth = (z/w + 1) / 2 of the back end's depth range, tested LEQUAL. */
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "f3d_gbi.h"
#include "f3d_interp.h"
#include "texconv.h"
#include "xgfx.h"
#include "xhw.h"

/* ---- switches (docs/testing.md) ---- */
#ifndef XGFX_FAR_CLIP
#define XGFX_FAR_CLIP 1        /* Fast3D clips at the far plane (z <= w): the sea stops short of the horizon */
#endif
#ifndef XGFX_OVERLAY_FULL
#define XGFX_OVERLAY_FULL 1    /* SUBSCREEN-sized no-Z untextured draws stretched to the screen edges */
#endif
#ifndef XGFX_TITLE_FIT
#define XGFX_TITLE_FIT 1       /* the title's SUBSCREEN picture fitted to the screen with a black margin */
#endif
#ifndef XGFX_SCISSOR
#define XGFX_SCISSOR 1         /* small scissors and viewports (photo album thumbnails) honoured */
#endif
#ifndef XGFX_Z_TIEBREAK
#define XGFX_Z_TIEBREAK 1      /* later coplanar draw states win depth ties, as on the RDP */
#endif
#ifndef XGFX_SHADOW_VOL
#define XGFX_SHADOW_VOL 1      /* shadow volumes through the stencil buffer; 0: both passes dropped */
#endif
#ifndef XGFX_TEX_MEMO
#define XGFX_TEX_MEMO 1        /* per-frame (load address, tile key) -> texture memo */
#endif
#ifndef XGFX_DXT_EXACT
#define XGFX_DXT_EXACT 1       /* LOADBLOCK dxt not sized for the line: the RDP's odd-line rule */
#endif

#define N64_W 320.0f
#define N64_H 240.0f
#define MTX_DEPTH 11           /* 10 pushes + current (Birdman64 gfx.md) */
#define DL_DEPTH 18
#define GUARD 2.0f             /* x/y clipped at +-GUARD*w: screen coordinates stay well inside the NV2A's range */

/* ------------------------------------------------------------------ state */

typedef struct {
    float x, y, z, w;          /* clip space */
    float s, t;                /* texel coordinates (s10.5 scaled by gSPTexture, /32) */
    float r, g, b, a;          /* shade 0..1 */
    float fog;                 /* 0..255 */
    uint32_t oc;               /* outcode: 1 x+, 2 x-, 4 y+, 8 y-, 16 near, 32 needs clipping, 64 far */
} Vtx;
#define VTX_LERP_FLOATS 11

typedef struct {
    uint8_t fmt, siz, cms, cmt, masks, maskt, shifts, shiftt;
    uint16_t line, tmem;
    uint16_t uls, ult, lrs, lrt;   /* 10.2 */
} Tile;

/* What TMEM holds at a tile's address: a LOADBLOCK (a run of bytes, odd
 * rows swapped unless dxt says otherwise) or a LOADTILE (a rectangle of the
 * texture image: `rows` rows of `row_bytes` at `stride`, unswapped as
 * sampled). */
typedef struct {
    uint32_t addr, bytes;
    uint16_t dxt;
    uint8_t valid, tile;
    uint16_t rows, row_bytes, stride;
} TmemLoad;

static struct {
    uint32_t seg[16];
    float proj[4][4];
    float mv[MTX_DEPTH][4][4];
    float mvp[4][4];
    int mv_top, mvp_dirty;
    float vp_sx, vp_sy, vp_tx, vp_ty;   /* N64 pixels */
    uint32_t geom, omh, oml;
    uint32_t cc0, cc1;
    uint8_t prim[4], env[4], fogc[4], blend[4];
    uint8_t prim_lod_frac;
    uint32_t fill;
    int16_t fog_m, fog_o;
    int tex_on, tex_tile;
    float tex_ss, tex_st;
    Tile tile[8];
    TmemLoad loads[64];                 /* indexed by tmem >> 3 */
    uint8_t timg_fmt, timg_siz;
    uint16_t timg_w;
    uint32_t timg_addr;
    uint32_t cimg, zimg;
    int num_lights;
    uint8_t light_col[9][3];
    int8_t light_dir[9][3];
    Vtx vc[16];
    uint32_t half1, half2;
} R;

/* ------------------------------------------------------------- addressing */

/* PW64 sets only segment 0 (= 0) and passes host pointers: the port's RAM
 * is the host's (the RDRAM arena is contiguous memory at 0x80000000 |
 * physical, patches/017 moved every KSEG0 literal into it, and the K0 /
 * physical conversions are the identity on the Xbox), so an address is a
 * host pointer unless it is a small segmented one whose segment has a base. */
static inline void* ram(uint32_t a) {
    uint32_t s = (a >> 24) & 0x0F;
    if (a < 0x10000000u && s && R.seg[s]) a = R.seg[s] + (a & 0x00FFFFFFu);
    return wx_addr(a);
}

/* --------------------------------------------------------------- matrices */

/* N64 Mtx = { s16 i[4][4]; u16 f[4][4] }, written by the game as u16
 * arrays (uvMat4CopyF2L) and swapped per u16 from the ROM (patches/013), so
 * on a little-endian host element e is halfword e (wings64-dc traps.md). */
static void mtx_from_n64(float out[4][4], const void* m) {
    const int16_t* ip = (const int16_t*)m;
    const uint16_t* fp = (const uint16_t*)m + 16;
    int e;
    for (e = 0; e < 16; e++) out[e >> 2][e & 3] = (float)(((int32_t)ip[e] << 16) | (int32_t)fp[e]) * (1.0f / 65536.0f);
}

static void mtx_mul(float out[4][4], const float a[4][4], const float b[4][4]) {   /* out = a x b */
    float t[4][4];
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
    memcpy(out, t, sizeof t);
}

static void do_mtx(uint32_t w0, uint32_t w1) {
    float m[4][4];
    uint8_t p = (w0 >> 16) & 0xFF;
    mtx_from_n64(m, ram(w1));
    if (p & F3D_G_MTX_PROJECTION) {
        if (p & F3D_G_MTX_LOAD) memcpy(R.proj, m, sizeof m);
        else mtx_mul(R.proj, m, R.proj);
    } else {
        if ((p & F3D_G_MTX_PUSH) && R.mv_top < MTX_DEPTH - 1) {
            memcpy(R.mv[R.mv_top + 1], R.mv[R.mv_top], sizeof m);
            R.mv_top++;
        }
        if (p & F3D_G_MTX_LOAD) memcpy(R.mv[R.mv_top], m, sizeof m);
        else mtx_mul(R.mv[R.mv_top], m, R.mv[R.mv_top]);
    }
    R.mvp_dirty = 1;
}

/* --------------------------------------------------------------- vertices */

/* Lighting and spherical texgen (rare in PW64: G_LIGHTING only with
 * G_TEXTURE_GEN + DECALRGB; Birdman64 gfx.md). Eye-space normal. */
static void vtx_lit(Vtx* v, const uint8_t* cn) {
    const float(*m)[4] = R.mv[R.mv_top];
    float nx = (int8_t)cn[0], ny = (int8_t)cn[1], nz = (int8_t)cn[2];
    float ex = nx * m[0][0] + ny * m[1][0] + nz * m[2][0];
    float ey = nx * m[0][1] + ny * m[1][1] + nz * m[2][1];
    float ez = nx * m[0][2] + ny * m[1][2] + nz * m[2][2];
    float len = sqrtf(ex * ex + ey * ey + ez * ez);
    if (len > 1e-6f) { ex /= len; ey /= len; ez /= len; }
    if (R.geom & F3D_G_LIGHTING) {
        int amb = R.num_lights < 8 ? R.num_lights : 8, l;
        float r = R.light_col[amb][0], g = R.light_col[amb][1], b = R.light_col[amb][2];
        for (l = 0; l < R.num_lights && l < 8; l++) {
            float lx = R.light_dir[l][0], ly = R.light_dir[l][1], lz = R.light_dir[l][2];
            float ll = sqrtf(lx * lx + ly * ly + lz * lz), d;
            if (ll < 1e-6f) continue;
            d = (ex * lx + ey * ly + ez * lz) / ll;
            if (d > 0.0f) { r += d * R.light_col[l][0]; g += d * R.light_col[l][1]; b += d * R.light_col[l][2]; }
        }
        v->r = r > 255.0f ? 1.0f : r * (1.0f / 255.0f);
        v->g = g > 255.0f ? 1.0f : g * (1.0f / 255.0f);
        v->b = b > 255.0f ? 1.0f : b * (1.0f / 255.0f);
    } else {
        v->r = cn[0] * (1.0f / 255.0f);
        v->g = cn[1] * (1.0f / 255.0f);
        v->b = cn[2] * (1.0f / 255.0f);
    }
    if (R.geom & F3D_G_TEXTURE_GEN) {   /* spherical env map: (n_eye + 1) / 4 of the texture scale */
        v->s = (ex + 1.0f) * 0.25f * R.tex_ss * (1.0f / 32.0f);
        v->t = (ey + 1.0f) * 0.25f * R.tex_st * (1.0f / 32.0f);
    }
}

static void do_vtx(uint32_t w0, uint32_t w1) {
    uint32_t n, v0, len, addr, i;
    const uint8_t* src;
    f3d_vtx_decode(w0, w1, &n, &v0, &len, &addr);
    src = (const uint8_t*)ram(addr);
    if (R.mvp_dirty) {
        mtx_mul(R.mvp, R.mv[R.mv_top], R.proj);   /* row vectors: v MV P */
        R.mvp_dirty = 0;
    }
    for (i = 0; i < n && v0 + i < 16; i++, src += 16) {
        const int16_t* ob = (const int16_t*)src;
        const int16_t* tc = (const int16_t*)(src + 8);
        const uint8_t* cn = src + 12;
        Vtx* v = &R.vc[v0 + i];
        float x = ob[0], y = ob[1], z = ob[2];
        const float(*m)[4] = R.mvp;
        uint32_t o = 0;
        float gw;
        v->x = x * m[0][0] + y * m[1][0] + z * m[2][0] + m[3][0];
        v->y = x * m[0][1] + y * m[1][1] + z * m[2][1] + m[3][1];
        v->z = x * m[0][2] + y * m[1][2] + z * m[2][2] + m[3][2];
        v->w = x * m[0][3] + y * m[1][3] + z * m[2][3] + m[3][3];
        gw = GUARD * v->w;
        if (v->x > v->w) o |= 1;
        if (v->x < -v->w) o |= 2;
        if (v->y > v->w) o |= 4;
        if (v->y < -v->w) o |= 8;
        if (v->z < -v->w) o |= 16;
        if (v->z + v->w < 0.0f || v->x > gw || v->x < -gw || v->y > gw || v->y < -gw) o |= 32;
        if (XGFX_FAR_CLIP && v->z > v->w) o |= 64 | 32;
        v->oc = o;
        if (R.geom & (F3D_G_LIGHTING | F3D_G_TEXTURE_GEN)) {
            vtx_lit(v, cn);
        } else {
            v->r = cn[0] * (1.0f / 255.0f);
            v->g = cn[1] * (1.0f / 255.0f);
            v->b = cn[2] * (1.0f / 255.0f);
        }
        v->a = cn[3] * (1.0f / 255.0f);
        if (!(R.geom & F3D_G_TEXTURE_GEN)) {
            v->s = tc[0] * R.tex_ss * (1.0f / 65536.0f) * (1.0f / 32.0f);
            v->t = tc[1] * R.tex_st * (1.0f / 65536.0f) * (1.0f / 32.0f);
        }
        if ((R.geom & F3D_G_FOG) && v->w > 0.0f) {
            float f = v->z / v->w * R.fog_m + R.fog_o;
            v->fog = f < 0.0f ? 0.0f : f > 255.0f ? 255.0f : f;
        } else {
            v->fog = 0.0f;
        }
    }
}

/* ------------------------------------------------------------- output map */

/* N64 viewport pixels -> framebuffer pixels: x * s_sx + s_ox. The content
 * rect is the whole framebuffer at 4:3, a centred 4:3 box in a 16:9 one
 * (PLAN.md 3.2; widescreen comes in M7). */
static float s_sx = 2.0f, s_sy = 2.0f, s_ox, s_oy;
static XgfxOutput s_out;
static float s_cx, s_cy, s_cw = 640.0f, s_ch = 480.0f;   /* content rect */
static int s_title;   /* the title screen this frame (title fit) */
#define TITLE_MARGIN 12.0f   /* output pixels at 480; scaled with the content rect */

static void map_setup(void) {
    float aspect;
    xgfx_output(&s_out);
    aspect = (float)s_out.fb_w / (float)s_out.fb_h;
    s_ch = (float)s_out.fb_h;
    s_cw = aspect > 1.4f ? s_ch * 4.0f / 3.0f : (float)s_out.fb_w;   /* DIVERGES: none (4:3 box) until M7 */
    s_cx = ((float)s_out.fb_w - s_cw) * 0.5f;
    s_cy = 0.0f;
    if (XGFX_TITLE_FIT && s_title) {
        /* the title composes its picture inside SUBSCREEN (10..310 x 8..222);
         * DIVERGES: fitted to the content rect with a black margin, as the DC port */
        float m = TITLE_MARGIN * s_ch / 480.0f;
        s_sx = (s_cw - 2.0f * m) / 300.0f;
        s_ox = s_cx + m - 10.0f * s_sx;
        s_sy = (s_ch - 2.0f * m) / 214.0f;
        s_oy = s_cy + m - 8.0f * s_sy;
    } else {
        s_sx = s_cw / N64_W;
        s_sy = s_ch / N64_H;
        s_ox = s_cx;
        s_oy = s_cy;
    }
}

/* SUBSCREEN edges in N64 pixels, 2 pixels of slack (the HUD fade box lands
 * at 18.0x, the fade-out quad at 15.4; wings64-dc traps.md) */
#define SUB_X0 12.0f
#define SUB_X1 308.0f
#define SUB_Y0 10.0f
#define SUB_Y1 220.0f

/* ------------------------------------------------------------- draw state */

static XgfxState s_st;           /* the state the batch below is drawn with */
static uint32_t s_epoch = 1, s_st_epoch;
#define DIRTY() (s_epoch++)
static int s_textured, s_tex1, s_decal, s_overlay, s_shadow_pass2;
static float s_u_scale[2], s_u_off[2], s_v_scale[2], s_v_off[2];
static unsigned s_zorder;
static float s_scissor[4] = { 0.0f, 0.0f, 320.0f, 240.0f };
static int s_sc_on;
static float s_sc[4];            /* the clip rectangle when s_sc_on, N64 pixels */

#define BATCH_TRIS 2048
static XgfxVtx s_batch[BATCH_TRIS * 3];
static uint32_t s_nb;

static int s_trace;   /* -DXGFX_TRACE_FRAME=<n>: frame n's draws as [DRAW] lines */
#ifndef XGFX_TRACE_FRAME
#define XGFX_TRACE_FRAME 0
#endif

static void flush(void) {
    if (!s_nb) return;
    if (s_trace) {
        uint32_t i;
        float zlo = 2e9f, zhi = -2e9f;
        for (i = 0; i < s_nb * 3; i++) {
            if (s_batch[i].z < zlo) zlo = s_batch[i].z;
            if (s_batch[i].z > zhi) zhi = s_batch[i].z;
        }
        xhw_logf("[DRAW] z %d..%d (x1000)", (int)(zlo * 1000.0f / s_out.zmax), (int)(zhi * 1000.0f / s_out.zmax));
    }
    if (s_trace)
        xhw_logf("[DRAW] %u tris cc %06x:%08x cyc %u tex %u/%u blend %u aref %u depth %u/%u bias %u sten %u fog %u "
                 "prim %02x%02x%02x%02x env %02x%02x%02x%02x geom %06x oml %08x v0 %d,%d z %d a %u",
                 (unsigned)s_nb, (unsigned)s_st.cc0, (unsigned)s_st.cc1, (unsigned)s_st.two_cycle + 1,
                 (unsigned)s_st.tex[0], (unsigned)s_st.tex[1], (unsigned)s_st.blend, (unsigned)s_st.alpha_ref,
                 (unsigned)s_st.depth_func, (unsigned)s_st.depth_write, (unsigned)s_st.depth_bias,
                 (unsigned)s_st.stencil, (unsigned)s_st.fog, s_st.prim[0], s_st.prim[1], s_st.prim[2], s_st.prim[3],
                 s_st.env[0], s_st.env[1], s_st.env[2], s_st.env[3], (unsigned)R.geom, (unsigned)R.oml,
                 (int)s_batch[0].x, (int)s_batch[0].y, (int)(s_batch[0].z * 1000.0f / s_out.zmax), s_batch[0].rgba[3]);
    xgfx_draw(&s_st, s_batch, s_nb);
    if (s_trace) {   /* the first draw nearer than 0.9: everything far is down by now */
        static int dumped;
        if (!dumped && s_batch[0].z < 0.9f * s_out.zmax && s_st.depth_func != XGFX_DEPTH_OFF) {
            dumped = 1;
            xhw_log("[DRAW] dump before this draw's effect (the far scene)");
            xgfx_debug_dump();
        }
    }
    s_nb = 0;
}

/* --- textures: a cache keyed by RAM address, tile key and a content sample --- */
#define TEX_CACHE 1024
typedef struct {
    uint32_t addr, key, sample, frame;
    uint32_t handle;
    uint16_t w, h, pw, ph;
} TexEntry;
static TexEntry s_texc[TEX_CACHE];
static uint32_t s_frame_no = 1;

static uint32_t sample_hash(const uint8_t* p, uint32_t bytes) {
    uint32_t h = 2166136261u, step = bytes > 64 ? bytes / 16 : 4, i, w;
    for (i = 0; i + 4 <= bytes; i += step) {
        memcpy(&w, p + (i & ~3u), 4);
        h ^= w;
        h *= 16777619u;
    }
    return h ^ bytes;
}

/* level load: the game reuses its heap, so everything goes (wings64-dc traps.md) */
void f3d_tex_reset(void) {
    int i;
    flush();
    for (i = 0; i < TEX_CACHE; i++)
        if (s_texc[i].handle) xgfx_tex_destroy(s_texc[i].handle);
    memset(s_texc, 0, sizeof s_texc);
}

static TexEntry* tex_evict_slot(unsigned slot) {
    TexEntry* e = &s_texc[slot];
    if (e->handle) xgfx_tex_destroy(e->handle);
    memset(e, 0, sizeof *e);
    return e;
}

#if XGFX_TEX_MEMO
#define MEMO 64
static struct { uint32_t frame, addr, key; TexEntry* e; } s_memo[MEMO];
#endif

static void tex_miss(const char* why, int tidx);

/* The texture tile `tidx` samples: converted, padded to powers of two,
 * uploaded once and cached. NULL when it can't be made. */
static TexEntry* texture_for_tile(int tidx) {
    static uint8_t raw[8192];
    static uint32_t conv[1024 * 64];
    Tile* t = &R.tile[tidx];
    TmemLoad* ld = &R.loads[(t->tmem >> 3) & 63];
    unsigned w = ((t->lrs - t->uls) >> 2) + 1, h = ((t->lrt - t->ult) >> 2) + 1;
    /* RGBA32 splits each texel across TMEM's two banks: SETTILE's line covers half a row */
    unsigned line = t->line * 8u * (t->siz == 3 ? 2u : 1u), bytes, pw, ph, slot, i;
    uint32_t key, sample;
    TexEntry* e;
    if (!ld->valid || t->line == 0) { tex_miss("no load", tidx); return NULL; }
    if (t->masks && (1u << t->masks) < w) w = 1u << t->masks;
    if (t->maskt && (1u << t->maskt) < h) h = 1u << t->maskt;
    /* tiles may declare more rows than were loaded (UVBT, fonts): only loaded rows exist */
    if (h > ld->bytes / line) h = ld->bytes / line;
    if (w == 0 || h == 0 || w > 1024 || h > 1024) { tex_miss("size", tidx); return NULL; }
    bytes = line * h;
    if (bytes > sizeof raw) { tex_miss("too big", tidx); return NULL; }
    key = t->fmt | t->siz << 3 | (w - 1) << 5 | (h - 1) << 15 | (uint32_t)(line & 0xFF) << 24 | (uint32_t)(ld->dxt != 0) << 31;
#if XGFX_TEX_MEMO
    {
        unsigned m = ((ld->addr >> 3) ^ (key * 0x9E3779B1u >> 26)) & (MEMO - 1);
        if (s_memo[m].frame == s_frame_no && s_memo[m].addr == ld->addr && s_memo[m].key == key && s_memo[m].e &&
            s_memo[m].e->addr == ld->addr && s_memo[m].e->key == key)
            return s_memo[m].e;
    }
#endif
    sample = sample_hash((const uint8_t*)ram(ld->addr), ld->tile ? ld->row_bytes : bytes) ^
             (ld->tile ? sample_hash((const uint8_t*)ram(ld->addr) + (ld->rows - 1u) * ld->stride, ld->row_bytes) : 0);
    slot = ((ld->addr >> 3) * 2654435761u ^ key * 40503u) % TEX_CACHE;
    for (i = 0; i < 8; i++) {   /* 8-way probe */
        e = &s_texc[(slot + i) % TEX_CACHE];
        if (e->handle && e->addr == ld->addr && e->key == key && e->sample == sample) goto hit;
    }
    {   /* a free slot, else the least recently used of the eight */
        unsigned pick = slot;
        for (i = 0; i < 8; i++) {
            TexEntry* c = &s_texc[(slot + i) % TEX_CACHE];
            if (!c->handle) { pick = (slot + i) % TEX_CACHE; break; }
            if (c->frame < s_texc[pick].frame) pick = (slot + i) % TEX_CACHE;
        }
        e = tex_evict_slot(pick);
    }
    if (ld->tile) {   /* LOADTILE: the rectangle's rows, laid out at the tile's line */
        const uint8_t* src = (const uint8_t*)ram(ld->addr);
        unsigned r, n = ld->row_bytes < line ? ld->row_bytes : line;
        memset(raw, 0, bytes);
        for (r = 0; r < h && r < ld->rows; r++) memcpy(raw + r * line, src + r * ld->stride, n);
    } else {
        memcpy(raw, ram(ld->addr), (bytes + 7) & ~7u);
    }
    if (ld->tile) {
    } else if (ld->dxt == 0) texconv_deswizzle(raw, line, bytes / line);   /* stored in TMEM order */
#if XGFX_DXT_EXACT
    else if (t->siz != 3 && (uint32_t)ld->dxt * (line / 8) != 2048u)
        texconv_deswizzle_dxt(raw, line, bytes / line, ld->dxt);
#endif
    pw = texconv_pow2(w);
    ph = texconv_pow2(h);
    if (pw * ph > sizeof conv / 4) { tex_miss("too big", tidx); return NULL; }
    if (!texconv_to_argb8(raw, line, w, h, (n64_fmt_t)t->fmt, (n64_siz_t)t->siz, conv, pw, ph)) {
        tex_miss("format", tidx);
        return NULL;
    }
    e->handle = xgfx_tex_create(pw, ph, conv);
    if (!e->handle) {   /* pool full: drop everything not used this frame and try once more */
        for (i = 0; i < TEX_CACHE; i++)
            if (s_texc[i].handle && s_texc[i].frame != s_frame_no && &s_texc[i] != e) tex_evict_slot(i);
        e->handle = xgfx_tex_create(pw, ph, conv);
        if (!e->handle) { tex_miss("pool full", tidx); return NULL; }
    }
    e->addr = ld->addr;
    e->key = key;
    e->sample = sample;
    e->w = (uint16_t)w;
    e->h = (uint16_t)h;
    e->pw = (uint16_t)pw;
    e->ph = (uint16_t)ph;
hit:
    e->frame = s_frame_no;
#if XGFX_TEX_MEMO
    {
        unsigned m = ((ld->addr >> 3) ^ (key * 0x9E3779B1u >> 26)) & (MEMO - 1);
        s_memo[m].frame = s_frame_no;
        s_memo[m].addr = ld->addr;
        s_memo[m].key = key;
        s_memo[m].e = e;
    }
#endif
    return e;
}

/* the first misses of each kind, for the log */
static void tex_miss(const char* why, int tidx) {
    static unsigned s_n;
    const Tile* t = &R.tile[tidx & 7];
    const TmemLoad* ld = &R.loads[(t->tmem >> 3) & 63];
    if (s_n++ >= 48) return;
    xhw_logf("[GFX] texture miss (%s): tile %d fmt %d siz %d line %d tmem %d s %d-%d t %d-%d mask %d/%d | load %d "
             "addr %08x bytes %u dxt %d",
             why, tidx, t->fmt, t->siz, t->line, t->tmem, t->uls >> 2, t->lrs >> 2, t->ult >> 2, t->lrt >> 2, t->masks,
             t->maskt, ld->valid, (unsigned)ld->addr, (unsigned)ld->bytes, ld->dxt);
}

static float shift_scale(int sh) {
    if (sh == 0) return 1.0f;
    if (sh <= 10) return 1.0f / (float)(1 << sh);
    return (float)(1 << (16 - sh));
}

static uint32_t wrap_of(uint8_t cm) {
    return (cm & 2) ? XGFX_WRAP_CLAMP : (cm & 1) ? XGFX_WRAP_MIRROR : XGFX_WRAP_REPEAT;
}

/* does the combiner read TEXEL1 (mux value 2 in an rgb slot, 9 in C, 2 in an alpha slot)? */
static int cc_uses_texel1(void) {
    uint8_t f[2][8];
    int cy, k;
    f3d_cc_decode(R.cc0, R.cc1, f);
    for (cy = 0; cy < 2; cy++) {
        for (k = 0; k < 4; k++)
            if (f[cy][k] == 2 || (k == 2 && f[cy][k] == 9)) return 1;
        for (k = 4; k < 8; k++)
            if (f[cy][k] == 2) return 1;
    }
    return 0;
}

static void bind_unit(int u, int tidx) {
    Tile* t = &R.tile[tidx & 7];
    TexEntry* e = texture_for_tile(tidx & 7);
    s_st.tex[u] = e ? e->handle : 0;
    if (!e) return;
    s_st.wrap_s[u] = wrap_of(t->cms);
    s_st.wrap_t[u] = wrap_of(t->cmt);
    s_st.filter[u] = ((R.omh >> F3D_G_MDSFT_TEXTFILT) & 3) != 0;
    /* u = (s * shift - uls) / padded width */
    s_u_scale[u] = shift_scale(t->shifts) / (float)e->pw;
    s_u_off[u] = t->uls * 0.25f / (float)e->pw;
    s_v_scale[u] = shift_scale(t->shiftt) / (float)e->ph;
    s_v_off[u] = t->ult * 0.25f / (float)e->ph;
}

/* N64 render mode -> XgfxState pixel choices (wings64-dc draw_state) */
static void pixel_state(int depth_override_always) {
    uint32_t rm = R.oml;
    int cycle = (R.omh >> F3D_G_MDSFT_CYCLETYPE) & 3;
    int zb = (R.geom & F3D_G_ZBUFFER) && !depth_override_always;
    s_st.blend = ((rm & F3D_FORCE_BL) && cycle < 2) ? XGFX_BLEND_ALPHA : XGFX_BLEND_OFF;
    if ((rm & 3) == 1)   /* G_AC_THRESHOLD: against the blend colour's alpha */
        s_st.alpha_ref = R.blend[3] ? R.blend[3] : 1;
    else if ((rm & F3D_CVG_X_ALPHA) && cycle < 2)
        s_st.alpha_ref = 128;   /* coverage x alpha cutouts: alpha test at 0.5 */
    else
        s_st.alpha_ref = 0;
    s_st.depth_func = zb && (rm & F3D_Z_CMP) ? XGFX_DEPTH_LEQUAL : (zb && (rm & F3D_Z_UPD)) ? XGFX_DEPTH_ALWAYS
                                                                                              : XGFX_DEPTH_OFF;
    s_st.depth_write = zb && (rm & F3D_Z_UPD);
#ifdef XGFX_DEBUG_NOZ
    s_st.depth_func = XGFX_DEPTH_OFF;
    s_st.depth_write = 0;
#endif
    s_decal = ((rm >> 10) & 3) == 3;
    s_st.depth_bias = 0;
    if (s_st.depth_func == XGFX_DEPTH_LEQUAL) {
        /* DIVERGES: later depth-tested states are pulled toward the eye by a
         * step each (64 at most), so coplanar layers (the landing pads'
         * bullseye) resolve to the later draw as on the RDP */
        if (XGFX_Z_TIEBREAK && s_zorder < 64) s_zorder++;
        s_st.depth_bias = (XGFX_Z_TIEBREAK ? s_zorder : 0) + (s_decal ? 16u : 0u);
    }
    s_st.color_write = 1;
    s_st.stencil = XGFX_STENCIL_OFF;
    s_st.fog = (R.geom & F3D_G_FOG) && cycle < 2;
    memcpy(s_st.fog_color, R.fogc, 4);
    memcpy(s_st.prim, R.prim, 4);
    memcpy(s_st.env, R.env, 4);
    s_st.prim_lod_frac = R.prim_lod_frac;
    s_st.lod_frac = 0;   /* DIVERGES: one mip level is drawn (PLAN.md D4 phase A) */
    {   /* the clip rectangle: the content rect, or a small scissor/viewport */
        float x0 = s_sc_on ? s_sc[0] : 0.0f, y0 = s_sc_on ? s_sc[1] : 0.0f;
        float x1 = s_sc_on ? s_sc[2] : N64_W, y1 = s_sc_on ? s_sc[3] : N64_H;
        if (s_sc_on) {
            s_st.scissor[0] = (int32_t)(x0 * s_sx + s_ox + 0.5f);
            s_st.scissor[1] = (int32_t)(y0 * s_sy + s_oy + 0.5f);
            s_st.scissor[2] = (int32_t)(x1 * s_sx + s_ox + 0.5f);
            s_st.scissor[3] = (int32_t)(y1 * s_sy + s_oy + 0.5f);
        } else {
            s_st.scissor[0] = (int32_t)s_cx;
            s_st.scissor[1] = (int32_t)s_cy;
            s_st.scissor[2] = (int32_t)(s_cx + s_cw);
            s_st.scissor[3] = (int32_t)(s_cy + s_ch);
        }
    }
}

/* the combiner reads no texel: a texture bound under a shade-only combiner (sky gradients) */
static int cc_reads_texel0(void) {
    uint8_t f[2][8];
    int cy, k, cycles = ((R.omh >> F3D_G_MDSFT_CYCLETYPE) & 3) == 1 ? 2 : 1;
    f3d_cc_decode(R.cc0, R.cc1, f);
    for (cy = 0; cy < cycles; cy++) {
        for (k = 0; k < 4; k++)
            if (f[cy][k] == 1 || f[cy][k] == 2 || (k == 2 && (f[cy][k] == 8 || f[cy][k] == 9))) return 1;
        for (k = 4; k < 8; k++)
            if (f[cy][k] == 1 || f[cy][k] == 2) return 1;
    }
    return 0;
}

static void draw_state(void) {
    int cycle = (R.omh >> F3D_G_MDSFT_CYCLETYPE) & 3;
    flush();
    memset(&s_st, 0, sizeof s_st);
    s_st.cc0 = R.cc0 & 0x00FFFFFFu;
    s_st.cc1 = R.cc1;
    s_st.two_cycle = cycle == 1;
    pixel_state(0);
    s_textured = s_tex1 = 0;
    if (R.tex_on) {
        bind_unit(0, R.tex_tile);
        s_textured = s_st.tex[0] != 0;
        if (s_st.two_cycle && cc_uses_texel1()) {
            bind_unit(1, R.tex_tile + 1);
            s_tex1 = s_st.tex[1] != 0;
        }
    }
    s_overlay = XGFX_OVERLAY_FULL && s_st.depth_func == XGFX_DEPTH_OFF && (!s_textured || !cc_reads_texel0());
    if (R.cimg == R.zimg && R.cimg) {   /* shadow volume pass 1 (shadow volumes below) */
        s_st.stencil = XGFX_STENCIL_MARK;
        s_st.color_write = 0;
        s_st.depth_write = 0;
        s_st.blend = XGFX_BLEND_OFF;
        s_st.alpha_ref = 0;
        s_st.depth_func = XGFX_DEPTH_LEQUAL;
        s_st.depth_bias = 0;
    } else if (s_shadow_pass2) {
        s_st.stencil = XGFX_STENCIL_TEST;
    }
    s_st_epoch = s_epoch;
}

/* --------------------------------------------------------------- triangles */

static uint8_t to8(float f) {
    int i = (int)(f * 255.0f + 0.5f);
    return (uint8_t)(i < 0 ? 0 : i > 255 ? 255 : i);
}

static void to_out(const Vtx* v, XgfxVtx* o, const Vtx* flat) {
    float iw = 1.0f / v->w;
    float z01 = (v->z * iw + 1.0f) * 0.5f;
    const Vtx* c = flat ? flat : v;
    o->x = (R.vp_tx + v->x * iw * R.vp_sx) * s_sx + s_ox;
    o->y = (R.vp_ty - v->y * iw * R.vp_sy) * s_sy + s_oy;
    o->z = (z01 < 0.0f ? 0.0f : z01 > 1.0f ? 1.0f : z01) * s_out.zmax;
#ifdef XGFX_DEBUG_ZHALF
    o->z *= 0.5f;
#endif
    o->w = v->w;
    o->rgba[0] = to8(c->r);
    o->rgba[1] = to8(c->g);
    o->rgba[2] = to8(c->b);
    o->rgba[3] = (R.geom & F3D_G_FOG) ? 255 : to8(c->a);   /* the RSP puts fog in shade alpha; the combiner sees 1 */
    o->s0 = v->s * s_u_scale[0] - s_u_off[0];
    o->t0 = v->t * s_v_scale[0] - s_v_off[0];
    o->s1 = v->s * s_u_scale[1] - s_u_off[1];
    o->t1 = v->t * s_v_scale[1] - s_v_off[1];
    o->fog = v->fog * (1.0f / 255.0f);
}

/* SUBSCREEN-sized overlay (fades, the HUD's fade box): stretched to the
 * content rect, since the port draws 3D views unscissored to the edges.
 * DIVERGES: as wings64-dc's overlay_full. */
static void overlay_full(XgfxVtx* p, int n) {
    float x0 = p[0].x, x1 = p[0].x, y0 = p[0].y, y1 = p[0].y;
    float sx0 = SUB_X0 * s_sx + s_ox, sx1 = SUB_X1 * s_sx + s_ox, sy0 = SUB_Y0 * s_sy + s_oy, sy1 = SUB_Y1 * s_sy + s_oy;
    int i;
    if (s_title) return;
    for (i = 1; i < n; i++) {
        if (p[i].x < x0) x0 = p[i].x;
        if (p[i].x > x1) x1 = p[i].x;
        if (p[i].y < y0) y0 = p[i].y;
        if (p[i].y > y1) y1 = p[i].y;
    }
    if (x0 > sx0 || x1 < sx1 || y0 > sy0 || y1 < sy1) return;
    for (i = 0; i < n; i++) {
        if (p[i].x <= sx0) p[i].x = s_cx;
        else if (p[i].x >= sx1) p[i].x = s_cx + s_cw;
        if (p[i].y <= sy0) p[i].y = s_cy;
        else if (p[i].y >= sy1) p[i].y = s_cy + s_ch;
    }
}

static void emit_poly(const Vtx* poly, int n, const Vtx* flat) {
    XgfxVtx pv[12];
    int i;
    if (s_nb + (uint32_t)(n - 2) > BATCH_TRIS) flush();
    for (i = 0; i < n; i++) to_out(&poly[i], &pv[i], flat);
    if (s_overlay) overlay_full(pv, n);
    for (i = 1; i + 1 < n; i++) {
        XgfxVtx* d = &s_batch[s_nb * 3];
        d[0] = pv[0];
        d[1] = pv[i];
        d[2] = pv[i + 1];
        s_nb++;
    }
}

static void lerp_vtx(Vtx* o, const Vtx* a, const Vtx* b, float t) {
    const float *pa = &a->x, *pb = &b->x;
    float* po = &o->x;
    unsigned i;
    for (i = 0; i < VTX_LERP_FLOATS; i++) po[i] = pa[i] + (pb[i] - pa[i]) * t;
}

/* Sutherland-Hodgman in clip space against near (z + w >= 0), the x/y
 * guard band and far (z <= w); few triangles need it */
static void clip_tri(Vtx* const v[3], const Vtx* flat) {
    const uint32_t cullmask = F3D_G_CULL_FRONT | F3D_G_CULL_BACK;
    Vtx bufA[12], bufB[12], *in = bufA, *out = bufB, *tmp;
    int n = 3, plane, i;
    for (i = 0; i < 3; i++) in[i] = *v[i];
    for (plane = 0; plane < 6 && n >= 3; plane++) {
        int m = 0;
        for (i = 0; i < n; i++) {
            Vtx *a = &in[i], *b = &in[(i + 1) % n];
            float da, db;
            switch (plane) {
                case 0: da = a->z + a->w; db = b->z + b->w; break;
                case 1: da = GUARD * a->w - a->x; db = GUARD * b->w - b->x; break;
                case 2: da = GUARD * a->w + a->x; db = GUARD * b->w + b->x; break;
                case 3: da = GUARD * a->w - a->y; db = GUARD * b->w - b->y; break;
                case 4: da = GUARD * a->w + a->y; db = GUARD * b->w + b->y; break;
                default:
                    da = XGFX_FAR_CLIP ? a->w - a->z : 1.0f;
                    db = XGFX_FAR_CLIP ? b->w - b->z : 1.0f;
                    break;
            }
            if (da >= 0.0f && m < 12) out[m++] = *a;
            if ((da >= 0.0f) != (db >= 0.0f) && m < 12) lerp_vtx(&out[m++], a, b, da / (da - db));
        }
        n = m;
        tmp = in;
        in = out;
        out = tmp;
    }
    if (n < 3) return;
    for (i = 0; i < n; i++)
        if (in[i].w < 1e-5f) in[i].w = 1e-5f;
    if (R.geom & cullmask) {   /* all w > 0 now: the NDC area sign is the true orientation */
        float area = 0.0f;
        for (i = 0; i < n; i++) {
            int j = (i + 1) % n;
            area += (in[i].x / in[i].w) * (in[j].y / in[j].w) - (in[j].x / in[j].w) * (in[i].y / in[i].w);
        }
        if ((R.geom & F3D_G_CULL_BACK) && -area >= 0.0f) return;
        if ((R.geom & F3D_G_CULL_FRONT) && -area <= 0.0f) return;
    }
    emit_poly(in, n, flat);
}

static uint32_t s_t_in, s_t_rej, s_t_cull, s_t_clip, s_t_shadow, s_vtx_loads, s_mtx_loads;

static void do_tri(int ia, int ib, int ic, int flag) {
    Vtx* v[3] = { &R.vc[ia], &R.vc[ib], &R.vc[ic] };
    const Vtx* flat = (R.geom & F3D_G_SHADING_SMOOTH) ? NULL : v[flag < 3 ? flag : 0];
    int needs_clip;
    if (R.cimg == R.zimg && R.cimg && !XGFX_SHADOW_VOL) return;
    if (s_shadow_pass2 && !XGFX_SHADOW_VOL) return;
    s_t_in++;
    if (v[0]->oc & v[1]->oc & v[2]->oc & (31 | 64)) { s_t_rej++; return; }   /* all beyond one plane */
    needs_clip = ((v[0]->oc | v[1]->oc | v[2]->oc) & 32) != 0;
    if ((R.geom & (F3D_G_CULL_FRONT | F3D_G_CULL_BACK)) && !needs_clip) {
        float det = v[0]->x * (v[1]->y * v[2]->w - v[2]->y * v[1]->w) - v[0]->y * (v[1]->x * v[2]->w - v[2]->x * v[1]->w) +
                    v[0]->w * (v[1]->x * v[2]->y - v[2]->x * v[1]->y);
        float cross = -det;
        if ((R.geom & F3D_G_CULL_BACK) && cross >= 0.0f) { s_t_cull++; return; }
        if ((R.geom & F3D_G_CULL_FRONT) && cross <= 0.0f) { s_t_cull++; return; }
    }
    if (needs_clip) s_t_clip++;
    if (s_st_epoch != s_epoch) draw_state();
    if (!needs_clip) {
        Vtx tri[3] = { *v[0], *v[1], *v[2] };
        emit_poly(tri, 3, flat);
        return;
    }
    clip_tri(v, flat);
}

/* ------------------------------------------------------------ shadow volumes
 *
 * PW64 draws vehicle shadows as z-buffer shadow volumes (kernel/graphics.c
 * uvGfxStateDrawDL): pass 1 draws the volume's back faces with the colour
 * image pointed at the z-buffer (cimg == zimg), writing "nearest" wherever
 * they are visible; pass 2 calls the same display list again and draws the
 * front faces as dark translucent shade, z-tested, so only terrain inside
 * the volume darkens. Here pass 1 marks the stencil where the back faces
 * pass the depth test, and pass 2 (the second call of that display list)
 * draws only where nothing was marked, clearing the mark as it goes: the
 * same pixels as the RDP's trick, without touching the depth buffer. */
static uint32_t s_shadow_dl;   /* the display list pass 1 called */
static int s_shadow_rec, s_shadow_depth;

/* ------------------------------------------------------------- rectangles */

static uint32_t cc_words_const(int d_rgb, int d_a, uint32_t* cc1) {
    /* (0 - 0) * 0 + D in both cycles: a = 15, b = 15, c = 31, d (3 bits); alpha a = b = c = 7 */
    uint32_t w0 = (15u << 20) | (31u << 15) | (7u << 12) | (7u << 9) | (15u << 5) | 31u;
    *cc1 = (15u << 28) | ((uint32_t)d_rgb << 15) | (7u << 12) | ((uint32_t)d_a << 9) | (15u << 24) | (7u << 21) |
           (7u << 18) | ((uint32_t)d_rgb << 6) | (7u << 3) | (uint32_t)d_a;
    return w0;
}

static void screen_quad(float x0, float y0, float x1, float y1, float s0, float t0, float s1, float t1) {
    Vtx q[4];
    XgfxVtx o[4];
    int i;
    if (s_sc_on) {
        float nx0 = x0 < s_sc[0] ? s_sc[0] : x0, nx1 = x1 > s_sc[2] ? s_sc[2] : x1;
        float ny0 = y0 < s_sc[1] ? s_sc[1] : y0, ny1 = y1 > s_sc[3] ? s_sc[3] : y1;
        if (nx0 >= nx1 || ny0 >= ny1) return;
        if (x1 > x0) { float k = (s1 - s0) / (x1 - x0); s0 += (nx0 - x0) * k; s1 -= (x1 - nx1) * k; }
        if (y1 > y0) { float k = (t1 - t0) / (y1 - y0); t0 += (ny0 - y0) * k; t1 -= (y1 - ny1) * k; }
        x0 = nx0; x1 = nx1; y0 = ny0; y1 = ny1;
    }
    memset(q, 0, sizeof q);
    q[0].s = s0; q[0].t = t0;
    q[1].s = s1; q[1].t = t0;
    q[2].s = s0; q[2].t = t1;
    q[3].s = s1; q[3].t = t1;
    for (i = 0; i < 4; i++) {
        float x = (i & 1) ? x1 : x0, y = (i & 2) ? y1 : y0;
        o[i].x = x * s_sx + s_ox;
        o[i].y = y * s_sy + s_oy;
        o[i].z = 0.0f;
        o[i].w = 1.0f;
        o[i].rgba[0] = o[i].rgba[1] = o[i].rgba[2] = o[i].rgba[3] = 0;   /* RDP rectangles have no shade */
        o[i].s0 = q[i].s * s_u_scale[0] - s_u_off[0];
        o[i].t0 = q[i].t * s_v_scale[0] - s_v_off[0];
        o[i].s1 = q[i].s * s_u_scale[1] - s_u_off[1];
        o[i].t1 = q[i].t * s_v_scale[1] - s_v_off[1];
        o[i].fog = 0.0f;
    }
    if (s_overlay) overlay_full(o, 4);
    if (s_nb + 2 > BATCH_TRIS) flush();
    {
        XgfxVtx* d = &s_batch[s_nb * 3];
        d[0] = o[0]; d[1] = o[1]; d[2] = o[2];
        d[3] = o[1]; d[4] = o[3]; d[5] = o[2];
        s_nb += 2;
    }
}

static void do_fillrect(uint32_t w0, uint32_t w1) {
    uint16_t lrx, lry, ulx, uly;
    int cycle = (R.omh >> F3D_G_MDSFT_CYCLETYPE) & 3;
    f3d_fillrect_decode(w0, w1, &lrx, &lry, &ulx, &uly);
    if (cycle >= 2) { lrx++; lry++; }   /* fill/copy include the lower-right edge */
    if (R.cimg == R.zimg && R.cimg) {   /* z-buffer clear (Birdman64 gfx.md) */
        float x0 = ulx, y0 = uly, x1 = lrx, y1 = lry;
        flush();
        if (s_trace) xhw_logf("[DRAW] z clear %d,%d-%d,%d fill %08x", ulx, uly, lrx, lry, (unsigned)R.fill);
        if (x0 <= SUB_X0 && x1 >= SUB_X1 && y0 <= SUB_Y0 && y1 >= SUB_Y1) {   /* SUBSCREEN-sized: the whole picture */
            xgfx_clear((int32_t)s_cx, (int32_t)s_cy, (int32_t)(s_cx + s_cw), (int32_t)(s_cy + s_ch), 0, 0, 1, 1.0f);
        } else {
            xgfx_clear((int32_t)(x0 * s_sx + s_ox), (int32_t)(y0 * s_sy + s_oy), (int32_t)(x1 * s_sx + s_ox),
                       (int32_t)(y1 * s_sy + s_oy), 0, 0, 1, 1.0f);
        }
        DIRTY();
        return;
    }
    flush();
    memset(&s_st, 0, sizeof s_st);
    pixel_state(1);
    s_st.fog = 0;
    s_st.depth_bias = 0;
    s_textured = s_tex1 = 0;
    if (cycle == 3) {   /* G_CYC_FILL: the RGBA5551 fill colour, no blend */
        uint16_t c = (uint16_t)(R.fill & 0xFFFF);
        s_st.cc0 = cc_words_const(3 /* PRIM */, 3, &s_st.cc1);
        s_st.prim[0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
        s_st.prim[1] = (uint8_t)(((c >> 6) & 31) * 255 / 31);
        s_st.prim[2] = (uint8_t)(((c >> 1) & 31) * 255 / 31);
        s_st.prim[3] = 255;
        s_st.blend = XGFX_BLEND_OFF;
        s_st.alpha_ref = 0;
    } else {
        s_st.cc0 = R.cc0 & 0x00FFFFFFu;
        s_st.cc1 = R.cc1;
        s_st.two_cycle = cycle == 1;
    }
    s_overlay = XGFX_OVERLAY_FULL && !s_title;
    if (s_overlay && ulx <= SUB_X0 && lrx >= SUB_X1 && uly <= SUB_Y0 && lry >= SUB_Y1) {
        ulx = uly = 0;
        lrx = 320;
        lry = 240;
    }
    if (s_trace) xhw_logf("[DRAW] fillrect %d,%d-%d,%d cyc %d cimg %08x zimg %08x", ulx, uly, lrx, lry, cycle,
                          (unsigned)R.cimg, (unsigned)R.zimg);
    screen_quad(ulx, uly, lrx, lry, 0, 0, 0, 0);
    flush();
    DIRTY();
}

static void do_texrect(uint32_t w0, uint32_t w1) {
    uint16_t xl, yl, xh, yh;
    uint8_t tile;
    int16_t s, t, dsdx, dtdy;
    int cycle = (R.omh >> F3D_G_MDSFT_CYCLETYPE) & 3;
    float x0, y0, x1, y1, fs, ft, fdx, fdy;
    f3d_texrect_decode(w0, w1, &xl, &yl, &xh, &yh, &tile);
    f3d_texrect_half1_decode(R.half1, &s, &t);
    f3d_texrect_half2_decode(R.half2, &dsdx, &dtdy);
    x0 = xl * 0.25f; y0 = yl * 0.25f; x1 = xh * 0.25f; y1 = yh * 0.25f;
    if (cycle >= 2) { x1 += 1.0f; y1 += 1.0f; }
    fs = s / 32.0f;
    ft = t / 32.0f;
    fdx = dsdx / 1024.0f;
    fdy = dtdy / 1024.0f;
    if (cycle == 2) fdx *= 0.25f;   /* copy mode: dsdx is x4 */
    flush();
    memset(&s_st, 0, sizeof s_st);
    pixel_state(1);
    s_st.fog = 0;
    s_st.depth_bias = 0;
    if (cycle == 2) {   /* copy: the texel as it is */
        s_st.cc0 = cc_words_const(1 /* TEXEL0 */, 1, &s_st.cc1);
        s_st.blend = XGFX_BLEND_OFF;
    } else {
        s_st.cc0 = R.cc0 & 0x00FFFFFFu;
        s_st.cc1 = R.cc1;
        s_st.two_cycle = cycle == 1;
    }
    bind_unit(0, tile);
    s_textured = s_st.tex[0] != 0;
    s_tex1 = 0;
    if (s_st.two_cycle && cc_uses_texel1()) {
        bind_unit(1, tile + 1);
        s_tex1 = s_st.tex[1] != 0;
    }
    if (s_textured && (cycle == 2 || (x1 - x0) * fdx < 64.0f)) s_st.filter[0] = 0;   /* 1:1 blits (gfx.md) */
    s_overlay = XGFX_OVERLAY_FULL && !s_textured;
    screen_quad(x0, y0, x1, y1, fs, ft, fs + (x1 - x0) * fdx, ft + (y1 - y0) * fdy);
    flush();
    DIRTY();
}

/* ------------------------------------------------------------ scissor */

/* A scissor or viewport clearly smaller than the SUBSCREEN view (the photo
 * album's 80x59 thumbnails, its 200x150 viewer) is honoured; the game
 * resets the scissor to the full screen after setting a thumbnail's, and
 * the RSP clips to the viewport anyway, so a small viewport alone clips */
static void clip_rect_update(void) {
    float x0 = s_scissor[0], y0 = s_scissor[1], x1 = s_scissor[2], y1 = s_scissor[3];
    int sc_small = x1 - x0 < 280.0f || y1 - y0 < 190.0f;
    int vp_small = R.vp_sx < 140.0f || R.vp_sy < 95.0f;
    if (!sc_small) { x0 = 0.0f; y0 = 0.0f; x1 = N64_W; y1 = N64_H; }
    if (vp_small) {
        if (R.vp_tx - R.vp_sx > x0) x0 = R.vp_tx - R.vp_sx;
        if (R.vp_tx + R.vp_sx < x1) x1 = R.vp_tx + R.vp_sx;
        if (R.vp_ty - R.vp_sy > y0) y0 = R.vp_ty - R.vp_sy;
        if (R.vp_ty + R.vp_sy < y1) y1 = R.vp_ty + R.vp_sy;
    }
    s_sc[0] = x0; s_sc[1] = y0; s_sc[2] = x1; s_sc[3] = y1;
    s_sc_on = XGFX_SCISSOR && (sc_small || vp_small);
    DIRTY();
}

/* ------------------------------------------------------------ interpreter */

static uint32_t s_unknown[256];

static void run_dl(const uint32_t* pc) {
    const uint32_t* stack[DL_DEPTH];
    int sp = 0;
    for (;;) {
        uint32_t w0 = pc[0], w1 = pc[1];
        pc += 2;
        switch (w0 >> 24) {
            case F3D_G_TEXTURE: case F3D_G_SETOTHERMODE_H: case F3D_G_SETOTHERMODE_L: case F3D_G_SETGEOMETRYMODE:
            case F3D_G_CLEARGEOMETRYMODE: case F3D_G_SETTILE: case F3D_G_SETTILESIZE: case F3D_G_LOADBLOCK:
            case F3D_G_SETCOMBINE: case F3D_G_SETTIMG: case F3D_G_SETFOGCOLOR: case F3D_G_SETBLENDCOLOR:
                DIRTY();
                break;
        }
        switch (w0 >> 24) {
            case F3D_G_MTX: s_mtx_loads++; do_mtx(w0, w1); break;
            case F3D_G_POPMTX:
                if (R.mv_top > 0) { R.mv_top--; R.mvp_dirty = 1; }
                break;
            case F3D_G_VTX: s_vtx_loads++; do_vtx(w0, w1); break;
            case F3D_G_TRI1: {
                uint8_t a, b, c, f;
                f3d_tri1_decode(w1, &a, &b, &c, &f);
                if (a < 16 && b < 16 && c < 16) do_tri(a, b, c, f);
                break;
            }
            case F3D_G_DL: {
                uint8_t push;
                uint32_t addr;
                f3d_dl_decode(w0, w1, &push, &addr);
                if (R.cimg == R.zimg && R.cimg) {   /* shadow pass 1: the list pass 2 will call again */
                    if (!s_shadow_rec) { s_shadow_dl = addr; s_shadow_rec = 1; }
                } else if (s_shadow_dl && addr == s_shadow_dl && push && !s_shadow_pass2) {
                    /* pass 2: draw where pass 1 marked nothing, until this call returns */
                    s_shadow_dl = 0;
                    s_shadow_pass2 = 1;
                    s_shadow_depth = sp + 1;
                    DIRTY();
                }
                if (push) {
                    if (sp == DL_DEPTH) return;
                    stack[sp++] = pc;
                }
                pc = (const uint32_t*)ram(addr);
                break;
            }
            case F3D_G_ENDDL:
                if (s_shadow_pass2 && sp == s_shadow_depth) {
                    s_shadow_pass2 = 0;
                    DIRTY();
                }
                if (sp == 0) return;
                pc = stack[--sp];
                break;
            case F3D_G_MOVEMEM: {
                uint8_t idx;
                uint32_t len, addr;
                const uint8_t* m;
                f3d_movemem_decode(w0, w1, &idx, &len, &addr);
                m = (const uint8_t*)ram(addr);
                if (idx == F3D_G_MV_VIEWPORT) {
                    const int16_t* vp = (const int16_t*)m;
                    R.vp_sx = vp[0] * 0.25f; R.vp_sy = vp[1] * 0.25f; R.vp_tx = vp[4] * 0.25f; R.vp_ty = vp[5] * 0.25f;
                    clip_rect_update();
                } else if (idx >= F3D_G_MV_L0 && idx <= F3D_G_MV_L7) {
                    int l = (idx - F3D_G_MV_L0) / 2;
                    R.light_col[l][0] = m[0]; R.light_col[l][1] = m[1]; R.light_col[l][2] = m[2];
                    R.light_dir[l][0] = (int8_t)m[8]; R.light_dir[l][1] = (int8_t)m[9]; R.light_dir[l][2] = (int8_t)m[10];
                }
                break;
            }
            case F3D_G_MOVEWORD: {
                uint8_t idx;
                uint16_t off;
                uint32_t data;
                f3d_moveword_decode(w0, w1, &idx, &off, &data);
                if (idx == F3D_G_MW_SEGMENT) R.seg[(off >> 2) & 15] = data;
                else if (idx == F3D_G_MW_NUMLIGHT) R.num_lights = f3d_numlights_from_data(data);
                else if (idx == F3D_G_MW_FOG) f3d_fog_from_data(data, &R.fog_m, &R.fog_o);
                else if (idx == F3D_G_MW_LIGHTCOL) {
                    uint8_t l = f3d_lightcol_index(off);
                    if (l < 9) { R.light_col[l][0] = data >> 24; R.light_col[l][1] = data >> 16; R.light_col[l][2] = data >> 8; }
                }
                break;
            }
            case F3D_G_TEXTURE: {
                uint8_t lvl, tile, on;
                uint16_t s, t;
                f3d_texture_decode(w0, w1, &lvl, &tile, &on, &s, &t);
                R.tex_on = on;
                R.tex_tile = tile;
                R.tex_ss = s;
                R.tex_st = t;
                break;
            }
            case F3D_G_SETOTHERMODE_H: R.omh = f3d_othermode_apply(R.omh, w0, w1); break;
            case F3D_G_SETOTHERMODE_L: R.oml = f3d_othermode_apply(R.oml, w0, w1); break;
            case F3D_G_SETGEOMETRYMODE: R.geom |= w1; break;
            case F3D_G_CLEARGEOMETRYMODE: R.geom &= ~w1; break;
            case F3D_G_RDPHALF_1: R.half1 = w1; break;
            case F3D_G_RDPHALF_2: R.half2 = w1; break;
            case F3D_G_RDPHALF_CONT: break;
            case F3D_G_TEXRECT:
            case F3D_G_TEXRECTFLIP:
                /* PW64's F3D emits E4, B3 (s,t), B2 (dsdx,dtdy); current gbi.h says E4, B4, B3:
                 * take the two halves that follow, whichever numbering (wings64-dc traps.md) */
                if ((pc[0] >> 24) >= 0xB2 && (pc[0] >> 24) <= 0xB4 && (pc[2] >> 24) >= 0xB2 && (pc[2] >> 24) <= 0xB4) {
                    R.half1 = pc[1];
                    R.half2 = pc[3];
                    pc += 4;
                }
                do_texrect(w0, w1);
                break;
            case F3D_G_FILLRECT: do_fillrect(w0, w1); break;
            case F3D_G_SETTIMG: f3d_setimg_decode(w0, w1, &R.timg_fmt, &R.timg_siz, &R.timg_w, &R.timg_addr); break;
            case F3D_G_SETTILE: {
                struct f3d_settile s;
                Tile* t;
                f3d_settile_decode(w0, w1, &s);
                t = &R.tile[s.tile & 7];
                t->fmt = s.fmt; t->siz = s.siz; t->line = s.line; t->tmem = s.tmem;
                t->cms = s.cms; t->cmt = s.cmt; t->masks = s.masks; t->maskt = s.maskt;
                t->shifts = s.shifts; t->shiftt = s.shiftt;
                break;
            }
            case F3D_G_SETTILESIZE: {
                uint8_t tile;
                uint16_t uls, ult, lrs, lrt;
                f3d_tilesize_decode(w0, w1, &tile, &uls, &ult, &lrs, &lrt);
                tile &= 7;
                R.tile[tile].uls = uls; R.tile[tile].ult = ult; R.tile[tile].lrs = lrs; R.tile[tile].lrt = lrt;
                break;
            }
            case F3D_G_LOADBLOCK: {
                uint8_t tile;
                uint16_t uls, ult, lrs, dxt;
                TmemLoad* ld;
                unsigned bpt;
                f3d_loadblock_decode(w0, w1, &tile, &uls, &ult, &lrs, &dxt);
                ld = &R.loads[(R.tile[tile & 7].tmem >> 3) & 63];
                bpt = R.timg_siz == 3 ? 4 : R.timg_siz == 2 ? 2 : 1;   /* LOADBLOCK in SETTIMG's size */
                ld->addr = R.timg_addr;
                ld->bytes = (lrs + 1u) * bpt;
                ld->dxt = dxt;
                ld->valid = 1;
                ld->tile = 0;
                if (R.timg_siz == 0) ld->bytes = (lrs + 2u) / 2;
                break;
            }
            case F3D_G_LOADTILE: {
                uint8_t tile;
                uint16_t uls, ult, lrs, lrt;
                TmemLoad* ld;
                unsigned x0, y0, x1, y1, bpp2;   /* bpp2: bytes per texel x 2 */
                f3d_loadtile_decode(w0, w1, &tile, &uls, &ult, &lrs, &lrt);
                ld = &R.loads[(R.tile[tile & 7].tmem >> 3) & 63];
                bpp2 = R.timg_siz == 3 ? 8 : R.timg_siz == 2 ? 4 : R.timg_siz == 1 ? 2 : 1;
                x0 = uls >> 2; y0 = ult >> 2; x1 = lrs >> 2; y1 = lrt >> 2;
                if (x1 < x0 || y1 < y0) break;
                ld->addr = R.timg_addr + (y0 * R.timg_w + x0) * bpp2 / 2;
                ld->row_bytes = (uint16_t)(((x1 - x0 + 1) * bpp2 + 1) / 2);
                ld->stride = (uint16_t)(R.timg_w * bpp2 / 2);
                ld->rows = (uint16_t)(y1 - y0 + 1);
                ld->bytes = ld->rows * R.tile[tile & 7].line * 8u;
                ld->dxt = 0;
                ld->tile = 1;
                ld->valid = 1;
                break;
            }
            case F3D_G_SETCOMBINE: R.cc0 = w0; R.cc1 = w1; break;
            case F3D_G_SETPRIMCOLOR:
                if (((uint32_t)R.prim[0] << 24 | (uint32_t)R.prim[1] << 16 | (uint32_t)R.prim[2] << 8 | R.prim[3]) != w1 ||
                    R.prim_lod_frac != (uint8_t)w0)
                    DIRTY();
                R.prim[0] = (uint8_t)(w1 >> 24); R.prim[1] = (uint8_t)(w1 >> 16);
                R.prim[2] = (uint8_t)(w1 >> 8); R.prim[3] = (uint8_t)w1;
                R.prim_lod_frac = (uint8_t)w0;
                break;
            case F3D_G_SETENVCOLOR:
                if (((uint32_t)R.env[0] << 24 | (uint32_t)R.env[1] << 16 | (uint32_t)R.env[2] << 8 | R.env[3]) != w1) DIRTY();
                R.env[0] = (uint8_t)(w1 >> 24); R.env[1] = (uint8_t)(w1 >> 16);
                R.env[2] = (uint8_t)(w1 >> 8); R.env[3] = (uint8_t)w1;
                break;
            case F3D_G_SETFOGCOLOR:
                R.fogc[0] = (uint8_t)(w1 >> 24); R.fogc[1] = (uint8_t)(w1 >> 16);
                R.fogc[2] = (uint8_t)(w1 >> 8); R.fogc[3] = (uint8_t)w1;
                break;
            case F3D_G_SETBLENDCOLOR:
                R.blend[0] = (uint8_t)(w1 >> 24); R.blend[1] = (uint8_t)(w1 >> 16);
                R.blend[2] = (uint8_t)(w1 >> 8); R.blend[3] = (uint8_t)w1;
                break;
            case F3D_G_SETFILLCOLOR: R.fill = w1; break;
            case F3D_G_SETCIMG: {
                uint32_t c = (uint32_t)(uintptr_t)ram(w1);
                if ((R.cimg == R.zimg) != (c == R.zimg)) DIRTY();
                if (R.cimg == R.zimg && R.cimg && c != R.zimg) s_shadow_rec = 0;   /* pass 1 ended */
                R.cimg = c;
                break;
            }
            case F3D_G_SETZIMG: R.zimg = (uint32_t)(uintptr_t)ram(w1); DIRTY(); break;
            case F3D_G_SETSCISSOR: {
                uint8_t mode;
                uint16_t ulx, uly, lrx, lry;
                f3d_scissor_decode(w0, w1, &mode, &ulx, &uly, &lrx, &lry);
                s_scissor[0] = ulx * 0.25f; s_scissor[1] = uly * 0.25f;
                s_scissor[2] = lrx * 0.25f; s_scissor[3] = lry * 0.25f;
                clip_rect_update();
                break;
            }
            case F3D_G_RDPLOADSYNC: case F3D_G_RDPPIPESYNC: case F3D_G_RDPTILESYNC: case F3D_G_RDPFULLSYNC:
            case F3D_G_SPNOOP: case F3D_G_NOOP: case F3D_G_SETPRIMDEPTH: case F3D_G_SETCONVERT: case F3D_G_SETKEYR:
            case F3D_G_SETKEYGB: case F3D_G_CULLDL:
                break;
            default:
                s_unknown[w0 >> 24]++;
                break;
        }
    }
}

/* ------------------------------------------------------------ frame */

static int s_init;
static uint32_t s_frames;
static uint64_t s_dl_us;

void f3d_run(const void* dl) {
    uint64_t t0 = xhw_time_us();
    int i;
    if (!s_init) {
        s_init = 1;
        R.vp_sx = 160.0f; R.vp_sy = 120.0f; R.vp_tx = 160.0f; R.vp_ty = 120.0f;
        for (i = 0; i < 16; i++) R.mv[0][i >> 2][i & 3] = R.proj[i >> 2][i & 3] = (i % 5) == 0 ? 1.0f : 0.0f;
    }
    xgfx_frame_begin();
    s_title = wx_game_state() == 0;
    map_setup();
    R.mv_top = 0;
    R.mvp_dirty = 1;
    s_zorder = 0;
    s_shadow_dl = 0;
    s_shadow_rec = s_shadow_pass2 = 0;
    s_scissor[0] = s_scissor[1] = 0.0f; s_scissor[2] = N64_W; s_scissor[3] = N64_H;
    clip_rect_update();
    s_frame_no++;
    s_trace = XGFX_TRACE_FRAME && s_frames == XGFX_TRACE_FRAME;
    DIRTY();
    run_dl((const uint32_t*)dl);
    flush();
    if (s_title && XGFX_TITLE_FIT) {   /* the black margin around the fitted title picture (drawn last) */
        int32_t m = (int32_t)(TITLE_MARGIN * s_ch / 480.0f), cx = (int32_t)s_cx, cw = (int32_t)s_cw, ch = (int32_t)s_ch;
        xgfx_clear(cx, 0, cx + cw, m, 0xFF000000u, 1, 0, 1.0f);
        xgfx_clear(cx, ch - m, cx + cw, ch, 0xFF000000u, 1, 0, 1.0f);
        xgfx_clear(cx, m, cx + m, ch - m, 0xFF000000u, 1, 0, 1.0f);
        xgfx_clear(cx + cw - m, m, cx + cw, ch - m, 0xFF000000u, 1, 0, 1.0f);
    }
    wx_frame_overlay();   /* the port's own text (FPS counter, settings panel; patches/021) */
    s_dl_us += xhw_time_us() - t0;
    xgfx_present();
    if (s_frames % 600 == 0)
        xhw_logf("[GFX] frame %u state %d: tris %u, rejected %u, culled %u, clipped %u | vtx loads %u, mtx %u",
                 s_frames, wx_game_state(), s_t_in, s_t_rej, s_t_cull, s_t_clip, s_vtx_loads, s_mtx_loads);
    s_t_in = s_t_rej = s_t_cull = s_t_clip = s_vtx_loads = s_mtx_loads = 0;
    if (++s_frames % 600 == 0) {
        xhw_logf("[GFX] frame %u: display lists %u us a frame (avg over 600)", s_frames, (unsigned)(s_dl_us / 600));
        s_dl_us = 0;
        for (i = 0; i < 256; i++)
            if (s_unknown[i]) {
                xhw_logf("[GFX] unhandled op %02X x%u", i, s_unknown[i]);
                s_unknown[i] = 0;
            }
    }
}
