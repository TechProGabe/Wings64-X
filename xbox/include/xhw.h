/* xhw.h - the Xbox hardware layer (xbox/src/hw, nxdk's i386-pc-win32 triple)
 * as seen by the game-side port code (xbox/src/game, xbox/src/gfx: the game
 * triple). From Melee-X's xhw.h, cut to what Wings64-X uses.
 *
 * Only scalars, pointers and structs without bit-fields or 64-bit members
 * cross this boundary, so both triples agree on every layout here. */
#ifndef XHW_H
#define XHW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Test builds (autopad's, or -DXHW_TEST_BUILD=1) keep the tools for console
 * rounds: BACK takes a screenshot and the frame-rate counter is on by
 * default. A plain build is a release. */
#ifndef XHW_TEST_BUILD
#if (defined(XHW_AUTOPAD) && XHW_AUTOPAD) || (defined(XHW_PROF) && XHW_PROF)
#define XHW_TEST_BUILD 1
#else
#define XHW_TEST_BUILD 0
#endif
#endif

/* ---- logging (COM1 when present, E:\UDATA\57580001\boot.log) ---- */
void xhw_log(const char* line);
void xhw_logf(const char* fmt, ...) __attribute__((format(__printf__, 1, 2)));
/* Unrecoverable: draw the message on screen, log it, wait, go to the dashboard. */
void xhw_fatal(const char* title, const char* msg) __attribute__((noreturn));

/* ---- time (xhw_clock.c) ----
 * Every clock here is the ACPI power-management timer (3.375 MHz from its
 * own crystal) through KeQueryPerformanceCounter: never the CPU's TSC, whose
 * rate nxdk guesses from the multiplier and FSB (wrong on an upgraded or
 * overclocked CPU). The TSC is only measured against it at boot for the
 * [CLOCK] line. */
uint64_t xhw_ticks(void);            /* ACPI timer ticks */
uint64_t xhw_ticks_per_sec(void);    /* 3375000 */
uint64_t xhw_time_ns(void);          /* monotonic */
uint64_t xhw_time_us(void);
void xhw_clock_calibrate(void);      /* boot: [CLOCK] cpu N MHz */
uint32_t xhw_cpu_mhz(void);          /* measured at boot (log and diagnostics only) */
void xhw_sleep_ms(uint32_t ms);
void xhw_yield(void);

/* ---- threads and locks ---- */
typedef struct xhw_mutex xhw_mutex;
xhw_mutex* xhw_mutex_create(void);   /* recursive */
void xhw_mutex_lock(xhw_mutex* m);
void xhw_mutex_unlock(xhw_mutex* m);
typedef struct xhw_event xhw_event;
xhw_event* xhw_event_create(void);   /* auto-reset */
void xhw_event_signal(xhw_event* e);
int xhw_event_wait(xhw_event* e, uint32_t timeout_ms);   /* 1 signalled, 0 timeout */
#define XHW_INFINITE 0xFFFFFFFFu
uint32_t xhw_tls_alloc(void);
void* xhw_tls_get(uint32_t slot);
void xhw_tls_set(uint32_t slot, void* value);
/* priority: -2 (lowest) .. +2 (highest), +15 time critical; relative to normal */
int xhw_thread_start(void (*fn)(void*), void* arg, int priority, uint32_t stack_bytes);
void xhw_thread_set_priority(int priority);   /* the calling thread */
/* Threads at absolute kernel priority levels (base priority, 1..14; normal
 * is 8, the watchdog runs at 15): the libultra shim maps N64 priorities
 * onto these so the kernel's strict priority preemption orders them as the
 * N64 did. The token stays valid for the program's life. */
typedef struct xhw_thread xhw_thread;
xhw_thread* xhw_thread_spawn(void (*fn)(void*), void* arg, int level, uint32_t stack_bytes);
void xhw_thread_set_level(xhw_thread* t, int level);
xhw_thread* xhw_thread_self(void);    /* NULL for a thread not made by xhw_thread_spawn */
void xhw_thread_adopt_self(int level);   /* the calling thread becomes one (the boot thread) */

/* ---- memory ---- */
uint32_t xhw_mem_free_kb(void);
/* The game's RDRAM arena: zeroed contiguous memory, cached, page-aligned, at
 * 0x80000000 | physical (bit 31 set: the game's RAM-not-ROM test). NULL if
 * there is not that much contiguous memory. Never freed. */
void* xhw_rdram_alloc(uint32_t bytes);
/* 128 MB consoles run as 64 MB ones (xhw_ramlock.c): call first in main_body.
 * _log writes the [MEM] line once the log is open. */
void xhw_ram_lock64(void);
void xhw_ram_lock64_log(void);
/* Memory the kernel writes (file reads) must be committed: nxdk's malloc is
 * committed already, so this is a no-op kept for the Melee-X call sites. */
void xhw_commit(const void* p, uint32_t bytes);
uint32_t xhw_lazy_committed_kb(void);

/* ---- files and paths ---- */
const char* xhw_game_dir(void);      /* "D:\\" */
const char* xhw_save_dir(void);      /* "E:\\UDATA\\57580001\\" */
int xhw_mkdir(const char* path);
int xhw_copy_file(const char* from, const char* to);
enum { XHW_REPLACE_FAILED = 0, XHW_REPLACE_OK = 1, XHW_REPLACE_LOST_TO = 2 };
int xhw_replace_file(const char* from, const char* to);
typedef struct xhw_dir_entry { char name[64]; int is_dir; uint32_t size; } xhw_dir_entry;
void* xhw_dir_first(const char* pattern, xhw_dir_entry* out);
int xhw_dir_next(void* handle, xhw_dir_entry* out);
void xhw_flush(void* stdio_file);
unsigned xhw_log_fallback(void);
/* Two lines at the top of the picture for 10 s, any thread (logged too). */
void xhw_notice(const char* line1, const char* line2);
void* xhw_file_open(const char* path);
int xhw_file_read(void* file, uint32_t off, void* dst, uint32_t len);
uint32_t xhw_file_size(void* file);
void xhw_file_close(void* file);
/* Whole-file write through a temp file, read back and renamed over `path`
 * (power-cut safe). 1 on success. */
int xhw_write_file_atomic(const char* path, const void* data, uint32_t len);

/* ---- controllers ---- */
typedef struct xhw_pad {
    int connected;
    uint32_t buttons;            /* XHW_BTN_* */
    int16_t lx, ly, rx, ry;      /* -32768..32767, y up */
    uint8_t lt, rt;              /* 0..255 */
} xhw_pad;
enum {
    XHW_BTN_A = 1u << 0, XHW_BTN_B = 1u << 1, XHW_BTN_X = 1u << 2, XHW_BTN_Y = 1u << 3,
    XHW_BTN_BLACK = 1u << 4, XHW_BTN_WHITE = 1u << 5, XHW_BTN_START = 1u << 6,
    XHW_BTN_BACK = 1u << 7, XHW_BTN_LSTICK = 1u << 8, XHW_BTN_RSTICK = 1u << 9,
    XHW_BTN_UP = 1u << 10, XHW_BTN_DOWN = 1u << 11, XHW_BTN_LEFT = 1u << 12,
    XHW_BTN_RIGHT = 1u << 13,
};
void xhw_pad_poll(void);
void xhw_pad_set_shots(int on);                /* BACK screenshots (always on in test builds) */
int xhw_pad_get(int port, xhw_pad* out);       /* port 0..3; returns connected */
void xhw_pad_rumble(int port, uint16_t low, uint16_t high);

/* ---- video ---- */
typedef struct xhw_video_mode {
    int width, height;           /* framebuffer */
    int bpp;                     /* 32, or 16 at 720p/1080i */
    int widescreen;              /* 1: the dashboard's 16:9 flag (or 720p/1080i) */
    int progressive;
} xhw_video_mode;
const xhw_video_mode* xhw_video(void);
int xhw_video_480p_allowed(void);
int xhw_video_720p_allowed(void);
int xhw_video_1080i_allowed(void);
int xhw_video_widescreen_set(void);
/* Before xhw_video_boot (settings.ini): XHW_OUT_* and 0 to force 480i where
 * the dashboard allows 480p. */
enum { XHW_OUT_AUTO = 0, XHW_OUT_480I, XHW_OUT_480P, XHW_OUT_720P, XHW_OUT_1080I };
void xhw_video_set_pref(int output, int progressive);
/* Sets the mode once, before pbkit starts. Ends the splash. */
void xhw_video_boot(void);
void xhw_video_fallback_480(void);
/* Retrace: waits for the next vblank and counts it (xhw_frame_count). */
void xhw_wait_vblank(void);
unsigned xhw_frame_count(void);
void xhw_note_present(void);         /* the renderer: once per presented frame */
unsigned xhw_present_count(void);
/* Boot title card (xhw_splash.c): load bar, 0..1; nothing once the mode is set. */
void xhw_splash_progress(float f);
/* Screenshot requests (BACK, autopad SHOT); the renderer takes them at present. */
void xhw_shot_request(int to_serial);
int xhw_shot_take(void);             /* 0 none, 1 BMP to E:, 2 [FBDUMP] to COM1 */
int xhw_running_in_xemu(void);

/* ---- system ---- */
void xhw_quit_to_dashboard(void) __attribute__((noreturn));
void xhw_reboot_self(void) __attribute__((noreturn));
void xhw_exit_reason(const char* why);
/* A full-screen message card (pbkit's debug screen): title + lines (NULL
 * ends), "Press any button or wait to return to the dashboard", then the
 * dashboard after `secs`. */
void xhw_error_card(const char* title, const char* const* lines, int secs) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
#endif
