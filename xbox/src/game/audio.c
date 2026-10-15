/* audio.c - the game's audio tasks and AI buffers (PLAN.md D6, M6).
 *
 * An audio task's command list (the libultra synthesizer's Acmd packets) is
 * run by the RSP audio HLE (xbox/src/hw/asp_hle.c) on the game's own audio
 * thread, at osSpTaskStartGo time (patches/022). The AI buffer the list
 * wrote is handed over in osAiSetNextBuffer (os_shim.c, whose virtual DAC
 * keeps paying the game's audio clock in wall-clock time) and goes into the
 * AC97 pump's ring (xbox/src/hw/xhw_audio.c, 22050 -> 48 kHz).
 *
 * Addresses: patches/029 makes osVirtualToPhysical the identity, so every
 * address word in the list is a host pointer (asp_hle.c builds with
 * ASP_HLE_SEGMENTS 0 on the console) and the translation is the identity.
 * The A_RESAMPLE filter table is read from the user's ROM at run time
 * (ASP_HLE_LUT_ROM_US), never stored in the tree.
 *
 * Kill switch: -DWX_AUDIO=0 counts the lists and drops them (silent, as
 * before M6). */
#include <ultra64.h>
#include <stdint.h>

#include "wx_game.h"
#include "xhw.h"
#include "../hw/asp_hle.h"

#ifndef WX_AUDIO
#define WX_AUDIO 1
#endif
#define STATS_US 10000000ull   /* an [AUDIO] line every 10 s */

u32 wx_ai_underruns(void);   /* os_shim.c: the virtual DAC's dropped deficits */

static uint32_t s_rate = 22050;
static int s_up, s_on;
static unsigned s_tasks, s_buffers;
static uint64_t s_frames_in, s_frames_dropped, s_task_us, s_stats_t0;
static unsigned s_tasks_iv, s_peak;

static void* asp_addr(uint32_t a, void* user) {
    (void)user;
    return (void*)(uintptr_t)a;
}

static void audio_up(void) {
    const uint8_t* lut;
    int lut_ok;
    if (s_up) return;
    s_up = 1;
    lut = wx_rom_ptr(ASP_HLE_LUT_ROM_US, 512);
    lut_ok = lut && asp_hle_set_resample_lut_be(lut);
    s_on = xhw_audio_init(s_rate);
    xhw_logf("[AUDIO] RSP audio HLE: resampler table %s; output %s at %u Hz",
             lut_ok ? "from the ROM" : "NOT valid in the ROM (linear fallback)", s_on ? "on" : "OFF (silent)",
             (unsigned)s_rate);
}

void wx_audio_task(void* task) {
    OSTask* t = (OSTask*)task;
    uint64_t t0;
    s_tasks++;
    if (!WX_AUDIO) return;
    audio_up();
    t0 = xhw_time_us();
    if (XHW_TEST_BUILD && s_tasks <= 3)
        xhw_logf("[AUDIO] task %u: %u commands at %p", s_tasks, (unsigned)(t->t.data_size / 8u), t->t.data_ptr);
    asp_hle_run((const uint32_t*)t->t.data_ptr, t->t.data_size / 8u, asp_addr, NULL);
    s_task_us += xhw_time_us() - t0;
    xhw_perf_add(XHW_PERF_AUDIO, xhw_time_us() - t0);
    if (XHW_TEST_BUILD && s_tasks <= 3)
        xhw_logf("[AUDIO] task %u done in %u us", s_tasks, (unsigned)(xhw_time_us() - t0));
    s_tasks_iv++;
    if (!s_stats_t0) s_stats_t0 = t0;
    if (t0 - s_stats_t0 >= STATS_US) {
        const asp_hle_stats_t* st = asp_hle_stats();
        xhw_logf("[AUDIO] %u tasks (%u us each), %u buffers, %u frames in, %u dropped (ring full), peak %u, "
                 "%u AI underruns, %u bad addresses, %u unknown commands",
                 s_tasks, (unsigned)(s_task_us / (s_tasks_iv ? s_tasks_iv : 1)), s_buffers, (unsigned)s_frames_in,
                 (unsigned)s_frames_dropped, s_peak, (unsigned)wx_ai_underruns(), (unsigned)st->bad_addr,
                 (unsigned)st->cmds[16]);
        s_task_us = 0;
        s_tasks_iv = 0;
        s_peak = 0;
        s_stats_t0 = t0;
    }
}

void wx_audio_rate(uint32_t hz) {
    xhw_logf("[AUDIO] AI rate %u Hz", (unsigned)hz);
    if (!hz) return;
    s_rate = hz;
    if (s_on) xhw_audio_set_rate(hz);
}

void wx_audio_buffer(void* buf, uint32_t bytes) {
    uint32_t frames = bytes / 4u, space;
    s_buffers++;
    if (XHW_TEST_BUILD && s_buffers <= 3) xhw_logf("[AUDIO] AI buffer %u: %u bytes at %p", s_buffers, (unsigned)bytes, buf);
    if (!WX_AUDIO || !s_on || !buf || !frames) return;
    space = xhw_audio_space();
    if (frames > space) s_frames_dropped += frames - space;
    {   /* the loudest sample since the last [AUDIO] line: is anything audible */
        const int16_t* p = (const int16_t*)buf;
        uint32_t i;
        for (i = 0; i < frames * 2u; i += 7) {
            unsigned a = (unsigned)(p[i] < 0 ? -p[i] : p[i]);
            if (a > s_peak) s_peak = a;
        }
    }
    xhw_audio_write((const int16_t*)buf, frames);
    s_frames_in += frames;
}
