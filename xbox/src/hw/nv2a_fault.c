/* nv2a_fault.c - the hooks the patched pbkit calls (tools/xbox/patch_pbkit.py),
 * from Melee-X's nv2a.c: GPU faults recorded at DPC level and logged later
 * at passive level, PGRAPH context switches counted, and the interrupt-storm
 * flag the renderer clears (nv2a_irq_recover).
 *
 * Melee-X's defaults are kept: a context switch for the channel already
 * loaded keeps the live state (XGX_CTX_KEEP=1), and the back-buffer retarget
 * sends the pitch again (XGX_COPY_FIX bit 1). */
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <stdint.h>

#include "nv2a_fault.h"
#include "xhw.h"

#ifndef NV2A_CTX_KEEP
#define NV2A_CTX_KEEP 1
#endif

int ocx_pb_retarget_repitch = 1;
volatile int ocx_pb_ctx_keep = NV2A_CTX_KEEP;
volatile int ocx_pb_irq_off;

static volatile uint32_t s_ctx_switches, s_ctx_last[2];

/* from pbkit's PGRAPH interrupt, at DPC level: count only */
void ocx_pb_ctx_switch(unsigned chid, unsigned current) {
    s_ctx_switches++;
    s_ctx_last[0] = chid;
    s_ctx_last[1] = current;
}

static volatile uint32_t s_gf_count, s_gf_storms, s_gf_last[5];
static volatile uint32_t s_gf_first[9], s_gf_first_logged;
static volatile uint32_t s_gf_limit[4];
static volatile uint32_t s_gf_regs[128];
volatile uint32_t nv2a_fault_frame, nv2a_fault_draw;   /* the renderer's position, for the first fault */

/* from the patched pbkit's interrupt handlers (DPC level): record only */
void ocx_pb_gpu_fault(unsigned kind, unsigned a, unsigned b, unsigned c, unsigned d) {
    if (!s_gf_count) {
        int i;
        s_gf_first[0] = kind;
        s_gf_first[1] = a;
        s_gf_first[2] = b;
        s_gf_first[3] = c;
        s_gf_first[4] = d;
        s_gf_first[5] = *(volatile uint32_t*)(0xFD000000u + 0x3244);   /* DMA GET */
        s_gf_first[6] = *(volatile uint32_t*)(0xFD000000u + 0x3240);   /* DMA PUT */
        s_gf_first[7] = nv2a_fault_frame;
        s_gf_first[8] = nv2a_fault_draw;
        for (i = 0; i < 4; i++) s_gf_limit[i] = *(volatile uint32_t*)(0xFD400800u + (uint32_t)i * 4);
        for (i = 0; i < 128; i++) s_gf_regs[i] = *(volatile uint32_t*)(0xFD400700u + (uint32_t)i * 4);
    }
    s_gf_last[0] = kind;
    s_gf_last[1] = a;
    s_gf_last[2] = b;
    s_gf_last[3] = c;
    s_gf_last[4] = d;
    s_gf_count++;
    if (kind == 3 && ++s_gf_storms >= 16) ocx_pb_irq_off = 1;
}

uint32_t nv2a_fault_count(void) { return s_gf_count; }

/* Passive level: the first fault in full, once ([NV2A] first GPU fault). */
void nv2a_fault_log_first(uint32_t pushbuffer_phys) {
    int i;
    if (!s_gf_count || s_gf_first_logged) return;
    s_gf_first_logged = 1;
    xhw_logf("[NV2A] first GPU fault: kind %u %08x %08x %08x %08x, get %08x put %08x (pushbuffer %08x), frame %u "
             "draw %u",
             (unsigned)s_gf_first[0], (unsigned)s_gf_first[1], (unsigned)s_gf_first[2], (unsigned)s_gf_first[3],
             (unsigned)s_gf_first[4], (unsigned)s_gf_first[5], (unsigned)s_gf_first[6], (unsigned)pushbuffer_phys,
             (unsigned)s_gf_first[7], (unsigned)s_gf_first[8]);
    xhw_logf("[NV2A]  first fault pgraph 400800: %08x %08x %08x %08x", (unsigned)s_gf_limit[0],
             (unsigned)s_gf_limit[1], (unsigned)s_gf_limit[2], (unsigned)s_gf_limit[3]);
    {   /* what the pusher had just read: 64 words before GET, 32 from it */
        const uint32_t* w = (const uint32_t*)(0x80000000u | (s_gf_first[5] & 0x03FFFFFCu));
        if (MmIsAddressValid((PVOID)(w - 64)) && MmIsAddressValid((PVOID)(w + 31)))
            for (i = -64; i < 32; i += 8)
                xhw_logf("[NV2A]  first fault get%+d: %08x %08x %08x %08x %08x %08x %08x %08x", i * 4, w[i], w[i + 1],
                         w[i + 2], w[i + 3], w[i + 4], w[i + 5], w[i + 6], w[i + 7]);
    }
    for (i = 0; i < 128; i += 8)
        xhw_logf("[NV2A]  first fault pgraph %06x: %08x %08x %08x %08x %08x %08x %08x %08x", 0x400700 + i * 4,
                 (unsigned)s_gf_regs[i], (unsigned)s_gf_regs[i + 1], (unsigned)s_gf_regs[i + 2],
                 (unsigned)s_gf_regs[i + 3], (unsigned)s_gf_regs[i + 4], (unsigned)s_gf_regs[i + 5],
                 (unsigned)s_gf_regs[i + 6], (unsigned)s_gf_regs[i + 7]);
}

/* An interrupt storm made the patched pbkit leave the GPU interrupt masked.
 * Vblank flips need it: turn it back on (passive level) and say so. */
void nv2a_irq_recover(void) {
    if (!ocx_pb_irq_off) return;
    ocx_pb_irq_off = 0;
    s_gf_storms = 0;
    *(volatile uint32_t*)0xFD000140u = 1;   /* NV_PMC_INTR_EN_0 = INTA_HARDWARE */
    xhw_logf("[NV2A] GPU interrupt storm: interrupt re-enabled (faults %u)", (unsigned)s_gf_count);
}

void nv2a_fault_log_ctx(void) {
    static uint32_t s_logged;
    if (s_ctx_switches != s_logged) {
        xhw_logf("[NV2A] PGRAPH context switches: %u (last chid %u, loaded %u)", (unsigned)s_ctx_switches,
                 (unsigned)s_ctx_last[0], (unsigned)s_ctx_last[1]);
        s_logged = s_ctx_switches;
    }
}
