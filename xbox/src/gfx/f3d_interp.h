/* f3d_interp.h - the Fast3D interpreter (f3d_interp.c) and the hooks it
 * takes from the rest of the game side. */
#ifndef F3D_INTERP_H
#define F3D_INTERP_H
#include <stdint.h>

/* One graphics task: interpret the display list and present the frame. */
void f3d_run(const void* dl);
/* Level load (the game reuses its heap): drop every cached texture. */
void f3d_tex_reset(void);
/* Screen fades (patches/026): 1 keeps the picture presented last under every
 * following frame, 0 ends it. Taken at the next frame (any thread may call). */
void f3d_hold_last(int on);
/* The photo album (patches/028): 1 keeps the picture presented last under
 * every following frame, 0 ends it. */
void f3d_fb_persist(int on);

/* ---- hooks (xbox/src/game) ---- */
/* An address as a display list holds it -> a host pointer (rdram.c: the
 * KSEG0 arena for 0x80000000..0x803FFFFF, host pointers as they are). */
void* wx_addr(uint32_t a);
/* The game's top-level state (0 = title screen), for the title fit. */
int wx_game_state(void);
/* The port's own drawing at the end of the frame (FPS counter, settings
 * panel); runs inside the frame, after the game's display list. */
void wx_frame_overlay(void);

#endif
