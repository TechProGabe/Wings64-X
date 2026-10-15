/* xhw_prof.c - sampling profiler over the game's threads, on the console.
 *
 * Built with -DXHW_PROF=1 (off by default). Melee-X's xhw_prof.c samples its
 * one game thread; the libultra shim runs several kernel threads
 * (xhw_thread_spawn), so this one tells which of them ran: a thread that the
 * clock interrupt found running has its KTHREAD KernelTime advanced by that
 * tick and is Ready (preempted by the sampler) with the interrupt frame the
 * CPU pushed on its kernel stack: EIP, CS 0x08, EFLAGS with IF set (Melee-X's
 * xhw_thread_eip). A time-critical thread wakes every millisecond and counts
 * that EIP per 64 bytes of the XBE image, and per thread.
 *
 * Every XHW_PROF_SECS: [PROF] lines with the hottest buckets (addr:samples),
 * [PROFT] samples per thread level, and the samples that ran outside the
 * image (kernel, nxdk). tools/xbox/sym.py --prof <boot.log> folds the
 * buckets into functions with the build's map. */
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xhw.h"
#include "xhw_internal.h"

#ifndef XHW_PROF
#define XHW_PROF 0
#endif
#ifndef XHW_PROF_SECS
#define XHW_PROF_SECS 10
#endif
#define BUCKET_SHIFT 6
#define TOP 60
#define MAX_THREADS 24

static PKTHREAD s_kt[MAX_THREADS];
static int s_level[MAX_THREADS];
static volatile LONG s_nthreads;

/* xhw_sys.c: every xhw_thread_spawn thread, at its entry */
void xhw_prof_register(void* kthread, int level) {
    LONG i = InterlockedIncrement(&s_nthreads) - 1;
    if (i >= MAX_THREADS) return;
    s_level[i] = level;
    s_kt[i] = (PKTHREAD)kthread;
}

/* the interrupted EIP from a preempted thread's kernel stack, 0 when none */
static ULONG thread_eip(PKTHREAD t) {
    ULONG* sp = (ULONG*)t->KernelStack;
    ULONG* top = (ULONG*)t->StackBase;
    int i;
    if (!sp || !top || sp >= top || ((ULONG)sp & 3)) return 0;
    for (i = 0; i < 160 && sp + 2 < top; i++, sp++)
        if (sp[1] == 0x08 && (sp[2] & 0x202) == 0x202 && !(sp[2] & 0xFFC00000u)) return sp[0];
    return 0;
}

#if XHW_PROF
static uint16_t* s_hist;
static uint32_t s_nbuckets;
static ULONG s_ktime[MAX_THREADS];
static uint32_t s_thread_samples[MAX_THREADS];
static uint32_t s_placed, s_outside, s_noframe, s_idle;

static char s_rep[8192];
static int s_rlen;

static void rep(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void rep(const char* fmt, ...) {
    va_list ap;
    int n;
    if (s_rlen >= (int)sizeof s_rep - 1) return;
    va_start(ap, fmt);
    n = vsnprintf(s_rep + s_rlen, sizeof s_rep - (size_t)s_rlen, fmt, ap);
    va_end(ap);
    if (n > 0) s_rlen += n;
    if (s_rlen > (int)sizeof s_rep - 1) s_rlen = (int)sizeof s_rep - 1;
}

static void report(void) {
    uint32_t i, k, best[TOP], bestn[TOP];
    int t, n = s_nthreads < MAX_THREADS ? (int)s_nthreads : MAX_THREADS;
    memset(best, 0, sizeof best);
    memset(bestn, 0, sizeof bestn);
    for (i = 0; i < s_nbuckets; i++) {
        uint32_t c = s_hist[i];
        if (!c || c <= bestn[TOP - 1]) continue;
        for (k = TOP - 1; k > 0 && bestn[k - 1] < c; k--) {
            best[k] = best[k - 1];
            bestn[k] = bestn[k - 1];
        }
        best[k] = i;
        bestn[k] = c;
    }
    s_rlen = 0;
    rep("[PROF] %u s: %u samples in the image, %u outside it (kernel, nxdk), %u without a frame, %u idle\n",
        XHW_PROF_SECS, s_placed, s_outside, s_noframe, s_idle);
    rep("[PROFT]");
    for (t = 0; t < n; t++)
        if (s_thread_samples[t]) rep(" L%d:%u", s_level[t], s_thread_samples[t]);
    rep("\n");
    for (k = 0; k < TOP && bestn[k]; k += 8) {
        int j;
        rep("[PROF]");
        for (j = 0; j < 8 && k + j < TOP && bestn[k + j]; j++)
            rep(" %08x:%u", xhw_image_base + (best[k + j] << BUCKET_SHIFT), bestn[k + j]);
        rep("\n");
    }
    if (s_rlen > 0 && s_rep[s_rlen - 1] == '\n') s_rep[--s_rlen] = '\0';
    xhw_log(s_rep);
    memset(s_hist, 0, s_nbuckets * sizeof s_hist[0]);
    memset(s_thread_samples, 0, sizeof s_thread_samples);
    s_placed = s_outside = s_noframe = s_idle = 0;
}

static void sampler(void* arg) {
    LARGE_INTEGER ms;
    uint64_t next = xhw_time_us() + XHW_PROF_SECS * 1000000ull;
    (void)arg;
    ms.QuadPart = -10000;   /* 1 ms */
    KeSetBasePriorityThread(KeGetCurrentThread(), 7);   /* 8 + 7: above every game thread (14) */
    KeSetPriorityThread(KeGetCurrentThread(), 15);
    for (;;) {
        int t, n, any = 0;
        KeDelayExecutionThread(KernelMode, FALSE, &ms);
        n = s_nthreads < MAX_THREADS ? (int)s_nthreads : MAX_THREADS;
        for (t = 0; t < n; t++) {
            PKTHREAD kt = s_kt[t];
            ULONG kt_time, eip;
            if (!kt) continue;
            kt_time = kt->KernelTime;
            if (kt_time == s_ktime[t]) continue;
            s_ktime[t] = kt_time;
            if (kt->State != 1 /* Ready: preempted */) continue;
            any = 1;
            s_thread_samples[t]++;
            eip = thread_eip(kt);
            if (!eip) s_noframe++;
            else if (eip >= xhw_image_base && eip < xhw_image_end) {
                uint32_t b = (eip - xhw_image_base) >> BUCKET_SHIFT;
                if (b < s_nbuckets && s_hist[b] < 0xFFFF) s_hist[b]++;
                s_placed++;
            } else {
                s_outside++;
            }
        }
        if (!any) s_idle++;
        if (xhw_time_us() >= next) {
            report();
            next = xhw_time_us() + XHW_PROF_SECS * 1000000ull;
        }
    }
}

void xhw_prof_start(void) {
    s_nbuckets = ((xhw_image_end - xhw_image_base) >> BUCKET_SHIFT) + 1;
    s_hist = (uint16_t*)calloc(s_nbuckets, sizeof s_hist[0]);
    if (!s_hist) return;
    xhw_thread_start(sampler, NULL, 2, 16 * 1024);
    xhw_logf("[PROF] sampling profiler on: every 1 ms, a report every %u s", XHW_PROF_SECS);
}
#else
void xhw_prof_start(void) {}
#endif
