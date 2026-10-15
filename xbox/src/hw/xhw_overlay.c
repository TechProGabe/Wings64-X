/* xhw_overlay.c - lines of text over a finished frame: the video-mode
 * prompt (PLAN.md 3.3) and the notices (xhw_notice). From Melee-X's
 * xhw_overlay.c (its "lines" kind), the box kind dropped.
 *
 * nv2a.c's present calls this once the GPU is idle and before the flip and
 * the screenshot dumps, so it is plain CPU writes into the back buffer, as
 * the boot card draws (xhw_splash.c, same font): no GPU state to set up or
 * undo, the same in every video mode. The framebuffer is write-combined, so
 * nothing here reads it back (an opaque panel, not a darkened one), and an
 * sfence puts the writes out before the flip is queued. 8x16 glyphs at 480
 * lines, x2 at 720p, x3 at 1080i. */
#include <stdint.h>
#include <string.h>

#include "xhw.h"
#include "xhw_internal.h"

typedef struct {
    uint8_t* fb;
    int w, h, bpp, pitch;
} Surf;

static uint32_t to_px(const Surf* s, uint32_t rgb) {
    if (s->bpp == 16) return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
    return 0xFF000000u | rgb;
}

static void fill(const Surf* s, int x, int y, int w, int h, uint32_t rgb) {
    uint32_t px = to_px(s, rgb);
    int x1 = x + w, y1 = y + h, i, j;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    for (j = y; j < y1; j++) {
        uint8_t* row = s->fb + (size_t)j * (size_t)s->pitch;
        if (s->bpp == 16)
            for (i = x; i < x1; i++) ((uint16_t*)row)[i] = (uint16_t)px;
        else
            for (i = x; i < x1; i++) ((uint32_t*)row)[i] = px;
    }
}

static void text(const Surf* s, const char* t, int x, int y, int z, uint32_t rgb) {
    for (; *t; t++, x += 8 * z) {
        const unsigned char* g = xhw_font16 + (unsigned char)*t * 16;
        int gy, gx;
        for (gy = 0; gy < 16; gy++)
            for (gx = 0; gx < 8; gx++)
                if (g[gy] & (0x80 >> gx)) fill(s, x + gx * z, y + gy * z, z, z, rgb);
    }
}

void xhw_overlay_lines(void* fb, int w, int h, int bpp, int pitch, char lines[2][96], int n, uint32_t rgb, int top) {
    Surf s;
    int z = h >= 1080 ? 3 : h >= 720 ? 2 : 1, lh = 22 * z, r, y0;
    if (!fb || n <= 0) return;
    s.fb = (uint8_t*)fb;
    s.w = w;
    s.h = h;
    s.bpp = bpp;
    s.pitch = pitch;
    /* near the top (inside the TV-safe area), or centred on the screen */
    y0 = top ? h / 12 : (h - n * lh) / 2;
    for (r = 0; r < n && r < 2; r++) {
        int len = (int)strlen(lines[r]), tw = len * 8 * z, x = (w - tw) / 2, y = y0 + r * lh;
        if (len) {
            fill(&s, x - 6 * z, y - 3 * z, tw + 12 * z, lh, 0x000000);
            text(&s, lines[r], x, y, z, rgb);
        }
    }
    __asm__ volatile("sfence" ::: "memory");
}
