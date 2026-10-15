/* xgfx_selftest.c - the NV2A back end on its own (-DXGFX_SELFTEST=1).
 *
 * Replaces the game with a fixed scene drawn through xgfx.h exactly as the
 * interpreter would send it: a shaded triangle (SHADE), a textured quad
 * (TEXEL0 * SHADE, a checkerboard), a fogged triangle, an alpha-blended
 * quad, a depth-tested pair, a stencil shadow-volume pair and a texrect-like
 * copy quad. In xemu "[FBDUMP]" shots of frames 30 and 90 show it (autopad
 * SHOT lines also work); it then runs until a button returns to the
 * dashboard. A test of the back end's state, combiner and vertex paths, not
 * of the interpreter. */
#include <string.h>

#include "xgfx.h"
#include "xhw.h"
#include "xhw_internal.h"

#if defined(XGFX_SELFTEST) && XGFX_SELFTEST

/* G_SETCOMBINE words, one cycle (cycle 1 = cycle 0): (A - B) * C + D */
static void cc(XgfxState* s, int a, int b, int c, int d, int aa, int ab, int ac, int ad) {
    s->cc0 = (uint32_t)a << 20 | (uint32_t)c << 15 | (uint32_t)aa << 12 | (uint32_t)ac << 9 | (uint32_t)a << 5 | (uint32_t)c;
    s->cc1 = (uint32_t)b << 28 | (uint32_t)d << 15 | (uint32_t)ab << 12 | (uint32_t)ad << 9 | (uint32_t)b << 24 |
             (uint32_t)aa << 21 | (uint32_t)ac << 18 | (uint32_t)d << 6 | (uint32_t)ab << 3 | (uint32_t)ad;
    s->two_cycle = 0;
}

static XgfxVtx vtx(float x, float y, float z, uint32_t rgba, float s, float t, float fog) {
    XgfxVtx v;
    memset(&v, 0, sizeof v);
    v.x = x;
    v.y = y;
    v.z = z;
    v.w = 1.0f;
    v.rgba[0] = (uint8_t)(rgba >> 24);
    v.rgba[1] = (uint8_t)(rgba >> 16);
    v.rgba[2] = (uint8_t)(rgba >> 8);
    v.rgba[3] = (uint8_t)rgba;
    v.s0 = v.s1 = s;
    v.t0 = v.t1 = t;
    v.fog = fog;
    return v;
}

static void base(XgfxState* s, const XgfxOutput* o) {
    memset(s, 0, sizeof *s);
    s->color_write = 1;
    s->scissor[2] = o->fb_w;
    s->scissor[3] = o->fb_h;
}

void wx_boot(const uint8_t* rom, uint32_t size) {
    static uint32_t tex[64 * 64];
    XgfxOutput o;
    XgfxState s;
    XgfxVtx v[12];
    uint32_t t, frame;
    float W, H, zmax;
    int x, y, armed = 0;
    (void)rom;
    (void)size;
    xhw_video_boot();
    xgfx_init();
    xgfx_set_fps_overlay(1);
    xgfx_output(&o);
    W = (float)o.fb_w / 640.0f;
    H = (float)o.fb_h / 480.0f;
    zmax = o.zmax;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) tex[y * 64 + x] = ((x ^ y) & 8) ? 0xFFFFFFFFu : 0xFF2050C0u;
    t = xgfx_tex_create(64, 64, tex);
    xhw_logf("[SELFTEST] texture handle %u, output %dx%d %d-bit zmax %u", (unsigned)t, o.fb_w, o.fb_h, o.bpp,
             (unsigned)zmax);
    for (frame = 0;; frame++) {
        float k = (float)(frame % 120) / 120.0f;
        xgfx_frame_begin();
        /* 1. shaded triangle: (0 - 0) * 0 + SHADE */
        base(&s, &o);
        cc(&s, 15, 15, 31, 4, 7, 7, 7, 4);
        v[0] = vtx(40 * W, 400 * H, 0, 0xFF0000FFu, 0, 0, 0);
        v[1] = vtx(200 * W, 400 * H, 0, 0x00FF00FFu, 0, 0, 0);
        v[2] = vtx(120 * W, (260 - 40 * k) * H, 0, 0x0000FFFFu, 0, 0, 0);
        xgfx_draw(&s, v, 1);
        /* 2. textured quad: TEXEL0 * SHADE, wrap, bilinear */
        base(&s, &o);
        cc(&s, 1, 15, 4, 7, 1, 7, 4, 7);
        s.tex[0] = t;
        s.filter[0] = 1;
        v[0] = vtx(240 * W, 260 * H, 0, 0xFFFFFFFFu, 0, 0, 0);
        v[1] = vtx(400 * W, 260 * H, 0, 0xFFFF80FFu, 2, 0, 0);
        v[2] = vtx(240 * W, 400 * H, 0, 0xFFFFFFFFu, 0, 2, 0);
        v[3] = v[1];
        v[4] = vtx(400 * W, 400 * H, 0, 0x80FFFFFFu, 2, 2, 0);
        v[5] = v[2];
        xgfx_draw(&s, v, 2);
        /* 3. fogged triangle: SHADE, fog toward orange, more at the bottom */
        base(&s, &o);
        cc(&s, 15, 15, 31, 4, 7, 7, 7, 4);
        s.fog = 1;
        s.fog_color[0] = 255;
        s.fog_color[1] = 128;
        s.fog_color[2] = 0;
        v[0] = vtx(440 * W, 260 * H, 0, 0x2040FFFFu, 0, 0, 0.0f);
        v[1] = vtx(600 * W, 260 * H, 0, 0x2040FFFFu, 0, 0, 0.0f);
        v[2] = vtx(520 * W, 400 * H, 0, 0x2040FFFFu, 0, 0, 1.0f);
        xgfx_draw(&s, v, 1);
        /* 4. depth: a far red quad then a near green one, half overlapping, LEQUAL */
        base(&s, &o);
        cc(&s, 15, 15, 31, 3, 7, 7, 7, 3);   /* PRIM */
        s.depth_func = XGFX_DEPTH_LEQUAL;
        s.depth_write = 1;
        s.prim[0] = 255;
        s.prim[3] = 255;
        v[0] = vtx(40 * W, 40 * H, 0.8f * zmax, 0, 0, 0, 0);
        v[1] = vtx(200 * W, 40 * H, 0.8f * zmax, 0, 0, 0, 0);
        v[2] = vtx(40 * W, 200 * H, 0.8f * zmax, 0, 0, 0, 0);
        v[3] = v[1];
        v[4] = vtx(200 * W, 200 * H, 0.8f * zmax, 0, 0, 0, 0);
        v[5] = v[2];
        xgfx_draw(&s, v, 2);
        s.prim[0] = 0;
        s.prim[1] = 255;
        for (x = 0; x < 6; x++) {
            v[x].x += 60 * W;
            v[x].y += 60 * H;
            v[x].z = 0.2f * zmax;
        }
        xgfx_draw(&s, v, 2);
        /* ...and a blue one behind both: hidden where they are */
        s.prim[1] = 0;
        s.prim[2] = 255;
        for (x = 0; x < 6; x++) {
            v[x].x -= 30 * W;
            v[x].y -= 30 * H;
            v[x].z = 0.9f * zmax;
        }
        xgfx_draw(&s, v, 2);
        /* 5. alpha blend: a half-transparent white quad over everything above */
        base(&s, &o);
        cc(&s, 15, 15, 31, 3, 7, 7, 7, 3);
        s.blend = XGFX_BLEND_ALPHA;
        s.prim[0] = s.prim[1] = s.prim[2] = 255;
        s.prim[3] = 96;
        v[0] = vtx(300 * W, 60 * H, 0, 0, 0, 0, 0);
        v[1] = vtx(620 * W, 60 * H, 0, 0, 0, 0, 0);
        v[2] = vtx(300 * W, 220 * H, 0, 0, 0, 0, 0);
        v[3] = v[1];
        v[4] = vtx(620 * W, 220 * H, 0, 0, 0, 0, 0);
        v[5] = v[2];
        xgfx_draw(&s, v, 2);
        /* 6. stencil: mark a square, then a dark quad drawn only outside it */
        base(&s, &o);
        cc(&s, 15, 15, 31, 3, 7, 7, 7, 3);
        s.stencil = XGFX_STENCIL_MARK;
        s.color_write = 0;
        s.depth_func = XGFX_DEPTH_LEQUAL;
        v[0] = vtx(360 * W, 100 * H, 0, 0, 0, 0, 0);
        v[1] = vtx(420 * W, 100 * H, 0, 0, 0, 0, 0);
        v[2] = vtx(360 * W, 160 * H, 0, 0, 0, 0, 0);
        v[3] = v[1];
        v[4] = vtx(420 * W, 160 * H, 0, 0, 0, 0, 0);
        v[5] = v[2];
        xgfx_draw(&s, v, 2);
        base(&s, &o);
        cc(&s, 15, 15, 31, 3, 7, 7, 7, 3);
        s.stencil = XGFX_STENCIL_TEST;
        s.prim[3] = 255;   /* black */
        v[0] = vtx(330 * W, 90 * H, 0, 0, 0, 0, 0);
        v[1] = vtx(450 * W, 90 * H, 0, 0, 0, 0, 0);
        v[2] = vtx(330 * W, 130 * H, 0, 0, 0, 0, 0);
        v[3] = v[1];
        v[4] = vtx(450 * W, 130 * H, 0, 0, 0, 0, 0);
        v[5] = v[2];
        xgfx_draw(&s, v, 2);
        xgfx_present();
        if (frame == 30 || frame == 90) xhw_shot_request(xhw_running_in_xemu());
        {
            xhw_pad p;
            int any = 0, i;
            xhw_pad_poll();
            for (i = 0; i < 4; i++)
                if (xhw_pad_get(i, &p) && p.buttons) any = 1;
            if (!any) armed = 1;
            else if (armed && frame > 60) xhw_quit_to_dashboard();
        }
        xhw_wait_vblank();
    }
}
#endif
