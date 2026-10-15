/* xhw_perf.c - where a frame's time goes: a [PERF] line every 5 s (PLAN.md
 * M4: interpreter, pushbuffer, present, audio per frame).
 *
 * Sections are timed with the ACPI clock (xhw_time_us), never the TSC, and
 * only at a few coarse points a frame: the display-list run (f3d_run, with
 * its texture conversions and uploads broken out), the present (with the
 * waits for the GPU and for the flip broken out) and the audio tasks (the
 * audio thread). Threads add from their own context, so the sums are
 * 32-bit atomics. On the single-core console the buckets add up: what is
 * left of a frame is the game's own logic and idle time.
 *
 * Kill switch: -DXHW_PERF=0 (no line, the adds still cost a few atomics). */
#include <stdint.h>

#include "xhw.h"

#ifndef XHW_PERF
#define XHW_PERF 1
#endif
#ifndef XHW_PERF_SECS
#define XHW_PERF_SECS 5
#endif

static volatile uint32_t s_us[XHW_PERF_N];
static volatile uint32_t s_count[XHW_PERF_N];
static uint32_t s_frames;
static uint64_t s_t0;

void xhw_perf_add(int bucket, uint64_t us) {
    if (bucket < 0 || bucket >= XHW_PERF_N) return;
    __atomic_fetch_add(&s_us[bucket], (uint32_t)(us > 0xFFFFFFFFull ? 0xFFFFFFFFu : us), __ATOMIC_RELAXED);
    __atomic_fetch_add(&s_count[bucket], 1u, __ATOMIC_RELAXED);
}

static uint32_t take(int b, uint32_t* n) {
    *n = __atomic_exchange_n(&s_count[b], 0u, __ATOMIC_RELAXED);
    return __atomic_exchange_n(&s_us[b], 0u, __ATOMIC_RELAXED);
}

/* tenths of a millisecond per frame */
static unsigned per_frame(uint32_t us, uint32_t frames) { return frames ? (unsigned)((us + frames * 50u) / (frames * 100u)) : 0; }

void xhw_perf_frame(void) {
    uint64_t now = xhw_time_us(), wall;
    uint32_t us[XHW_PERF_N], n[XHW_PERF_N], used;
    unsigned fps10, frame10, rest10;
    int b;
    if (!XHW_PERF) return;
    if (!s_t0) s_t0 = now;
    s_frames++;
    wall = now - s_t0;
    if (wall < XHW_PERF_SECS * 1000000ull) return;
    for (b = 0; b < XHW_PERF_N; b++) us[b] = take(b, &n[b]);
    fps10 = (unsigned)((uint64_t)s_frames * 10000000ull / wall);
    frame10 = (unsigned)(wall / ((uint64_t)s_frames * 100u));
    used = us[XHW_PERF_DL] + us[XHW_PERF_PRESENT] + us[XHW_PERF_AUDIO];
    rest10 = used / 100u / s_frames < frame10 ? frame10 - (unsigned)(used / 100u / s_frames) : 0;
    xhw_logf("[PERF] %u.%u fps, %u.%u ms a frame: display lists %u.%u (textures %u.%u), present %u.%u "
             "(GPU and flip waits %u.%u), audio %u.%u (%u tasks a second), the rest %u.%u (game logic, idle)",
             fps10 / 10, fps10 % 10, frame10 / 10, frame10 % 10, per_frame(us[XHW_PERF_DL], s_frames) / 10,
             per_frame(us[XHW_PERF_DL], s_frames) % 10, per_frame(us[XHW_PERF_TEX], s_frames) / 10,
             per_frame(us[XHW_PERF_TEX], s_frames) % 10, per_frame(us[XHW_PERF_PRESENT], s_frames) / 10,
             per_frame(us[XHW_PERF_PRESENT], s_frames) % 10, per_frame(us[XHW_PERF_WAIT], s_frames) / 10,
             per_frame(us[XHW_PERF_WAIT], s_frames) % 10, per_frame(us[XHW_PERF_AUDIO], s_frames) / 10,
             per_frame(us[XHW_PERF_AUDIO], s_frames) % 10,
             (unsigned)((uint64_t)n[XHW_PERF_AUDIO] * 1000000ull / wall), rest10 / 10, rest10 % 10);
    s_frames = 0;
    s_t0 = now;
}
