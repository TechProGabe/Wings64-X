/* xhw_internal.h - shared between the xbox/src/hw files (nxdk triple). */
#ifndef XHW_INTERNAL_H
#define XHW_INTERNAL_H
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

/* Saves, settings and logs. Always on the HDD, so a burned-disc boot can
 * still save. "WX" 0001 (tools/xbox/xbe_title_image.py writes the same ID
 * into the certificate; cxbe has no title-ID flag). */
#define XHW_TITLE "Wings64-X"
#define XHW_UDATA_ROOT "E:\\UDATA\\57580001"
#define XHW_UDATA_DIR XHW_UDATA_ROOT "\\"

void xhw_log_open_file(void);
void xhw_vlog_raw(const char* fmt, va_list ap);
size_t xhw_log_tail(char* out, size_t cap);
void xhw_com1_raw(const char* s, size_t n);
void xhw_log_com1_line(const char* line);
void xhw_mem_log(const char* where);
void xhw_flush_handle(HANDLE h);
void xhw_flush_volume(char drive);
void xhw_log_sync(void);               /* flush pending boot.log lines if the lock is free */
void xhw_log_booted(void);             /* the first frame is presented: boot.log stops flushing every line */
int xhw_log_try(const char* line);     /* log + flush without blocking on the lock for long */
int xhw_log_try_file(const char* text);   /* the same, boot.log and the tail only (not COM1) */
void xhw_log_keep(const char* name);   /* boot.log closed and kept as name */

/* xhw_main.c: how this boot ends, in lastexit.txt for the next boot's
 * "[BOOT] previous exit" line; NULL deletes it */
void xhw_exit_write(const char* why);

/* xhw_crash.c: run fn under the CPU exception reporter (crash.log + screen). */
void xhw_crash_guard(void (*fn)(void*), void* arg);
extern unsigned int xhw_image_base, xhw_image_end;

/* xhw_fbdump.c: a framebuffer as [FBDUMP] log lines (tools/xbox/fbdump_to_png.py) */
void xhw_fbdump(const void* fb, int w, int h, int bpp, int pitch);
/* the same frame as E:\UDATA\57580001\shotNN.bmp (BACK on the console) */
void xhw_fbdump_file(const void* fb, int w, int h, int bpp, int pitch);

/* xhw_autopad.c: scripted input from D:\autopad.txt (-DXHW_AUTOPAD=1 only) */
struct xhw_pad;
void xhw_autopad_load(void);
void xhw_autopad_apply(int port, struct xhw_pad* out);
void xhw_autopad_after_hang(void);
void xhw_reboot_cold(void) __attribute__((noreturn));
int xhw_image_folder(char* out, int size);
void xhw_launch_xbe(const char* dos_path) __attribute__((noreturn));
void xhw_error_screen(const char* title, const char* const* lines);

/* xhw_splash.c: "TechProGabe Presents..." title card */
void xhw_splash_show(void);
void xhw_splash_release(void);   /* the mode is about to change */
extern const unsigned char xhw_font16[256 * 16];   /* unscii-16: 8x16, one byte a row, MSB left */

/* xhw_watchdog.c: hang dumper (hang.log + screen) */
void xhw_watchdog_start(void);
void xhw_set_game_thread(void);   /* call on the game's main thread */
void* xhw_game_thread(void);
unsigned long xhw_thread_eip(void* kthread, unsigned long* esp_out);
void xhw_watchdog_disable(void);
void xhw_watchdog_busy(int on);   /* a long, deliberate stall (screenshot) */
void xhw_code_repair(void);       /* xhw_code_repair.c: .text put back as default.xbe has it */
void xhw_video_pb_up(void);        /* nv2a.c: pbkit is up, the retrace can follow its vblank */
/* xhw_video.c: the lines to draw over this frame (the video-mode prompt,
 * a notice), 0 when none; rgb is the text colour */
int xhw_video_overlay(char lines[2][96], uint32_t* rgb, int* top);
/* xhw_overlay.c: centred lines of text on a dark box over a finished frame,
 * CPU writes (the GPU must be idle); `top`: near the top, else mid-screen */
void xhw_overlay_lines(void* fb, int w, int h, int bpp, int pitch, char lines[2][96], int n, uint32_t rgb, int top);
void xhw_clock_recheck(void);      /* xhw_clock.c: retraces against ACPI time (the watchdog, 30 s) */

/* xhw_pad.c: stop USB before leaving the XBE (a quick reboot into the next
 * XBE doesn't reset it; OpenCrossing traps.md). xhw_audio_shutdown: the
 * same for the AC97 (weak until the audio driver exists). */
void xhw_pad_shutdown(void);
uint32_t xhw_pad_events_queued(void);
void xhw_audio_shutdown(void);

/* xhw_prof.c (-DXHW_PROF=1): the sampling profiler over the game's threads */
void xhw_prof_register(void* kthread, int level);
void xhw_prof_start(void);

#endif
