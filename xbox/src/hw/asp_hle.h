/* asp_hle.h - the RSP audio microcode (aspMain, classic libultra ABI) in C.
 *
 * PLAN.md D6 / M6. Design, byte order of every buffer and the DIVERGES list
 * are in asp_hle.c's top comment. Plain C, no nxdk or kernel calls: the same
 * file builds for the console (nxdk triple) and for the host tests
 * (tools/xbox/test_asp_hle.py).
 *
 * One audio thread drives it: the state (DMEM, the resampler table, the
 * counters) is global and not locked. */
#ifndef ASP_HLE_H
#define ASP_HLE_H

#include <stdint.h>

/* RDRAM translation. `n64_addr` is the address an SP DMA would use: the
 * command's word after the RSP's segment resolution (segment table, A_SEGMENT)
 * with the low 3 bits cleared, as the SP DMA engine ignores them. Return a
 * host pointer to it, valid for the length of the transfer (at most 4 KB), or
 * NULL: the command then skips that transfer (reads as zeros) and
 * asp_hle_stats()->bad_addr counts it. */
typedef void* (*asp_addr_fn)(uint32_t n64_addr, void* user);

/* Runs one audio task: `cmds` holds `n_cmds` Acmd packets as native uint32
 * pairs, cmds[2*i] = w0 and cmds[2*i+1] = w1 (the port's Acmd is `struct {
 * unsigned int w0, w1; }` in host order, so the task's data_ptr can be passed
 * as is). */
void asp_hle_run(const uint32_t* cmds, uint32_t n_cmds, asp_addr_fn addr, void* user);

/* The A_RESAMPLE filter: 64 phases x 4 taps, s16 Q15, the table inside
 * aspMain's data segment. It is read from the user's ROM at run time and never
 * stored in the tree: US ROM offset ASP_HLE_LUT_ROM_US, 512 bytes, big-endian.
 * Both setters check the table (phase 63-p is phase p reversed, every phase's
 * taps sum to 0x7000..0x8FFF) and install it only if it passes; they return 1
 * when installed, 0 when the linear fallback stays (see DIVERGES in asp_hle.c). */
#define ASP_HLE_LUT_ROM_US 0x51C30u
int asp_hle_set_resample_lut(const int16_t lut[256]);
int asp_hle_set_resample_lut_be(const uint8_t be[512]);
int asp_hle_resample_lut_valid(const int16_t lut[256]);

/* Back to power-on: DMEM cleared, the linear resampler table, counters zeroed. */
void asp_hle_reset(void);

typedef struct {
    uint32_t tasks;       /* asp_hle_run calls */
    uint32_t cmds[17];    /* commands by opcode; [16] = unknown opcodes (ignored) */
    uint32_t bad_addr;    /* transfers skipped: the callback returned NULL */
    uint32_t rom_lut;     /* 1: the ROM's resampler table is installed */
} asp_hle_stats_t;

const asp_hle_stats_t* asp_hle_stats(void);

/* The 4 KB DMEM model (host tests). */
uint8_t* asp_hle_dmem(void);

#endif
