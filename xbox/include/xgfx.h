/* xgfx.h - the Fast3D interpreter (xbox/src/gfx, game triple) and the NV2A
 * back end (xbox/src/hw/nv2a*.c, nxdk triple) share this API (PLAN.md D4).
 *
 * The interpreter does the RSP's work on the CPU: display-list walk, vertex
 * transform, lighting, texgen, fog factor, clipping and culling, and the
 * N64 -> framebuffer mapping. What reaches the back end is screen-space
 * triangles and a draw state that still speaks the RDP's language where the
 * NV2A can do the work: the colour combiner words (compiled to register
 * combiners by nv2a_cc.c), primitive/environment colours, two texture units.
 * Blending, alpha test, depth and stencil are decided by the interpreter
 * (which knows the N64 render modes) and sent as plain choices.
 *
 * Both triples must lay these structs out identically: only 32-bit scalars,
 * floats and byte arrays, no bit-fields, no 64-bit members. */
#ifndef XGFX_H
#define XGFX_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One vertex as the GPU takes it (the back end's pass-through vertex
 * program copies these to its outputs). */
typedef struct {
    float x, y;        /* framebuffer pixels */
    float z;           /* depth in depth-buffer units, 0..XgfxOutput.zmax (near..far) */
    float w;           /* clip-space w (> 0) for perspective-correct interpolation; 1 for 2D */
    uint8_t rgba[4];   /* shade: r g b a */
    float s0, t0;      /* texture unit 0, normalised to the texture's (power-of-two) size */
    float s1, t1;      /* texture unit 1 */
    float fog;         /* 0..1: how much of the fog colour (used when XgfxState.fog) */
} XgfxVtx;

enum { XGFX_WRAP_REPEAT = 0, XGFX_WRAP_MIRROR = 1, XGFX_WRAP_CLAMP = 2 };
enum { XGFX_BLEND_OFF = 0, XGFX_BLEND_ALPHA = 1 /* src*a + dst*(1-a) */, XGFX_BLEND_ADD = 2 /* src*a + dst */ };
enum {
    XGFX_STENCIL_OFF = 0,
    XGFX_STENCIL_MARK = 1,   /* shadow volume pass 1: where the draw passes the depth test, stencil = 1; no colour */
    XGFX_STENCIL_TEST = 2,   /* shadow volume pass 2: draw where stencil == 0; every fragment sets stencil back to 0 */
};
enum { XGFX_DEPTH_OFF = 0, XGFX_DEPTH_LEQUAL = 1, XGFX_DEPTH_LESS = 2, XGFX_DEPTH_ALWAYS = 3 };

typedef struct {
    /* colour combiner: the G_SETCOMBINE words as the display list holds
     * them (cc0 = w0 & 0x00FFFFFF, cc1 = w1); two_cycle 0: cycle 0's half
     * only. The interpreter rewrites them for FILL (PRIM) and COPY (TEXEL0)
     * rectangles. */
    uint32_t cc0, cc1;
    uint32_t two_cycle;
    uint8_t prim[4], env[4];     /* rgba */
    uint8_t prim_lod_frac;       /* PRIM_LOD_FRAC, 0..255 */
    uint8_t lod_frac;            /* LOD_FRACTION: 0 until mip levels are drawn (XGFX_MIPS) */
    uint8_t k5, pad0;            /* K5 (YUV convert; unused by PW64) */
    /* textures: back-end handles (xgfx_tex_create), 0 none */
    uint32_t tex[2];
    uint32_t wrap_s[2], wrap_t[2];   /* XGFX_WRAP_* */
    uint32_t filter[2];              /* 0 point, 1 bilinear */
    /* pixel */
    uint32_t blend;              /* XGFX_BLEND_* */
    uint32_t alpha_ref;          /* 0: no alpha test; else pass alpha >= ref (1..255) */
    uint32_t depth_func;         /* XGFX_DEPTH_* */
    uint32_t depth_write;
    uint32_t depth_bias;         /* decal: pulled toward the eye by this many depth steps (polygon offset units) */
    uint32_t color_write;        /* 0: depth/stencil only */
    uint32_t stencil;            /* XGFX_STENCIL_* */
    uint32_t fog;                /* 1: blend toward fog_color by the vertices' fog amount */
    uint8_t fog_color[4];        /* rgb (a unused) */
    int32_t scissor[4];          /* framebuffer pixels: x0 y0 x1 y1 (exclusive) */
} XgfxState;

typedef struct {
    int32_t fb_w, fb_h;          /* framebuffer */
    int32_t bpp;                 /* 32, or 16 at 720p/1080i */
    float zmax;                  /* depth-buffer units at the far plane (2^24 - 1 for Z24S8) */
    int32_t xscale;              /* framebuffer pixels per screen pixel across: 2 with 2x supersampling, else 1 */
} XgfxOutput;

/* ---- back end (hw) ---- */
int xgfx_init(void);                       /* pbkit up in the mode xhw_video_boot chose */
void xgfx_output(XgfxOutput* out);
void xgfx_frame_begin(void);               /* idempotent per frame */
/* n triangles (3n vertices, any winding: culling is the interpreter's). The
 * vertices are copied; the state is diffed against what the GPU has. */
void xgfx_draw(const XgfxState* st, const XgfxVtx* v, uint32_t ntris);
/* Clears a framebuffer rect (pixels, exclusive end) to argb and/or the depth
 * (0..1 of zmax), stencil to 0 with depth. */
void xgfx_clear(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t argb, int color, int depth, float z01);
/* Ends the frame and queues the flip (vblank-synchronised by pbkit). */
void xgfx_present(void);
void xgfx_set_fps_overlay(int on);
/* The Settings panel's texture filter: 0 N64 (the game's per-texture
 * choice), 1 smooth (bilinear through a 3x3 Gaussian), 2 sharp (point).
 * At once. */
void xgfx_set_tex_filter(int mode);
/* Anti-aliasing by supersampling: 1 off, 2 (2x wide), 4 (2x2). Before
 * xgfx_init; only at 640x480 32-bit (else off). */
void xgfx_set_aa(int samples);

/* Textures: w and h powers of two (1..1024), rows of A8R8G8B8 texels
 * (0xAARRGGBB in a native uint32). Returns 0 when the pool is full (after
 * waiting for the GPU to release textures already destroyed). */
uint32_t xgfx_tex_create(uint32_t w, uint32_t h, const uint32_t* argb);
void xgfx_tex_destroy(uint32_t tex);       /* deferred until the GPU is done with it */
uint32_t xgfx_tex_pool_free_kb(void);

/* The finished picture read back (photo album, persistent frame): the
 * framebuffer rect (pixels) into argb (w x h, A8R8G8B8). Waits for the GPU. */
void xgfx_read_rect(int32_t x0, int32_t y0, int32_t w, int32_t h, uint32_t* argb);

/* The picture presented last as a texture (the frozen frame under the game's
 * screen fades, patches/026): the picture fills u 0..u_max, v 0..v_max.
 * 0 when there is none or the pool is full. Waits for the GPU. */
uint32_t xgfx_capture_last(float* u_max, float* v_max);
/* The same on the GPU, no CPU read (the CPU capture cost ~244 ms a fade on
 * the console): a linear texture viewing the picture presented last (texel
 * coordinates: w x h), the hold surface as the render target (1) or the
 * screen again (0; 0 returned when there is no hold surface), and a linear
 * texture of the hold surface. A linear texture's memory is the surface's:
 * xgfx_tex_destroy only drops the handle. */
uint32_t xgfx_tex_linear_last(float* w, float* h);
int xgfx_target_hold(int on);
uint32_t xgfx_tex_linear_hold(float* w, float* h);

void xgfx_debug_dump(void);   /* debug: the back buffer now, as [FBDUMP] */

/* Combiner coverage: modes the compiler had to approximate since boot. */
uint32_t xgfx_cc_approximated(void);

#ifdef __cplusplus
}
#endif
#endif
