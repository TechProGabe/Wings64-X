/* xhw_ramlock.c - a console upgraded to 128 MB runs as a stock 64 MB one
 * (PLAN.md D2; from OpenCrossing-Xbox's xbox_ramlock.c, MIT).
 *
 * Everything is built and tested for 64 MB: pbkit and the renderer mask GPU
 * addresses with 0x03FFFFFF, nxdk's XVideo framebuffer and the audio DMA
 * buffers may come from anywhere below 0x7FFFFFFF, and allocations that
 * fail on 64 MB (and have fallbacks, like the video mode's) must fail the
 * same way on every console. cxbe marks the XBE "limit to 64 MB", and a
 * 128 MB-aware BIOS then keeps the upper 64 MB from the title. For a kernel
 * that hands it out anyway, xhw_ram_lock64 holds it, first thing in
 * main_body, before anything else allocates.
 *
 * How: commit every free page (bar a small reserve) in a top-down
 * reservation, keep the pages whose physical address is at or above 64 MB,
 * decommit the rest, and repeat while a pass still finds some. On a 64 MB
 * console nothing happens (the kernel counts 64 MB of pages). The held
 * pages are never given back. Kill switch: -DXHW_RAM_LOCK64=0 (diagnosis
 * only). */
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <string.h>

#include "xhw.h"
#include "xhw_internal.h"

#ifndef XHW_RAM_LOCK64
#define XHW_RAM_LOCK64 1
#endif

#define LOW_TOP (64u * 1024 * 1024)
#define PAGE 4096u
#define COMMIT_STEP (1024u * 1024)
#define RESERVE_KB 256u   /* left free while a pass runs, for the kernel */
#define MAX_PASSES 4

static int s_upper;   /* the kernel counts pages above 64 MB */
static unsigned s_held_kb, s_passes, s_ms, s_free_before_kb, s_total_kb;

static int has_upper(void) {
    MM_STATISTICS st;
    memset(&st, 0, sizeof st);
    st.Length = sizeof st;
    if (MmQueryStatistics(&st) < 0) return 0;
    s_total_kb = (unsigned)(st.TotalPhysicalPages * 4);
    return st.TotalPhysicalPages * 4096ull > LOW_TOP;
}

/* one pass: returns the KB of upper pages it kept */
static unsigned hold_pass(void) {
    unsigned free_kb = xhw_mem_free_kb(), kept = 0;
    PVOID base = NULL;
    SIZE_T size, done = 0, run_lo = 0, off;
    int in_run = 0;
    if (free_kb <= RESERVE_KB + 64) return 0;
    size = (SIZE_T)(free_kb - RESERVE_KB) * 1024;
    if (!NT_SUCCESS(NtAllocateVirtualMemory(&base, 0, &size, MEM_RESERVE | MEM_TOP_DOWN, PAGE_READWRITE)))
        return 0;
    while (done < size) {
        PVOID p = (PVOID)((uintptr_t)base + done);
        SIZE_T n = size - done < COMMIT_STEP ? size - done : COMMIT_STEP;
        if (!NT_SUCCESS(NtAllocateVirtualMemory(&p, 0, &n, MEM_COMMIT, PAGE_READWRITE))) {
            if (n <= PAGE) break;
            n = PAGE;   /* the last few pages one at a time */
            p = (PVOID)((uintptr_t)base + done);
            if (!NT_SUCCESS(NtAllocateVirtualMemory(&p, 0, &n, MEM_COMMIT, PAGE_READWRITE))) break;
        }
        done += n;
    }
    /* give back the pages below 64 MB, a run at a time */
    for (off = 0; off <= done; off += PAGE) {
        int low = off < done && MmGetPhysicalAddress((PVOID)((uintptr_t)base + off)) < LOW_TOP;
        if (off < done && !low) kept += PAGE / 1024;
        if (low && !in_run) {
            in_run = 1;
            run_lo = off;
        } else if (!low && in_run) {
            PVOID p = (PVOID)((uintptr_t)base + run_lo);
            SIZE_T n = off - run_lo;
            NtFreeVirtualMemory(&p, &n, MEM_DECOMMIT);
            in_run = 0;
        }
    }
    if (!kept) {
        SIZE_T n = 0;
        NtFreeVirtualMemory(&base, &n, MEM_RELEASE);
    }
    return kept;
}

void xhw_ram_lock64(void) {
    DWORD t0;
    s_upper = has_upper();
    if (!s_upper || !XHW_RAM_LOCK64) return;
    t0 = GetTickCount();
    s_free_before_kb = xhw_mem_free_kb();
    while (s_passes < MAX_PASSES) {
        unsigned kb = hold_pass();
        s_passes++;
        s_held_kb += kb;
        if (!kb) break;
    }
    s_ms = (unsigned)(GetTickCount() - t0);
}

void xhw_ram_lock64_log(void) {
    if (!s_upper) {
        xhw_logf("[MEM] boot: %u KB of RAM, nothing to hold (64 MB console)", s_total_kb);
        return;
    }
    if (!XHW_RAM_LOCK64)
        xhw_logf("[MEM] boot: 128 MB console, XHW_RAM_LOCK64=0: the RAM above 64 MB is not held");
    else if (s_held_kb)
        xhw_logf("[MEM] boot: 128 MB console, upper 64 MB held (%u KB, free %u -> %u KB, %u passes, %u ms), "
                 "running as 64 MB", s_held_kb, s_free_before_kb, xhw_mem_free_kb(), s_passes, s_ms);
    else
        xhw_logf("[MEM] boot: 128 MB console, nothing above 64 MB is free to the title (the kernel keeps it; "
                 "%u ms), running as 64 MB", s_ms);
}
