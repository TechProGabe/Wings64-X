/* xhw_clock.c - time from the ACPI timer, never from an assumed CPU clock.
 *
 * KeQueryPerformanceCounter on the Xbox kernel is the ACPI power-management
 * timer: 3.375 MHz from its own crystal, the same on every console whatever
 * CPU is fitted (OpenCrossing-Xbox docs/traps.md "Timer frequencies").
 * nxdk's QueryPerformanceCounter is the CPU's TSC instead, its rate worked
 * out from the multiplier and FSB with a 733 MHz fallback, which is wrong on
 * a 1.0/1.4 GHz upgrade or an overclock (OpenCrossing GitHub #2). So:
 *
 *   - every clock the port uses (log timestamps, osGetTime/osGetCount, the
 *     frame pacer's fallback, the watchdog) is the ACPI timer;
 *   - frame pacing is the retrace (xhw_wait_vblank), audio pacing the AC97;
 *   - the TSC is read in exactly one place, here, at boot, to measure the
 *     CPU's clock against the ACPI timer for the [CLOCK] line. A BIOS that
 *     rewrites rdtsc (0f 31 -> int 2Eh, OpenCrossing #3) can only spoil that
 *     line; -DXHW_CLOCK_TSC=0 skips it.
 *
 * xhw_clock_recheck (the watchdog, every 30 s) compares the ACPI timer with
 * the retrace count: a console whose vblank is not 59.94/60 Hz, or a timer
 * that runs off, shows up as a [CLOCK] line. */
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <stdint.h>

#include "xhw.h"
#include "xhw_internal.h"

#ifndef XHW_CLOCK_TSC
#define XHW_CLOCK_TSC 1
#endif

static uint64_t s_freq;
static uint32_t s_cpu_mhz;

uint64_t xhw_ticks(void) { return KeQueryPerformanceCounter(); }

uint64_t xhw_ticks_per_sec(void) {
    if (!s_freq) s_freq = KeQueryPerformanceFrequency();
    return s_freq;
}

uint64_t xhw_time_ns(void) {
    uint64_t t = KeQueryPerformanceCounter(), f = xhw_ticks_per_sec();
    return (t / f) * 1000000000ull + (t % f) * 1000000000ull / f;
}

uint64_t xhw_time_us(void) {
    uint64_t t = KeQueryPerformanceCounter(), f = xhw_ticks_per_sec();
    return (t / f) * 1000000ull + (t % f) * 1000000ull / f;
}

uint32_t xhw_cpu_mhz(void) { return s_cpu_mhz; }

#if XHW_CLOCK_TSC
static uint64_t read_tsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (uint64_t)hi << 32 | lo;
}
#endif

void xhw_clock_calibrate(void) {
    uint64_t f = xhw_ticks_per_sec();
    LARGE_INTEGER nf;
    QueryPerformanceFrequency(&nf);
#if XHW_CLOCK_TSC
    {
        uint64_t k0, k1, c0, c1, hz;
        /* 100 ms of ACPI ticks, the TSC read at both ends */
        k0 = KeQueryPerformanceCounter();
        c0 = read_tsc();
        do {
            k1 = KeQueryPerformanceCounter();
        } while (k1 - k0 < f / 10);
        c1 = read_tsc();
        hz = (c1 - c0) * f / (k1 - k0);
        s_cpu_mhz = (uint32_t)((hz + 500000) / 1000000);
        xhw_logf("[CLOCK] cpu %u MHz (TSC against the ACPI timer, %u Hz); nxdk's guess %u MHz%s",
                 s_cpu_mhz, (unsigned)f, (unsigned)(nf.QuadPart / 1000000),
                 (nf.QuadPart / 1000000 > s_cpu_mhz + s_cpu_mhz / 50 || nf.QuadPart / 1000000 + s_cpu_mhz / 50 < s_cpu_mhz)
                     ? " (differs: nothing here uses it)" : "");
    }
#else
    xhw_logf("[CLOCK] ACPI timer %u Hz; TSC not read (XHW_CLOCK_TSC=0); nxdk's guess %u MHz", (unsigned)f,
             (unsigned)(nf.QuadPart / 1000000));
#endif
}

/* The watchdog thread, every 30 s: retraces counted against ACPI time. */
void xhw_clock_recheck(void) {
    static uint64_t s_t0;
    static unsigned s_f0;
    uint64_t t = xhw_time_us();
    unsigned f = xhw_frame_count();
    if (s_t0 && f > s_f0 && t > s_t0) {
        unsigned mhz100 = (unsigned)((uint64_t)(f - s_f0) * 100000000ull / (t - s_t0));   /* Hz x 100 */
        if (mhz100 < 5900 || mhz100 > 6100)
            xhw_logf("[CLOCK] retrace %u.%02u Hz over the last %u s of ACPI time", mhz100 / 100, mhz100 % 100,
                     (unsigned)((t - s_t0) / 1000000));
    }
    s_t0 = t;
    s_f0 = f;
}
