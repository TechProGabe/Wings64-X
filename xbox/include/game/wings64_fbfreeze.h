/* wings64_fbfreeze.h - the renderer's frame-buffer persistence hooks as seen
 * from the decomp (patches/026, 028). Plain prototypes only: decomp units and
 * port units both include them. The renderer (xbox/src/gfx, M3-M4) decides
 * what they do; until then they are no-ops (xbox/src/game/ultra/hooks.c).
 *
 * The N64 frame buffer persists between frames and the game relies on it in
 * two places: the screen fade (replay_screen.c func_8032D51C copies the last
 * frame and then only draws a fade quad) and the photo album (snap.c
 * func_8033E3A8 draws each thumbnail once). A renderer that clears every
 * frame needs these hooks; one that keeps the previous frame in its colour
 * buffer may make them no-ops for good.
 *
 * Kill switches (default 1; 0 compiles the hook out):
 *   XBOX_CFLAGS=-DWINGS64_FB_FREEZE=0   fades draw over a cleared frame
 *   XBOX_CFLAGS=-DWINGS64_FB_PERSIST=0  the album relies on the colour buffer alone */
#ifndef WINGS64_FBFREEZE_H
#define WINGS64_FBFREEZE_H

#ifndef WINGS64_FB_FREEZE
#define WINGS64_FB_FREEZE 1
#endif
/* 1: keep the last finished frame as the background of the frames that
 * follow; 0: back to normal. Called on the game thread between gfx tasks. */
void wings64_fb_freeze(int on);

#ifndef WINGS64_FB_PERSIST
#define WINGS64_FB_PERSIST 1
#endif
/* 1: every frame starts from the previous finished frame instead of a clear
 * (the photo album); 0: back to normal. */
void wings64_fb_persist(int on);

#endif
